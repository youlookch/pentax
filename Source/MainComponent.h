#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "CameraBackend.h"

//==============================================================================
/**
    Interface de contrôle du tethering :
      - bouton principal "DÉCLENCHER" (raccourci : barre d'espace)
      - aperçu : flux Live View de l'appareil et/ou dernière photo capturée
      - sélecteur de dossier de destination
      - indicateur d'état temps réel (pastille colorée + libellé)
      - journal des transferts

    La liaison avec le moteur est purement événementielle : les méthodes du
    listener sont invoquées par le backend caméra sur le message thread.
*/
class MainComponent : public juce::Component,
                      private CameraListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // CameraListener
    void cameraStateChanged (CameraState newState,
                             const juce::String& detail) override;
    void photoSaved (const juce::File& savedFile) override;
    void liveViewFrameReady (const juce::Image& frame) override;

    void chooseTargetFolder();
    void loadSavedPhotoPreview (const juce::File& file);
    void appendLog (const juce::String& line);
    static juce::Colour colourForState (CameraState);
    static juce::String labelForState (CameraState);

    std::unique_ptr<CameraBackend> controller { CameraBackend::createBest() };

    juce::Label          titleLabel;
    juce::Label          statusLabel;
    juce::TextButton     triggerButton;
    juce::Label          folderLabel;
    juce::TextButton     browseButton;
    juce::TextEditor     logView;
    juce::ImageComponent previewView;
    juce::ToggleButton   liveViewToggle;
    juce::Label          previewCaption;

    std::unique_ptr<juce::FileChooser> folderChooser;

    CameraState displayedState = CameraState::disconnected;
    juce::String lastLoggedDetail;
    juce::Rectangle<float> statusLedArea;
    juce::Rectangle<int>   previewArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
