// ============================================================================
//  Backend "RICOH Camera USB SDK" : la même couche de communication USB/PTP
//  propriétaire qu'Image Transmitter 2 (appareils supportés : PENTAX 645Z,
//  K-1, K-1 II, KP — boîtier en mode USB "PTP").
//
//  Compilé uniquement quand CMake est configuré avec
//  -DRICOH_CAMERA_SDK_ROOT=<racine du SDK C++> (téléchargé depuis
//  https://api.ricoh — le SDK n'est pas redistribuable avec ce dépôt).
//  Écrit pour l'API du RICOH Camera USB SDK for C++ v1.1.x.
//
//  Contrairement à libgphoto2 (polling), le SDK pousse les événements :
//  l'image capturée est annoncée par imageAdded() et le Live View arrive en
//  flux continu par liveViewFrameUpdated(), sans boucle d'interrogation.
// ============================================================================
#include "CameraBackend.h"

#if PENTAX_WITH_RICOH_SDK

#include <ricoh_camera_sdk.hpp>

#include <fstream>

using namespace Ricoh::CameraController;

namespace
{
    juce::String utf8 (const char* text)        { return juce::String (juce::CharPointer_UTF8 (text)); }
    juce::String utf8 (const std::string& text) { return utf8 (text.c_str()); }
}

//==============================================================================
class RicohSdkBackend : public NotifyingCameraBackend,
                        private juce::Thread
{
public:
    RicohSdkBackend() : juce::Thread ("Ricoh SDK Session Thread") {}

    ~RicohSdkBackend() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (8000);
    }

    void start() override
    {
        startThread (juce::Thread::Priority::high);
    }

    void triggerCapture() override
    {
        if (getState() == CameraState::ready)
        {
            captureRequested.store (true);
            notify();
        }
    }

    void setLiveViewEnabled (bool shouldBeEnabled) override
    {
        liveViewWanted.store (shouldBeEnabled);
        notify();
    }

    bool isLiveViewEnabled() const override   { return liveViewWanted.load(); }

    juce::String getBackendName() const override
    {
        return "RICOH Camera USB SDK (protocole natif Image Transmitter 2)";
    }

