#include "GPhoto2Backend.h"

#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2-context.h>

#include <fcntl.h>
#if defined (_WIN32)
 #include <io.h>
#else
 #include <unistd.h>
#endif

namespace
{
    juce::String utf8 (const char* text)  { return juce::String (juce::CharPointer_UTF8 (text)); }

    juce::String gpError (int code)       { return utf8 (gp_result_as_string (code)); }

    // Ouvre un descripteur de fichier en écriture binaire, cross-platform.
    int openBinaryForWrite (const juce::File& file)
    {
       #if defined (_WIN32)
        return ::_open (file.getFullPathName().toRawUTF8(),
                        _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY,
                        _S_IREAD | _S_IWRITE);
       #else
        return ::open (file.getFullPathName().toRawUTF8(),
                       O_CREAT | O_WRONLY | O_TRUNC, 0644);
       #endif
    }
}

//==============================================================================
GPhoto2Backend::GPhoto2Backend()
    : juce::Thread ("Pentax USB Tether Thread")
{
    context = gp_context_new();
}

GPhoto2Backend::~GPhoto2Backend()
{
    signalThreadShouldExit();
    notify();
    stopThread (8000);           // laisse le temps de terminer un transfert en cours

    if (context != nullptr)
        gp_context_unref (context);
}

void GPhoto2Backend::start()
{
    startThread (juce::Thread::Priority::high);
}

void GPhoto2Backend::triggerCapture()
{
    if (getState() == CameraState::ready)
    {
        captureRequested.store (true);
        notify();   // réveille immédiatement le thread USB
    }
}

void GPhoto2Backend::setLiveViewEnabled (bool shouldBeEnabled)
{
    liveViewOn.store (shouldBeEnabled);
    notify();   // démarre/arrête le flux sans attendre le prochain timeout
}

//==============================================================================
// Boucle du thread USB : connexion -> écoute d'événements -> capture à la demande
//==============================================================================
void GPhoto2Backend::run()
{
    while (! threadShouldExit())
    {
        if (camera == nullptr)
        {
            setState (CameraState::connecting, utf8 ("Recherche du Pentax 645Z sur le bus USB..."));

            if (connectCamera())
                continue;

            setState (CameraState::disconnected,
                      utf8 ("Aucun appareil détecté — nouvelle tentative dans 2 s"));
            wait (reconnectDelayMs);   // interrompu instantanément par notify()
            continue;
        }

        if (captureRequested.exchange (false))
        {
            performCaptureAndDownload();
        }
        else if (liveViewOn.load())
        {
            // gp_camera_capture_preview bloque le temps d'une trame : elle
            // cadence naturellement la boucle. Le poll d'événements très court
            // qui suit capte quand même FILE_ADDED / la déconnexion.
            grabLiveViewFrame();

            if (camera != nullptr && liveViewOn.load())
                pollCameraEvents (1);
        }
        else
        {
            pollCameraEvents (eventPollTimeoutMs);
        }
    }

    disconnectCamera();
}

//==============================================================================
bool GPhoto2Backend::connectCamera()
{
    gp_camera_new (&camera);

    // gp_camera_init détecte le premier appareil PTP disponible et ouvre la
    // session de contrôle (le 645Z doit être en mode USB "PTP", pas "MSC").
    const int ret = gp_camera_init (camera, context);

    if (ret < GP_OK)
    {
        gp_camera_unref (camera);
        camera = nullptr;
        return false;
    }

    juce::String model ("Appareil PTP");
    CameraAbilities abilities;
    if (gp_camera_get_abilities (camera, &abilities) >= GP_OK)
        model = utf8 (abilities.model);

    setState (CameraState::ready, utf8 ("Prêt — ") + model);
    return true;
}

void GPhoto2Backend::disconnectCamera()
{
    if (camera != nullptr)
    {
        gp_camera_exit (camera, context);
        gp_camera_unref (camera);
        camera = nullptr;
    }
}

//==============================================================================
// Déclenchement distant + rapatriement immédiat
//==============================================================================
void GPhoto2Backend::performCaptureAndDownload()
{
    setState (CameraState::capturing, utf8 ("Déclenchement en cours..."));

    // Envoie l'opération PTP InitiateCapture : l'obturateur part immédiatement,
    // sans passer par la carte SD. La fonction rend la main dès que l'appareil
    // annonce le fichier produit dans son buffer interne.
    CameraFilePath camPath;
    const int ret = gp_camera_capture (camera, GP_CAPTURE_IMAGE, &camPath, context);

    if (ret < GP_OK)
    {
        if (ret == GP_ERROR_IO || ret == GP_ERROR_IO_USB_FIND || ret == GP_ERROR_TIMEOUT)
        {
            disconnectCamera();
            setState (CameraState::disconnected, utf8 ("Connexion USB perdue pendant le déclenchement"));
        }
        else
        {
            setState (CameraState::ready, utf8 ("Échec du déclenchement : ") + gpError (ret));
        }
        return;
    }

    setState (CameraState::transferring, utf8 ("Transfert de ") + utf8 (camPath.name) + "...");

    if (downloadFile (camPath.folder, camPath.name, true))
        setState (CameraState::ready, utf8 ("Prêt"));

    // En cas d'échec du transfert, downloadFile a déjà positionné l'état.
}

