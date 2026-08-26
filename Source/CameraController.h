#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

// Types opaques de libgphoto2 (évite d'exposer les en-têtes C dans toute l'app)
typedef struct _Camera    Camera;
typedef struct _GPContext GPContext;

//==============================================================================
/**
    Moteur de tethering : possède le thread USB dédié qui maintient la session
    de contrôle PTP ouverte avec le Pentax 645Z.

    Stratégie temps-réel :
      - Le thread USB est le SEUL à toucher libgphoto2 (la lib n'est pas
        thread-safe) ; l'UI ne fait que poster des commandes.
      - triggerCapture() est non bloquant : il lève un drapeau atomique et
        réveille immédiatement le thread (notify()), qui envoie l'ordre PTP
        InitiateCapture via gp_camera_capture().
      - Le fichier est rapatrié depuis le buffer de l'appareil par écriture
        binaire directe sur le disque (gp_file_new_from_fd : zéro copie en
        RAM intermédiaire, crucial pour les RAW ~50 Mo du 645Z).
      - Entre deux captures, le thread écoute gp_camera_wait_for_event :
        détection immédiate d'un débranchement USB et récupération des photos
        déclenchées depuis le boîtier lui-même.
      - En cas de perte de connexion, la session est détruite proprement et
        une reconnexion est retentée toutes les 2 secondes.

    Les notifications vers l'UI passent par AsyncUpdater : les listeners sont
    toujours appelés sur le message thread de JUCE.
*/
class CameraController : private juce::Thread,
                         private juce::AsyncUpdater
{
public:
    enum class State
    {
        disconnected,   // Déconnecté : aucun appareil, reconnexion en cours
        connecting,     // Tentative d'ouverture de session
        ready,          // Prêt : session ouverte, déclenchement possible
        capturing,      // Déclenchement en cours (obturateur)
        transferring    // Transfert du fichier vers le disque
    };

    struct Listener
    {
        virtual ~Listener() = default;

        /** Toujours appelé sur le message thread. */
        virtual void cameraStateChanged (State newState, const juce::String& detail) = 0;

        /** Toujours appelé sur le message thread, après écriture complète du fichier. */
        virtual void photoSaved (const juce::File& savedFile) = 0;

        /** Nouvelle image Live View disponible. Toujours appelé sur le message thread. */
        virtual void liveViewFrameReady (const juce::Image&) {}
    };

    CameraController();
    ~CameraController() override;

    /** Démarre le thread USB (connexion automatique dès que possible). */
    void start();

    /** Demande de déclenchement — non bloquant, latence minimale. */
    void triggerCapture();

    /** Active/désactive le flux Live View (gp_camera_capture_preview).
        Si le driver ne supporte pas l'aperçu, le flux se coupe tout seul et
        l'état "Prêt" est renvoyé avec un message explicatif. */
    void setLiveViewEnabled (bool shouldBeEnabled);
    bool isLiveViewEnabled() const noexcept   { return liveViewOn.load(); }

    void setTargetDirectory (const juce::File& directory);
    juce::File getTargetDirectory() const;

    State getState() const noexcept   { return state.load(); }

    void addListener (Listener* l)     { listeners.add (l); }
    void removeListener (Listener* l)  { listeners.remove (l); }

private:
    //==========================================================================
    void run() override;                 // boucle du thread USB
    void handleAsyncUpdate() override;   // relais vers l'UI (message thread)

    bool connectCamera();
    void disconnectCamera();
    void performCaptureAndDownload();
    bool downloadFile (const char* cameraFolder, const char* cameraName, bool deleteAfter);
    void pollCameraEvents (int timeoutMs);
    void grabLiveViewFrame();

    void setState (State newState, juce::String detail = {});
    void notifyPhotoSaved (const juce::File& file);
    juce::File makeUniqueTarget (const juce::String& cameraName) const;

    //==========================================================================
    GPContext* context = nullptr;
    Camera*    camera  = nullptr;   // accédé uniquement par le thread USB

    std::atomic<State> state { State::disconnected };
    std::atomic<bool>  captureRequested { false };
    std::atomic<bool>  liveViewOn { false };

    mutable juce::CriticalSection dataLock;
    juce::File   targetDirectory;
    juce::String pendingDetail;
    juce::Array<juce::File> pendingSavedFiles;
    juce::Image  pendingFrame;   // dernière image Live View (ref-comptée, copie triviale)

    juce::ListenerList<Listener> listeners;

    static constexpr int eventPollTimeoutMs = 100;   // réactivité déclenchement <= 100 ms
    static constexpr int reconnectDelayMs   = 2000;  // cadence des tentatives de reconnexion

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CameraController)
};
