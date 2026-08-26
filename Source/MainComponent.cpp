#include "MainComponent.h"

namespace
{
    juce::String utf8 (const char* text)  { return juce::String (juce::CharPointer_UTF8 (text)); }
}

//==============================================================================
MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);

    titleLabel.setText (utf8 ("Pentax 645Z — Prise de vue connectée"), juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    statusLabel.setText (utf8 ("Déconnecté"), juce::dontSendNotification);
    statusLabel.setFont (juce::FontOptions (16.0f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    triggerButton.setButtonText (utf8 ("DÉCLENCHER"));
    triggerButton.setEnabled (false);
    triggerButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb3261e));
    triggerButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    triggerButton.onClick = [this] { controller.triggerCapture(); };
    addAndMakeVisible (triggerButton);

    folderLabel.setText (controller.getTargetDirectory().getFullPathName(), juce::dontSendNotification);
    folderLabel.setFont (juce::FontOptions (14.0f));
    folderLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    folderLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (folderLabel);

    browseButton.setButtonText (utf8 ("Dossier cible..."));
    browseButton.onClick = [this] { chooseTargetFolder(); };
    addAndMakeVisible (browseButton);

    logView.setMultiLine (true);
    logView.setReadOnly (true);
    logView.setCaretVisible (false);
    logView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (logView);

    appendLog (utf8 ("En attente de l'appareil. Branchez le 645Z en USB (mode PTP) et allumez-le."));

    controller.addListener (this);
    controller.start();

    setSize (640, 480);
}

MainComponent::~MainComponent()
{
    controller.removeListener (this);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // Pastille d'état
    g.setColour (colourForState (displayedState));
    g.fillEllipse (statusLedArea);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawEllipse (statusLedArea, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (20);

    titleLabel.setBounds (area.removeFromTop (36));
    area.removeFromTop (8);

    auto statusRow = area.removeFromTop (28);
    statusLedArea = statusRow.removeFromLeft (28).reduced (6).toFloat();
    statusLabel.setBounds (statusRow);
    area.removeFromTop (16);

    triggerButton.setBounds (area.removeFromTop (72).reduced (40, 0));
    area.removeFromTop (16);

    auto folderRow = area.removeFromTop (32);
    browseButton.setBounds (folderRow.removeFromRight (130));
    folderRow.removeFromRight (8);
    folderLabel.setBounds (folderRow);
    area.removeFromTop (16);

    logView.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        controller.triggerCapture();
        return true;
    }
    return false;
}

//==============================================================================
void MainComponent::cameraStateChanged (CameraController::State newState,
                                        const juce::String& detail)
{
    const bool stateHasChanged = (newState != displayedState);
    displayedState = newState;

    statusLabel.setText (detail.isNotEmpty() ? detail : labelForState (newState),
                         juce::dontSendNotification);

    triggerButton.setEnabled (newState == CameraController::State::ready);

    if (stateHasChanged && detail.isNotEmpty())
        appendLog (detail);

    repaint();
}

void MainComponent::photoSaved (const juce::File& savedFile)
{
    appendLog (utf8 ("Photo enregistrée : ") + savedFile.getFullPathName()
               + " (" + juce::File::descriptionOfSizeInBytes (savedFile.getSize()) + ")");
}

//==============================================================================
void MainComponent::chooseTargetFolder()
{
    folderChooser = std::make_unique<juce::FileChooser> (
        utf8 ("Choisir le dossier de destination"),
        controller.getTargetDirectory());

    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
                                [this] (const juce::FileChooser& fc)
                                {
                                    const auto dir = fc.getResult();
                                    if (dir.isDirectory() || dir != juce::File())
                                    {
                                        controller.setTargetDirectory (dir);
                                        folderLabel.setText (dir.getFullPathName(),
                                                             juce::dontSendNotification);
                                        appendLog (utf8 ("Dossier cible : ") + dir.getFullPathName());
                                    }
                                });
}

void MainComponent::appendLog (const juce::String& line)
{
    const auto time = juce::Time::getCurrentTime().formatted ("[%H:%M:%S] ");
    logView.moveCaretToEnd();
    logView.insertTextAtCaret (time + line + juce::newLine);
}

juce::Colour MainComponent::colourForState (CameraController::State s)
{
    using State = CameraController::State;
    switch (s)
    {
        case State::ready:        return juce::Colour (0xff2e9e4f);   // vert
        case State::capturing:    return juce::Colour (0xffe8a13a);   // orange
        case State::transferring: return juce::Colour (0xff3a7be8);   // bleu
        case State::connecting:   return juce::Colour (0xffb9b93a);   // jaune
        case State::disconnected: break;
    }
    return juce::Colour (0xff9e2e2e);                                 // rouge
}

juce::String MainComponent::labelForState (CameraController::State s)
{
    using State = CameraController::State;
    switch (s)
    {
        case State::ready:        return utf8 ("Prêt");
        case State::capturing:    return utf8 ("Déclenchement en cours...");
        case State::transferring: return utf8 ("Transfert...");
        case State::connecting:   return utf8 ("Connexion...");
        case State::disconnected: break;
    }
    return utf8 ("Déconnecté");
}
