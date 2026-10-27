#pragma once

#include "CameraBackend.h"

// Types opaques de libgphoto2 (évite d'exposer les en-têtes C dans toute l'app)
typedef struct _Camera    Camera;
typedef struct _GPContext GPContext;

//==============================================================================
/**
    Backend libgphoto2 (PTP générique) : utilisé quand l'application est
    compilée sans le SDK officiel Ricoh.

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
*/
class GPhoto2Backend : public NotifyingCameraBackend,
                       private juce::Thread
{
public:
    GPhoto2Backend();
    ~GPhoto2Backend() override;

    void start() override;
    void triggerCapture() override;

    void setLiveViewEnabled (bool shouldBeEnabled) override;
    bool isLiveViewEnabled() const override   { return liveViewOn.load(); }

    juce::String getBackendName() const override
    {
        return "libgphoto2 (PTP g\xc3\xa9n\xc3\xa9rique)";
    }

private:
    //==========================================================================
    void run() override;                 // boucle du thread USB

    bool connectCamera();
    void disconnectCamera();
    void performCaptureAndDownload();
    bool downloadFile (const char* cameraFolder, const char* cameraName, bool deleteAfter);
    void pollCameraEvents (int timeoutMs);
    void grabLiveViewFrame();

    //==========================================================================
    GPContext* context = nullptr;
    Camera*    camera  = nullptr;   // accédé uniquement par le thread USB

    std::atomic<bool> captureRequested { false };
    std::atomic<bool> liveViewOn { false };

    static constexpr int eventPollTimeoutMs = 100;   // réactivité déclenchement <= 100 ms
    static constexpr int reconnectDelayMs   = 2000;  // cadence des tentatives de reconnexion

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GPhoto2Backend)
};
