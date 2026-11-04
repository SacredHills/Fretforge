#include "PluginManager.h"
#include "../StudioLookAndFeel.h"
#include <condition_variable>
#include <queue>

namespace ff
{
// ===================================================================== child process side

class PluginScannerWorker final : private juce::ChildProcessWorker, private juce::AsyncUpdater
{
public:
    PluginScannerWorker() { juce::addDefaultFormatsToManager (formats); }
    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    juce::AudioPluginFormatManager formats;
    std::mutex mutex;
    std::queue<juce::MemoryBlock> pending;

    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty()) return;
        const std::lock_guard<std::mutex> lock (mutex);
        if (auto results = scan (mb); ! results.isEmpty()) send (results);
        else { pending.push (mb); triggerAsyncUpdate(); }   // formats that need the message thread
    }

    void handleConnectionLost() override { juce::JUCEApplicationBase::quit(); }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (pending.empty()) return;
            send (scan (pending.front()));
            pending.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> scan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream in (block, false);
        const auto formatName = in.readString();
        const auto identifier = in.readString();
        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        juce::OwnedArray<juce::PluginDescription> results;
        for (auto* f : formats.getFormats())
            if (f->getName() == formatName
                && (juce::MessageManager::getInstance()->isThisTheMessageThread() || f->requiresUnblockedMessageThreadDuringCreation (pd)))
                f->findAllTypesForFile (results, identifier);
        return results;
    }

    void send (const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml ("LIST");
        for (auto* d : results) xml.addChildElement (d->createXml().release());
        const auto s = xml.toString();
        sendMessageToCoordinator ({ s.toRawUTF8(), s.getNumBytesAsUTF8() });
    }
};

PluginScannerWorker* startPluginScannerWorker (const juce::String& commandLine)
{
    auto w = std::make_unique<PluginScannerWorker>();
    if (w->initialiseFromCommandLine (commandLine, pluginScannerProcessId)) return w.release();
    return nullptr;
}

void destroyPluginScannerWorker (PluginScannerWorker* w) { delete w; }

// ===================================================================== app side: out-of-process scanning

namespace
{
class ScannerProcess final : private juce::ChildProcessCoordinator
{
public:
    ScannerProcess()
    {
        launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), pluginScannerProcessId, 0, 0);
    }
    enum class State { timeout, gotResult, lost };
    std::pair<State, std::unique_ptr<juce::XmlElement>> response()
    {
        std::unique_lock<std::mutex> lock (mutex);
        if (! cv.wait_for (lock, std::chrono::milliseconds (50), [this] { return got || lost; })) return { State::timeout, nullptr };
        const auto st = lost ? State::lost : State::gotResult;
        got = lost = false;
        return { st, std::move (xml) };
    }
    using juce::ChildProcessCoordinator::sendMessageToWorker;
private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        xml = juce::parseXML (mb.toString());
        got = true;
        cv.notify_one();
    }
    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        lost = true;
        cv.notify_one();
    }
    std::mutex mutex;
    std::condition_variable cv;
    std::unique_ptr<juce::XmlElement> xml;
    bool got = false, lost = false;
};

class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        if (process == nullptr) process = std::make_unique<ScannerProcess>();
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream out (block, true);
            out.writeString (format.getName());
            out.writeString (fileOrIdentifier);
        }
        if (! process->sendMessageToWorker (block)) { process = nullptr; return false; }
        const auto started = juce::Time::getMillisecondCounter();
        for (;;)
        {
            if (shouldExit()) return true;
            auto [state, xml] = process->response();
            if (state == ScannerProcess::State::timeout)
            {
                if (juce::Time::getMillisecondCounter() - started > 60000) { process = nullptr; return false; }   // hung plugin
                continue;
            }
            if (xml != nullptr)
                for (auto* item : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*item)) result.add (std::move (d));
                }
            if (state == ScannerProcess::State::lost) { process = nullptr; return false; }   // it crashed: mark as failed
            return true;
        }
    }
    void scanFinished() override { process = nullptr; }
