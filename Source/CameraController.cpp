#include "CameraController.h"

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
CameraController::CameraController()
    : juce::Thread ("Pentax USB Tether Thread")
{
    context = gp_context_new();

    targetDirectory = juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                          .getChildFile ("Pentax645Z");
    targetDirectory.createDirectory();
}

CameraController::~CameraController()
{
    signalThreadShouldExit();
    notify();
    stopThread (8000);           // laisse le temps de terminer un transfert en cours
    cancelPendingUpdate();

    if (context != nullptr)
        gp_context_unref (context);
}

void CameraController::start()
{
    startThread (juce::Thread::Priority::high);
}

void CameraController::triggerCapture()
{
    if (state.load() == State::ready)
    {
        captureRequested.store (true);
        notify();   // réveille immédiatement le thread USB
    }
}

void CameraController::setTargetDirectory (const juce::File& directory)
{
    const juce::ScopedLock sl (dataLock);
    targetDirectory = directory;
    targetDirectory.createDirectory();
}

juce::File CameraController::getTargetDirectory() const
{
    const juce::ScopedLock sl (dataLock);
    return targetDirectory;
}

//==============================================================================
// Boucle du thread USB : connexion -> écoute d'événements -> capture à la demande
//==============================================================================
void CameraController::run()
{
    while (! threadShouldExit())
    {
        if (camera == nullptr)
        {
            setState (State::connecting, utf8 ("Recherche du Pentax 645Z sur le bus USB..."));

            if (connectCamera())
                continue;

            setState (State::disconnected,
                      utf8 ("Aucun appareil détecté — nouvelle tentative dans 2 s"));
            wait (reconnectDelayMs);   // interrompu instantanément par notify()
            continue;
        }

        if (captureRequested.exchange (false))
            performCaptureAndDownload();
        else
            pollCameraEvents();
    }

    disconnectCamera();
}

//==============================================================================
bool CameraController::connectCamera()
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

    setState (State::ready, utf8 ("Prêt — ") + model);
    return true;
}

void CameraController::disconnectCamera()
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
void CameraController::performCaptureAndDownload()
{
    setState (State::capturing, utf8 ("Déclenchement en cours..."));

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
            setState (State::disconnected, utf8 ("Connexion USB perdue pendant le déclenchement"));
        }
        else
        {
            setState (State::ready, utf8 ("Échec du déclenchement : ") + gpError (ret));
        }
        return;
    }

    setState (State::transferring, utf8 ("Transfert de ") + utf8 (camPath.name) + "...");

    if (downloadFile (camPath.folder, camPath.name, true))
        setState (State::ready, utf8 ("Prêt"));

    // En cas d'échec du transfert, downloadFile a déjà positionné l'état.
}

bool CameraController::downloadFile (const char* cameraFolder, const char* cameraName,
                                     bool deleteAfter)
{
    const juce::File target (makeUniqueTarget (utf8 (cameraName)));

    const int fd = openBinaryForWrite (target);
    if (fd < 0)
    {
        setState (State::ready, utf8 ("Impossible d'écrire dans ") + target.getFullPathName());
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
            setState (State::disconnected, utf8 ("Connexion USB perdue pendant le transfert"));
        }
        else
        {
            setState (State::ready, utf8 ("Échec du transfert : ") + gpError (ret));
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
void CameraController::pollCameraEvents()
{
    CameraEventType eventType = GP_EVENT_UNKNOWN;
    void* eventData = nullptr;

    const int ret = gp_camera_wait_for_event (camera, eventPollTimeoutMs,
                                              &eventType, &eventData, context);

    if (ret < GP_OK)
    {
        disconnectCamera();
        setState (State::disconnected, utf8 ("Connexion USB perdue (câble débranché ?)"));
        return;
    }

    if (eventType == GP_EVENT_FILE_ADDED && eventData != nullptr)
    {
        // Photo déclenchée depuis le boîtier : on la rapatrie aussi.
        const auto* path = static_cast<CameraFilePath*> (eventData);
        setState (State::transferring, utf8 ("Transfert de ") + utf8 (path->name) + "...");

        if (downloadFile (path->folder, path->name, true))
            setState (State::ready, utf8 ("Prêt"));
    }

    if (eventData != nullptr)
        free (eventData);   // libgphoto2 alloue eventData avec malloc
}

//==============================================================================
juce::File CameraController::makeUniqueTarget (const juce::String& cameraName) const
{
    const juce::File dir (getTargetDirectory());
    dir.createDirectory();

    juce::File target (dir.getChildFile (cameraName));
    if (! target.existsAsFile())
        return target;

    return target.getNonexistentSibling();
}

//==============================================================================
// Relais thread USB -> message thread
//==============================================================================
void CameraController::setState (State newState, juce::String detail)
{
    state.store (newState);
    {
        const juce::ScopedLock sl (dataLock);
        pendingDetail = std::move (detail);
    }
    triggerAsyncUpdate();
}

void CameraController::notifyPhotoSaved (const juce::File& file)
{
    {
        const juce::ScopedLock sl (dataLock);
        pendingSavedFiles.add (file);
    }
    triggerAsyncUpdate();
}

void CameraController::handleAsyncUpdate()
{
    juce::String detail;
    juce::Array<juce::File> saved;
    {
        const juce::ScopedLock sl (dataLock);
        detail = pendingDetail;
        saved.swapWith (pendingSavedFiles);
    }

    const State current = state.load();

    listeners.call ([&] (Listener& l) { l.cameraStateChanged (current, detail); });

    for (const auto& f : saved)
        listeners.call ([&] (Listener& l) { l.photoSaved (f); });
}
