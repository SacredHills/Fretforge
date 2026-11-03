#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "AppInfo.h"

namespace ff
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::PropertiesFile& settings)
        : DocumentWindow (FF_APP_NAME, palette::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* mc = new MainComponent (settings);
        content = mc;
        setContentOwned (mc, true);
        mc->onTitleChanged = [this] (const juce::String& t) { setName (t); };
        setResizable (true, true);
        setResizeLimits (1200, 720, 10000, 10000);

        if (auto state = settings.getValue ("windowState"); state.isNotEmpty())
            restoreWindowStateFromString (state);
        else
            centreWithSize (getWidth(), getHeight());
        setVisible (true);

        juce::Component::SafePointer<MainComponent> safe (mc);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->runStartupChecks(); });
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent* content = nullptr;
};

class Application : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return FF_APP_NAME; }
    const juce::String getApplicationVersion() override { return FF_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        logger.reset (juce::FileLogger::createDateStampedLogger (FF_APP_NAME, "session", ".log",
                                                                 juce::String (FF_APP_NAME) + " " + FF_VERSION_STRING + " starting on "
                                                                     + juce::SystemStats::getOperatingSystemName()));
        juce::Logger::setCurrentLogger (logger.get());

        juce::PropertiesFile::Options o;
        o.applicationName = FF_APP_NAME;
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = FF_APP_NAME;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        o.millisecondsBeforeSaving = 2000;
        properties.setStorageParameters (o);

        window = std::make_unique<MainWindow> (*properties.getUserSettings());
        openArgument (commandLine);
    }

    void shutdown() override
    {
        if (window != nullptr)
            properties.getUserSettings()->setValue ("windowState", window->getWindowStateAsString());
        window.reset();
        properties.closeFiles();
        juce::Logger::writeToLog ("Clean shutdown");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr && window->content != nullptr)
            window->content->requestQuit ([] { juce::JUCEApplication::getInstance()->quit(); });
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override { openArgument (commandLine); }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<MainWindow> window;

    void openArgument (const juce::String& commandLine)
    {
        const auto arg = commandLine.unquoted().trim();
        if (arg.isNotEmpty() && juce::File::isAbsolutePath (arg) && window != nullptr && window->content != nullptr)
            window->content->openFromCommandLine (juce::File (arg));
    }
};
} // namespace ff

START_JUCE_APPLICATION (ff::Application)