private:
    std::unique_ptr<ScannerProcess> process;
};
}

// ===================================================================== editor windows

struct PluginManager::EditorWindow final : public juce::DocumentWindow
{
    EditorWindow (std::shared_ptr<juce::AudioPluginInstance> p, const juce::String& title, std::function<void()> closed)
        : juce::DocumentWindow (title, palette::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          plugin (std::move (p)), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* ed = nullptr;
        {
            fx::PluginActivity activity (*plugin, "opening its window");
            ed = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
        }
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*plugin);
        editor.reset (ed);
        setContentNonOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    ~EditorWindow() override
    {
        clearContentComponent();
        if (editor != nullptr) plugin->editorBeingDeleted (editor.get());
        editor.reset();
    }
    void closeButtonPressed() override { if (onClosed) juce::MessageManager::callAsync (onClosed); }

    std::shared_ptr<juce::AudioPluginInstance> plugin;   // keeps the plugin alive while its editor is open
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<void()> onClosed;
};

// ===================================================================== manager

PluginManager::PluginManager (juce::PropertiesFile& s) : settings (s)
{
    juce::addDefaultFormatsToManager (formats);
    if (auto xml = settings.getXmlValue ("knownPlugins")) known.recreateFromXml (*xml);
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
    known.addChangeListener (this);
    lastBlacklistSize = known.getBlacklistedFiles().size();
    deadMansPedal = settings.getFile().getSiblingFile ("PluginScanCrashGuard.txt");
    crashReportFile = settings.getFile().getSiblingFile ("LastCrash.txt");
    previousCrash = fx::takePreviousCrashReport (crashReportFile);
    if (previousCrash.pluginName.isNotEmpty())   // don't let it crash again before the user has decided
        heldBack = previousCrash.fileOrIdentifier.isNotEmpty() ? previousCrash.fileOrIdentifier : previousCrash.pluginName;
    fx::installPluginCrashReporter (crashReportFile);
    fx::setPluginHost (this);
}

void PluginManager::reportPreviousCrash (juce::Component* parent)
{
    if (! previousCrash.exists) return;
    const auto r = previousCrash;
    previousCrash = {};
    const auto saved = crashReportFile.getSiblingFile ("LastCrash-previous.txt");
    if (r.pluginName.isEmpty())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("FretForge closed unexpectedly")
                                          .withMessage ("FretForge closed unexpectedly last time. A crash report was saved to:\n" + saved.getFullPathName())
                                          .withButton ("OK").withAssociatedComponent (parent), nullptr);
        return;
    }
    const auto what = r.hang ? "stopped responding while \"" + r.pluginName + "\" was " + r.activity
                             : "closed unexpectedly while \"" + r.pluginName + "\" was " + r.activity;
    std::weak_ptr<bool> token = alive;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Plugin problem")
                                      .withMessage ("Last time FretForge " + what + ".\n\nDisable this plugin? Tracks and rigs that use it keep "
                                                    "their settings but leave it out until you enable it again (Plugins > Options).\n\n"
                                                    "Report: " + saved.getFullPathName())
                                      .withButton ("Disable plugin").withButton ("Keep it").withAssociatedComponent (parent),
                                  [this, token, r] (int result) {
        if (token.expired()) return;
        if (result == 1) disablePlugin (heldBack);
        heldBack.clear();
        if (result != 1 && onPluginsReenabled) onPluginsReenabled();
    });
}

void PluginManager::disablePlugin (const juce::String& fileOrIdentifier)
{
    for (auto& d : known.getTypes())
        if (d.fileOrIdentifier == fileOrIdentifier || d.name == fileOrIdentifier)
        {
            known.removeType (d);
            known.addToBlacklist (d.fileOrIdentifier);
            return;
        }
    known.addToBlacklist (fileOrIdentifier);
}

PluginManager::~PluginManager()
{
    fx::setPluginHost (nullptr);
    fx::shutdownPluginCrashReporter();
    *alive = false;
    closeAllEditors();
    managerWindow.reset();
    known.removeChangeListener (this);
}

void PluginManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = known.createXml()) settings.setValue ("knownPlugins", xml.get());
    const int blacklisted = known.getBlacklistedFiles().size();
    if (blacklisted < lastBlacklistSize && onPluginsReenabled) onPluginsReenabled();   // a plugin was re-enabled
    lastBlacklistSize = blacklisted;
}

void PluginManager::addToMenu (juce::PopupMenu& menu, int) const
{
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : known.getTypes())
        if (! d.isInstrument) effects.add (d);
    juce::KnownPluginList::addToMenu (menu, effects, juce::KnownPluginList::sortByManufacturer);
}

juce::String PluginManager::descriptionForMenuResult (int result, int, juce::String& name) const
{
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : known.getTypes())
        if (! d.isInstrument) effects.add (d);
    const int idx = juce::KnownPluginList::getIndexChosenByMenu (effects, result);
    if (! juce::isPositiveAndBelow (idx, effects.size())) return {};
    name = effects[idx].name;
    return effects[idx].createXml()->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

namespace
{
/** The plugin manager window's content: the folders to scan per format (edited in a normal window, so the folder
    chooser is never opened from behind a modal dialog), and the list of known plugins. */
class ManagerContent final : public juce::Component, private juce::ListBoxModel
{
public:
    ManagerContent (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& kl, const juce::File& pedal, juce::PropertiesFile& props)
        : formats (fm), settings (props), list (fm, kl, pedal, &props, true)
    {
        for (auto* f : formats.getFormats())
            if (f->canScanForPlugins() && f->getDefaultLocationsToSearch().getNumPaths() > 0)
                pathFormats.add (f);

        title.setText ("Plugin folders", juce::dontSendNotification);
        title.setFont (uiFont (14.0f, true));
        addAndMakeVisible (title);
        for (int i = 0; i < pathFormats.size(); ++i) formatBox.addItem (pathFormats[i]->getName(), i + 1);
        formatBox.onChange = [this] { loadPath(); };
        addAndMakeVisible (formatBox);
        folders.setModel (this);
        folders.setRowHeight (22);
        folders.setColour (juce::ListBox::backgroundColourId, palette::well);
        addAndMakeVisible (folders);

        addFolder.onClick = [this] { chooseFolder(); };
        removeFolder.onClick = [this] {
            const int r = folders.getSelectedRow();
            if (r < 0 || r >= path.getNumPaths()) return;
            path.remove (r);
            savePath();
        };
        defaults.onClick = [this] {
            if (auto* f = currentFormat()) { path = f->getDefaultLocationsToSearch(); savePath(); }
        };
        scanButton.onClick = [this] { scanCurrent(); };
        for (auto* b : { &addFolder, &removeFolder, &defaults, &scanButton }) addAndMakeVisible (*b);
        scanButton.setColour (juce::TextButton::buttonColourId, palette::accentDim);

        hint.setText ("Pick a format, add the folders your plugins are installed in, then Scan. "
                      "Plugins that crash while scanning are skipped and marked as failed.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, palette::textDim);
        hint.setFont (uiFont (12.0f));
        addAndMakeVisible (hint);

        list.setOptionsButtonText ("Options...");
        list.getOptionsButton().onClick = [this] { showOptions(); };
        addAndMakeVisible (list);

        if (pathFormats.isEmpty()) { formatBox.setEnabled (false); addFolder.setEnabled (false); scanButton.setEnabled (false); }
        else formatBox.setSelectedId (1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (26);
        title.setBounds (top.removeFromLeft (120));
        formatBox.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        scanButton.setBounds (top.removeFromRight (90));
        top.removeFromRight (6);
        defaults.setBounds (top.removeFromRight (80));
        top.removeFromRight (6);
        removeFolder.setBounds (top.removeFromRight (80));
        top.removeFromRight (6);
        addFolder.setBounds (top.removeFromRight (100));
        r.removeFromTop (6);
        folders.setBounds (r.removeFromTop (110));
        r.removeFromTop (4);
        hint.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        list.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (palette::bg); }

private:
    juce::AudioPluginFormatManager& formats;
    juce::PropertiesFile& settings;
    juce::PluginListComponent list;
    juce::Array<juce::AudioPluginFormat*> pathFormats;
    juce::FileSearchPath path;
    juce::Label title, hint;
    juce::ComboBox formatBox;
    juce::ListBox folders;
    juce::TextButton addFolder { "Add folder..." }, removeFolder { "Remove" }, defaults { "Defaults" }, scanButton { "Scan" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioPluginFormat* currentFormat() const { return pathFormats[formatBox.getSelectedId() - 1]; }

    void loadPath()
    {
        if (auto* f = currentFormat()) path = juce::PluginListComponent::getLastSearchPath (settings, *f);
        folders.updateContent();
        folders.repaint();
    }
    void savePath()
    {
        if (auto* f = currentFormat()) juce::PluginListComponent::setLastSearchPath (settings, *f, path);
        settings.saveIfNeeded();
        loadPath();
    }
    void chooseFolder()
    {
        auto start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        for (int i = 0; i < path.getNumPaths(); ++i)
            if (path[i].isDirectory()) { start = path[i]; break; }
        chooser = std::make_unique<juce::FileChooser> ("Add a plugin folder", start);
        juce::Component::SafePointer<ManagerContent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr) return;
            const auto dir = fc.getResult();
            if (dir == juce::File() || ! dir.isDirectory()) return;
            safe->path.add (dir);
            safe->path.removeRedundantPaths();
            safe->savePath();
        });
    }
    void scanCurrent()
    {
        auto* f = currentFormat();
        if (f == nullptr || list.isScanning()) return;
        auto files = f->searchPathsForPlugins (path, true, false);
        if (files.isEmpty())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Scan").withMessage ("No " + f->getName()
                                              + " plugins were found in these folders.").withButton ("OK").withAssociatedComponent (this), nullptr);
            return;
        }
        list.scanFor (*f, files);
    }
    void showOptions()
    {
        juce::PopupMenu menu;
        const auto original = list.createOptionsMenu();
        for (juce::PopupMenu::MenuItemIterator it (original); it.next();)
        {
            auto& item = it.getItem();
            if (item.text.startsWith ("Scan for new")) continue;   // scanning with folders is done above
            if (item.isSeparator) menu.addSeparator(); else menu.addItem (item);
        }
        for (auto* f : formats.getFormats())   // formats without folders (e.g. Audio Units) scan the system list
            if (f->canScanForPlugins() && ! pathFormats.contains (f))
                menu.addItem ("Scan for " + f->getName() + " plugins", [this, f] { if (! list.isScanning()) list.scanFor (*f); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (list.getOptionsButton()).withDeletionCheck (*this));
    }

    int getNumRows() override { return path.getNumPaths(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) g.fillAll (palette::accentDim);
        g.setColour (path[row].isDirectory() ? palette::text : palette::bad);
        g.setFont (uiFont (13.0f));
        g.drawText (path[row].getFullPathName() + (path[row].isDirectory() ? juce::String() : "   (missing)"),
                    8, 0, w - 16, h, juce::Justification::centredLeft, true);
    }
    void deleteKeyPressed (int) override { removeFolder.triggerClick(); }
};
}