private:
    //==========================================================================
    // Les événements du SDK arrivent sur ses propres threads : on ne fait que
    // relayer vers les helpers thread-safe du socle commun.
    //==========================================================================
    class SdkEventListener : public CameraEventListener
    {
    public:
        explicit SdkEventListener (RicohSdkBackend& ownerToUse) : owner (ownerToUse) {}

        void imageAdded (const std::shared_ptr<const CameraDevice>&,
                         const std::shared_ptr<const CameraImage>& image) override
        {
            owner.downloadImage (image);
        }

        void captureComplete (const std::shared_ptr<const CameraDevice>&,
                              const std::shared_ptr<const Capture>&) override
        {
            // L'obturateur a fini son cycle ; le transfert éventuel est signalé
            // séparément par imageAdded.
            if (owner.getState() == CameraState::capturing)
                owner.setState (CameraState::ready, utf8 ("Prêt"));
        }

        void liveViewFrameUpdated (const std::shared_ptr<const CameraDevice>&,
                                   const std::shared_ptr<const unsigned char>& frameData,
                                   uint64_t frameSize) override
        {
            if (frameData == nullptr || frameSize == 0)
                return;

            // Trame JPEG poussée par l'appareil (~30 i/s sur le 645Z).
            auto frame = juce::ImageFileFormat::loadFrom (frameData.get(), (size_t) frameSize);
            if (frame.isValid())
                owner.notifyLiveViewFrame (frame);
        }

        void deviceDisconnected (const std::shared_ptr<const CameraDevice>&,
                                 DeviceInterface) override
        {
            owner.connectionLost.store (true);
            owner.notify();
        }

    private:
        RicohSdkBackend& owner;
    };

    //==========================================================================
    void run() override
    {
        while (! threadShouldExit())
        {
            if (camera == nullptr)
            {
                setState (CameraState::connecting,
                          utf8 ("Recherche d'un PENTAX (645Z / K-1 / K-1 II / KP) sur le bus USB..."));

                if (! connectCamera())
                {
                    setState (CameraState::disconnected,
                              utf8 ("Aucun appareil détecté — nouvelle tentative dans 2 s"));
                    wait (reconnectDelayMs);
                    continue;
                }
            }

            if (connectionLost.exchange (false))
            {
                teardownCamera();
                setState (CameraState::disconnected, utf8 ("Connexion USB perdue (câble débranché ?)"));
                continue;
            }

            if (captureRequested.exchange (false))
                performCapture();

            syncLiveView();

            // Tout le reste (images, trames Live View, déconnexion) arrive par
            // les callbacks du SDK : le thread n'a plus qu'à dormir.
            wait (100);
        }

        teardownCamera();
    }

    bool connectCamera()
    {
        auto devices = CameraDeviceDetector::detect (DeviceInterface::USB);
        if (devices.empty())
            return false;

        camera = devices.front();
        listener = std::make_shared<SdkEventListener> (*this);
        camera->addEventListener (listener);

        const auto response = camera->connect (DeviceInterface::USB);
        if (response.getResult() != Result::Ok)
        {
            camera->removeEventListener (listener);
            camera.reset();
            listener.reset();
            return false;
        }

        liveViewActive = false;
        setState (CameraState::ready, utf8 ("Prêt — ") + utf8 (camera->getModel())
                                          + utf8 (" (SDK Ricoh)"));
        return true;
    }

    void teardownCamera()
    {
        if (camera != nullptr)
        {
            if (liveViewActive)
                camera->stopLiveView();

            camera->removeEventListener (listener);
            camera->disconnect (DeviceInterface::USB);
            camera.reset();
        }

        listener.reset();
        liveViewActive = false;
    }

    void performCapture()
    {
        setState (CameraState::capturing, utf8 ("Déclenchement en cours..."));

        // startCapture(false) : déclenche SANS relancer l'autofocus, pour une
        // latence minimale (même choix qu'IT2 quand la MAP est déjà faite).
        const auto response = camera->startCapture (false);

        if (response.getResult() != Result::Ok)
        {
            juce::String message (utf8 ("Échec du déclenchement"));
            const auto& errors = response.getErrors();
            if (! errors.empty())
                message << " : " << utf8 (errors.front()->getMessage());

            setState (CameraState::ready, message);
        }
        // Succès : captureComplete / imageAdded prennent le relais.
    }

    void syncLiveView()
    {
        const bool wanted = liveViewWanted.load();
        if (wanted == liveViewActive || camera == nullptr)
            return;

        const auto response = wanted ? camera->startLiveView()
                                     : camera->stopLiveView();

        if (response.getResult() == Result::Ok)
        {
            liveViewActive = wanted;
        }
        else if (wanted)
        {
            liveViewWanted.store (false);
            juce::String message (utf8 ("Live View indisponible"));
            const auto& errors = response.getErrors();
            if (! errors.empty())
                message << " : " << utf8 (errors.front()->getMessage());

            setState (getState(), message);
        }
    }

    //==========================================================================
    // Appelé depuis le thread d'événements du SDK : rapatrie l'image annoncée
    // dans le buffer de l'appareil, en écriture binaire directe sur le disque.
    //==========================================================================
    void downloadImage (const std::shared_ptr<const CameraImage>& image)
    {
        if (image == nullptr)
            return;

        const juce::String name (utf8 (image->getName()));
        setState (CameraState::transferring, utf8 ("Transfert de ") + name + "...");

        const juce::File target (makeUniqueTarget (name));

        std::ofstream stream (target.getFullPathName().toRawUTF8(),
                              std::ofstream::binary);
        if (! stream.is_open())
        {
            setState (CameraState::ready,
                      utf8 ("Impossible d'écrire dans ") + target.getFullPathName());
            return;
        }

        const auto response = image->getData (stream);
        stream.close();

        if (response.getResult() != Result::Ok)
        {
            target.deleteFile();

            juce::String message (utf8 ("Échec du transfert de ") + name);
            const auto& errors = response.getErrors();
            if (! errors.empty())
                message << " : " << utf8 (errors.front()->getMessage());

            setState (CameraState::ready, message);
            return;
        }

        notifyPhotoSaved (target);
        setState (CameraState::ready, utf8 ("Prêt"));
    }

    //==========================================================================
    std::shared_ptr<CameraDevice>    camera;
    std::shared_ptr<SdkEventListener> listener;

    std::atomic<bool> captureRequested { false };
    std::atomic<bool> liveViewWanted { false };
    std::atomic<bool> connectionLost { false };
    bool liveViewActive = false;   // accédé uniquement par le thread de session

    static constexpr int reconnectDelayMs = 2000;

    friend class SdkEventListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RicohSdkBackend)
};

//==============================================================================
std::unique_ptr<CameraBackend> createRicohSdkBackend()
{
    return std::make_unique<RicohSdkBackend>();
}

#endif // PENTAX_WITH_RICOH_SDK
