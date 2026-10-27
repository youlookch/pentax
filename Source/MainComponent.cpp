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
    triggerButton.onClick = [this] { controller->triggerCapture(); };
    addAndMakeVisible (triggerButton);

    folderLabel.setText (controller->getTargetDirectory().getFullPathName(), juce::dontSendNotification);
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

    previewView.setImagePlacement (juce::RectanglePlacement::centred
                                       | juce::RectanglePlacement::onlyReduceInSize);
    addAndMakeVisible (previewView);

    liveViewToggle.setButtonText (utf8 ("Live View"));
    liveViewToggle.onClick = [this]
    {
        controller->setLiveViewEnabled (liveViewToggle.getToggleState());
        previewCaption.setText (liveViewToggle.getToggleState()
                                    ? utf8 ("Live View")
                                    : utf8 ("Aperçu"),
                                juce::dontSendNotification);
    };
    addAndMakeVisible (liveViewToggle);

    previewCaption.setText (utf8 ("Aperçu"), juce::dontSendNotification);
    previewCaption.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    previewCaption.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (previewCaption);

    appendLog (utf8 ("Moteur caméra : ") + controller->getBackendName());
    appendLog (utf8 ("En attente de l'appareil. Branchez le 645Z en USB (mode PTP) et allumez-le."));

    controller->addListener (this);
    controller->start();

    setSize (1000, 620);
}

MainComponent::~MainComponent()
{
    controller->removeListener (this);
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

    // Fond de la zone d'aperçu
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (previewArea.toFloat(), 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (previewArea.toFloat(), 6.0f, 1.0f);

    if (previewView.getImage().isNull())
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (utf8 ("Aucune image — activez le Live View ou déclenchez une photo"),
                    previewArea, juce::Justification::centred);
    }
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

    // Colonne de gauche : contrôles + journal. Colonne de droite : aperçu.
    auto controls = area.removeFromLeft (juce::jmax (300, area.getWidth() / 3));
    area.removeFromLeft (16);

    triggerButton.setBounds (controls.removeFromTop (72));
    controls.removeFromTop (16);

    auto folderRow = controls.removeFromTop (32);
    browseButton.setBounds (folderRow.removeFromRight (120));
    folderRow.removeFromRight (8);
    folderLabel.setBounds (folderRow);
    controls.removeFromTop (16);

    logView.setBounds (controls);

    auto previewHeader = area.removeFromTop (28);
    liveViewToggle.setBounds (previewHeader.removeFromRight (110));
    previewCaption.setBounds (previewHeader);
    area.removeFromTop (8);

    previewArea = area;
    previewView.setBounds (previewArea.reduced (6));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        controller->triggerCapture();
        return true;
    }
    return false;
}

//==============================================================================
void MainComponent::cameraStateChanged (CameraState newState,
                                        const juce::String& detail)
{
    displayedState = newState;

    statusLabel.setText (detail.isNotEmpty() ? detail : labelForState (newState),
                         juce::dontSendNotification);

    triggerButton.setEnabled (newState == CameraState::ready);

    // Le moteur peut couper le Live View de lui-même (driver sans support) :
    // on garde la case à cocher synchronisée.
    liveViewToggle.setToggleState (controller->isLiveViewEnabled(), juce::dontSendNotification);

    if (detail.isNotEmpty() && detail != lastLoggedDetail)
    {
        appendLog (detail);
        lastLoggedDetail = detail;
    }

    repaint();
}

void MainComponent::photoSaved (const juce::File& savedFile)
{
    appendLog (utf8 ("Photo enregistrée : ") + savedFile.getFullPathName()
               + " (" + juce::File::descriptionOfSizeInBytes (savedFile.getSize()) + ")");

    // Si le Live View tourne, il continue d'alimenter l'aperçu ; sinon on
    // affiche la photo qui vient d'être capturée (si son format est décodable).
    if (! controller->isLiveViewEnabled())
        loadSavedPhotoPreview (savedFile);
}

void MainComponent::liveViewFrameReady (const juce::Image& frame)
{
    previewView.setImage (frame);
}

void MainComponent::loadSavedPhotoPreview (const juce::File& file)
{
    // Le décodage d'un JPEG 51 Mpx prend jusqu'à une seconde : il se fait hors
    // du message thread pour ne pas geler l'UI. Les RAW (PEF/DNG) ne sont pas
    // décodables par JUCE : loadFrom renvoie alors une image invalide et
    // l'aperçu reste inchangé.
    juce::Thread::launch ([safeThis = SafePointer<MainComponent> (this), file]
    {
        auto decoded = juce::ImageFileFormat::loadFrom (file);

        if (decoded.isValid())   // safeThis n'est consulté que sur le message thread
            juce::MessageManager::callAsync ([safeThis, decoded, file]
            {
                if (safeThis != nullptr && ! safeThis->controller->isLiveViewEnabled())
                {
                    safeThis->previewView.setImage (decoded);
                    safeThis->previewCaption.setText (file.getFileName(),
                                                      juce::dontSendNotification);
                }
            });
    });
}

//==============================================================================
void MainComponent::chooseTargetFolder()
{
    folderChooser = std::make_unique<juce::FileChooser> (
        utf8 ("Choisir le dossier de destination"),
        controller->getTargetDirectory());

    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
                                [this] (const juce::FileChooser& fc)
                                {
                                    const auto dir = fc.getResult();
                                    if (dir.isDirectory() || dir != juce::File())
                                    {
                                        controller->setTargetDirectory (dir);
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

juce::Colour MainComponent::colourForState (CameraState s)
{
        switch (s)
    {
        case CameraState::ready:        return juce::Colour (0xff2e9e4f);   // vert
        case CameraState::capturing:    return juce::Colour (0xffe8a13a);   // orange
        case CameraState::transferring: return juce::Colour (0xff3a7be8);   // bleu
        case CameraState::connecting:   return juce::Colour (0xffb9b93a);   // jaune
        case CameraState::disconnected: break;
    }
    return juce::Colour (0xff9e2e2e);                                 // rouge
}

juce::String MainComponent::labelForState (CameraState s)
{
        switch (s)
    {
        case CameraState::ready:        return utf8 ("Prêt");
        case CameraState::capturing:    return utf8 ("Déclenchement en cours...");
        case CameraState::transferring: return utf8 ("Transfert...");
        case CameraState::connecting:   return utf8 ("Connexion...");
        case CameraState::disconnected: break;
    }
    return utf8 ("Déconnecté");
}
