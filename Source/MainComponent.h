#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "CameraController.h"

//==============================================================================
/**
    Interface de contrôle du tethering :
      - bouton principal "DÉCLENCHER" (raccourci : barre d'espace)
      - sélecteur de dossier de destination
      - indicateur d'état temps réel (pastille colorée + libellé)
      - journal des transferts

    La liaison avec le moteur est purement événementielle : les méthodes du
    listener sont invoquées par CameraController sur le message thread.
*/
class MainComponent : public juce::Component,
                      private CameraController::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // CameraController::Listener
    void cameraStateChanged (CameraController::State newState,
                             const juce::String& detail) override;
    void photoSaved (const juce::File& savedFile) override;

    void chooseTargetFolder();
    void appendLog (const juce::String& line);
    static juce::Colour colourForState (CameraController::State);
    static juce::String labelForState (CameraController::State);

    CameraController controller;

    juce::Label      titleLabel;
    juce::Label      statusLabel;
    juce::TextButton triggerButton;
    juce::Label      folderLabel;
    juce::TextButton browseButton;
    juce::TextEditor logView;

    std::unique_ptr<juce::FileChooser> folderChooser;

    CameraController::State displayedState = CameraController::State::disconnected;
    juce::Rectangle<float> statusLedArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
