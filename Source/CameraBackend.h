#pragma once

#include <juce_events/juce_events.h>
#include "CameraTypes.h"

//==============================================================================
/**
    Interface commune des moteurs de tethering.

    Deux implémentations :
      - RicohSdkBackend  : le SDK USB officiel de Ricoh ("RICOH Camera USB SDK"),
        c'est-à-dire la même couche de communication PTP propriétaire
        qu'Image Transmitter 2 (645Z, K-1, K-1 II, KP). Compilé si CMake est
        configuré avec -DRICOH_CAMERA_SDK_ROOT=<chemin du SDK>.
      - GPhoto2Backend   : libgphoto2 (PTP générique), utilisé sinon.
*/
class CameraBackend
{
public:
    virtual ~CameraBackend() = default;

    virtual void start() = 0;
    virtual void triggerCapture() = 0;

    virtual void setLiveViewEnabled (bool shouldBeEnabled) = 0;
    virtual bool isLiveViewEnabled() const = 0;

    virtual void setTargetDirectory (const juce::File&) = 0;
    virtual juce::File getTargetDirectory() const = 0;

    virtual CameraState getState() const = 0;

    virtual void addListener (CameraListener*) = 0;
    virtual void removeListener (CameraListener*) = 0;

    virtual juce::String getBackendName() const = 0;

    /** Fabrique : backend SDK Ricoh officiel si compilé avec, sinon libgphoto2. */
    static std::unique_ptr<CameraBackend> createBest();
};

//==============================================================================
/**
    Socle commun des backends : état atomique, dossier cible, et relais
    thread-safe des notifications vers le message thread via AsyncUpdater.

    Les sous-classes appellent setState / notifyPhotoSaved /
    notifyLiveViewFrame depuis n'importe quel thread.
*/
class NotifyingCameraBackend : public CameraBackend,
                               private juce::AsyncUpdater
{
public:
    void setTargetDirectory (const juce::File& directory) override
    {
        const juce::ScopedLock sl (dataLock);
        targetDirectory = directory;
        targetDirectory.createDirectory();
    }

    juce::File getTargetDirectory() const override
    {
        const juce::ScopedLock sl (dataLock);
        return targetDirectory;
    }

    CameraState getState() const override           { return state.load(); }
    void addListener (CameraListener* l) override    { listeners.add (l); }
    void removeListener (CameraListener* l) override { listeners.remove (l); }

protected:
    NotifyingCameraBackend()
    {
        targetDirectory = juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                              .getChildFile ("Pentax645Z");
        targetDirectory.createDirectory();
    }

    ~NotifyingCameraBackend() override
    {
        cancelPendingUpdate();
    }

    void setState (CameraState newState, juce::String detail = {})
    {
        state.store (newState);
        {
            const juce::ScopedLock sl (dataLock);
            pendingDetail = std::move (detail);
        }
        triggerAsyncUpdate();
    }

    void notifyPhotoSaved (const juce::File& file)
    {
        {
            const juce::ScopedLock sl (dataLock);
            pendingSavedFiles.add (file);
        }
        triggerAsyncUpdate();
    }

    void notifyLiveViewFrame (const juce::Image& frame)
    {
        {
            const juce::ScopedLock sl (dataLock);
            pendingFrame = frame;
        }
        triggerAsyncUpdate();
    }

    /** Dédoublonne un nom de fichier dans le dossier cible. */
    juce::File makeUniqueTarget (const juce::String& cameraName) const
    {
        const juce::File dir (getTargetDirectory());
        dir.createDirectory();

        juce::File target (dir.getChildFile (cameraName));
        if (! target.existsAsFile())
            return target;

        return target.getNonexistentSibling();
    }

    std::atomic<CameraState> state { CameraState::disconnected };

private:
    void handleAsyncUpdate() override
    {
        juce::String detail;
        juce::Array<juce::File> saved;
        juce::Image frame;
        {
            const juce::ScopedLock sl (dataLock);
            detail = pendingDetail;
            saved.swapWith (pendingSavedFiles);
            frame = pendingFrame;
            pendingFrame = {};
        }

        const CameraState current = state.load();

        listeners.call ([&] (CameraListener& l) { l.cameraStateChanged (current, detail); });

        if (frame.isValid())
            listeners.call ([&] (CameraListener& l) { l.liveViewFrameReady (frame); });

        for (const auto& f : saved)
            listeners.call ([&] (CameraListener& l) { l.photoSaved (f); });
    }

    mutable juce::CriticalSection dataLock;
    juce::File   targetDirectory;
    juce::String pendingDetail;
    juce::Array<juce::File> pendingSavedFiles;
    juce::Image  pendingFrame;

    juce::ListenerList<CameraListener> listeners;
};
