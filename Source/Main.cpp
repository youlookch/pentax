#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

#if JUCE_MAC
 #include <cstdlib>

// Lorsque l'application est distribuée en .dmg, les drivers de libgphoto2
// (camlibs/iolibs) sont embarqués dans le bundle par scripts/make_dmg.sh.
// libgphoto2 les localise via les variables d'environnement CAMLIBS/IOLIBS,
// qui doivent être posées avant toute initialisation de la librairie.
static void useBundledGPhotoDriversIfPresent()
{
    const auto resources = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                               .getChildFile ("Contents/Resources");
    const auto camlibs = resources.getChildFile ("camlibs");
    const auto iolibs  = resources.getChildFile ("iolibs");

    if (camlibs.isDirectory())
        ::setenv ("CAMLIBS", camlibs.getFullPathName().toRawUTF8(), 0);

    if (iolibs.isDirectory())
        ::setenv ("IOLIBS", iolibs.getFullPathName().toRawUTF8(), 0);
}
#endif

//==============================================================================
class PentaxTetherApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override     { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override  { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise (const juce::String&) override
    {
       #if JUCE_MAC
        useBundledGPhotoDriversIfPresent();
       #endif

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (760, 480, 2400, 1600);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (PentaxTetherApplication)