bool GPhoto2Backend::downloadFile (const char* cameraFolder, const char* cameraName,
                                   bool deleteAfter)
{
    const juce::File target (makeUniqueTarget (utf8 (cameraName)));

    const int fd = openBinaryForWrite (target);
    if (fd < 0)
    {
        setState (CameraState::ready, utf8 ("Impossible d'écrire dans ") + target.getFullPathName());
        return false;
    }

    // gp_file_new_from_fd branche la CameraFile directement sur le descripteur :
    // les blocs USB sont écrits sur le disque au fil de l'eau, sans tampon
    // intermédiaire de 50 Mo en RAM. La CameraFile prend possession du fd
    // et le ferme lors de gp_file_free.
    CameraFile* file = nullptr;
    gp_file_new_from_fd (&file, fd);

    const int ret = gp_camera_file_get (camera, cameraFolder, cameraName,
                                        GP_FILE_TYPE_NORMAL, file, context);
    gp_file_free (file);

    if (ret < GP_OK)
    {
        target.deleteFile();

        if (ret == GP_ERROR_IO || ret == GP_ERROR_IO_USB_FIND || ret == GP_ERROR_TIMEOUT)
        {
            disconnectCamera();
            setState (CameraState::disconnected, utf8 ("Connexion USB perdue pendant le transfert"));
        }
        else
        {
            setState (CameraState::ready, utf8 ("Échec du transfert : ") + gpError (ret));
        }
        return false;
    }

    if (deleteAfter)
        gp_camera_file_delete (camera, cameraFolder, cameraName, context);   // libère le buffer

    notifyPhotoSaved (target);
    return true;
}

//==============================================================================
// Entre deux captures : surveillance de la session (déconnexion, photos prises
// depuis le boîtier). Le timeout court garantit une latence de déclenchement
// bornée à eventPollTimeoutMs.
//==============================================================================
void GPhoto2Backend::pollCameraEvents (int timeoutMs)
{
    CameraEventType eventType = GP_EVENT_UNKNOWN;
    void* eventData = nullptr;

    const int ret = gp_camera_wait_for_event (camera, timeoutMs,
                                              &eventType, &eventData, context);

    if (ret < GP_OK)
    {
        disconnectCamera();
        setState (CameraState::disconnected, utf8 ("Connexion USB perdue (câble débranché ?)"));
        return;
    }

    if (eventType == GP_EVENT_FILE_ADDED && eventData != nullptr)
    {
        // Photo déclenchée depuis le boîtier : on la rapatrie aussi.
        const auto* path = static_cast<CameraFilePath*> (eventData);
        setState (CameraState::transferring, utf8 ("Transfert de ") + utf8 (path->name) + "...");

        if (downloadFile (path->folder, path->name, true))
            setState (CameraState::ready, utf8 ("Prêt"));
    }

    if (eventData != nullptr)
        free (eventData);   // libgphoto2 alloue eventData avec malloc
}

//==============================================================================
// Live View : récupère une trame d'aperçu (JPEG basse résolution) depuis
// l'appareil et la décode pour l'UI.
//==============================================================================
void GPhoto2Backend::grabLiveViewFrame()
{
    CameraFile* file = nullptr;
    gp_file_new (&file);

    const int ret = gp_camera_capture_preview (camera, file, context);

    if (ret < GP_OK)
    {
        gp_file_free (file);

        if (ret == GP_ERROR_IO || ret == GP_ERROR_IO_USB_FIND || ret == GP_ERROR_TIMEOUT)
        {
            // Le flux reprendra automatiquement à la reconnexion (le drapeau
            // liveViewOn reste levé).
            disconnectCamera();
            setState (CameraState::disconnected, utf8 ("Connexion USB perdue (câble débranché ?)"));
        }
        else
        {
            liveViewOn.store (false);
            setState (CameraState::ready, utf8 ("Live View indisponible sur cet appareil : ")
                                              + gpError (ret));
        }
        return;
    }

    const char* data = nullptr;
    unsigned long size = 0;
    gp_file_get_data_and_size (file, &data, &size);

    juce::Image frame;
    if (data != nullptr && size > 0)
        frame = juce::ImageFileFormat::loadFrom (data, (size_t) size);

    gp_file_free (file);

    if (frame.isValid())
        notifyLiveViewFrame (frame);
}