void PluginManager::showManager()
{
    if (managerWindow == nullptr)
    {
        struct Window : juce::DocumentWindow
        {
            Window (juce::Component* c)
                : juce::DocumentWindow ("Plugins (" + juce::String (pluginFormatsLabel()) + ")", palette::bg,
                                        juce::DocumentWindow::closeButton)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (c, true);
                setResizable (true, false);
                setResizeLimits (560, 420, 4000, 3000);
                centreWithSize (860, 640);
            }
            void closeButtonPressed() override { setVisible (false); }
        };
        managerWindow = std::make_unique<Window> (new ManagerContent (formats, known, deadMansPedal, settings));
    }
    managerWindow->setVisible (true);
    managerWindow->toFront (true);
}

void PluginManager::showEditor (std::shared_ptr<juce::AudioPluginInstance> p, const juce::String& title)
{
    if (p == nullptr) return;
    auto* key = p.get();
    if (auto it = editors.find (key); it != editors.end()) { it->second->setVisible (true); it->second->toFront (true); return; }
    std::weak_ptr<bool> token = alive;
    editors[key] = std::make_unique<EditorWindow> (p, title, [this, token, key] { if (! token.expired()) closeEditor (key); });
}

void PluginManager::closeEditor (juce::AudioPluginInstance* p)
{
    editors.erase (p);
}

