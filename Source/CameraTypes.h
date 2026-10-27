#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

//==============================================================================
/** États de la session caméra, communs à tous les backends. */
enum class CameraState
{
    disconnected,   // Déconnecté : aucun appareil, reconnexion en cours
    connecting,     // Tentative d'ouverture de session
    ready,          // Prêt : session ouverte, déclenchement possible
    capturing,      // Déclenchement en cours (obturateur)
    transferring    // Transfert du fichier vers le disque
};

//==============================================================================
/** Interface d'écoute UI. Toutes les méthodes sont appelées sur le message thread. */
struct CameraListener
{
    virtual ~CameraListener() = default;

    virtual void cameraStateChanged (CameraState newState, const juce::String& detail) = 0;
    virtual void photoSaved (const juce::File& savedFile) = 0;
    virtual void liveViewFrameReady (const juce::Image&) {}
};