void PluginManager::closeAllEditors()
{
    editors.clear();
}

void PluginManager::createAsync (const juce::String& descXml, double sr, int bs,
                                 std::function<void (std::unique_ptr<juce::AudioPluginInstance>, juce::String)> done)
{
    juce::PluginDescription d;
    auto xml = juce::parseXML (descXml);
    if (xml == nullptr || ! d.loadFromXml (*xml)) { done (nullptr, "The plugin description is not valid."); return; }
    if (heldBack.isNotEmpty() && (d.fileOrIdentifier == heldBack || d.name == heldBack))
    {
        done (nullptr, "\"" + d.name + "\" was held back because it crashed or froze FretForge last time.");
        return;
    }
    if (known.getBlacklistedFiles().contains (d.fileOrIdentifier))
    {
        done (nullptr, "\"" + d.name + "\" is disabled because it crashed or froze FretForge. Re-enable it under Plugins > Options.");
        return;
    }
    std::weak_ptr<bool> token = alive;
    fx::PluginActivity activity (d.name, d.fileOrIdentifier, "loading");
    formats.createPluginInstanceAsync (d, sr, bs, [done, token, name = d.name, id = d.fileOrIdentifier] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
        if (token.expired()) return;
        fx::registerPluginInstance (inst.get(), name, id);
        const auto message = inst == nullptr && err.isEmpty() ? "\"" + name + "\" could not be loaded (is it still installed?)" : err;
        done (std::move (inst), message);
    });
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createBlocking (const juce::String& descXml, double sr, int bs, juce::String& error)
{
    juce::PluginDescription d;
    auto xml = juce::parseXML (descXml);
    if (xml == nullptr || ! d.loadFromXml (*xml)) { error = "The plugin description is not valid."; return nullptr; }
    if (heldBack.isNotEmpty() && (d.fileOrIdentifier == heldBack || d.name == heldBack)) { error = "\"" + d.name + "\" was held back because it crashed last time."; return nullptr; }
    if (known.getBlacklistedFiles().contains (d.fileOrIdentifier)) { error = "\"" + d.name + "\" is disabled because it crashed or froze FretForge."; return nullptr; }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        fx::PluginActivity activity (d.name, d.fileOrIdentifier, "loading");
        auto inst = formats.createPluginInstance (d, sr, bs, error);
        fx::registerPluginInstance (inst.get(), d.name, d.fileOrIdentifier);
        return inst;
    }

    struct Shared { juce::WaitableEvent done; std::unique_ptr<juce::AudioPluginInstance> inst; juce::String err; };
    auto shared = std::make_shared<Shared>();
    juce::MessageManager::callAsync ([this, d, sr, bs, shared, token = std::weak_ptr<bool> (alive)] {
        if (token.expired()) { shared->done.signal(); return; }
        fx::PluginActivity activity (d.name, d.fileOrIdentifier, "loading");
        formats.createPluginInstanceAsync (d, sr, bs, [shared, d] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            fx::registerPluginInstance (inst.get(), d.name, d.fileOrIdentifier);
            shared->inst = std::move (inst);
            shared->err = err;
            shared->done.signal();
        });
    });
    if (! shared->done.wait (60000)) { error = "Timed out loading \"" + d.name + "\"."; return nullptr; }
    error = shared->err;
    return std::move (shared->inst);
}
} // namespace ff
