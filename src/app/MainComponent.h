#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "StudioLookAndFeel.h"
#include "EngineHost.h"
#include "ProjectDocument.h"
#include "TransportBar.h"
#include "SessionController.h"
#include "ExportDialog.h"
#include "pages/StudioPage.h"
#include "pages/ToolsPage.h"
#include "pages/MixerPage.h"
#include "pages/SettingsPage.h"
#include "pages/MilestonePage.h"
#include "pages/tabs/TabPage.h"
#include "pages/drums/DrumPage.h"
#include "pages/fx/EffectsPage.h"

namespace ff
{
/** Left-hand section navigator. */
class SectionNav : public juce::Component, public juce::SettableTooltipClient
{
public:
    juce::StringArray names;
    juce::Array<bool> implemented;
    int selected = 0;
    std::function<void (int)> onSelect;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    static constexpr int rowHeight = 40;
private:
    int hover = -1;
    int rowAt (int y) const { return (y - 12) / rowHeight; }
};

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    enum Commands
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdSaveAs, cmdReveal, cmdUndo, cmdRedo,
        cmdPlayStop, cmdReturnToZero, cmdToggleClick, cmdTapTempo, cmdPracticeMetronome,
        cmdShortcuts, cmdAbout,
        cmdRecord, cmdLoop, cmdPunch, cmdAddTrack, cmdImport, cmdExport, cmdSplit, cmdDuplicate, cmdDelete,
        cmdCopy, cmdCut, cmdPaste, cmdSelectAll, cmdAddMarker, cmdZoomIn, cmdZoomOut, cmdZoomFit,
        cmdSectionBase = 0x2100 // + section index
    };

    MainComponent (juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs startup prompts (crash recovery, first-run, device errors). */
    void runStartupChecks();
    /** Asks about unsaved changes, then calls `quit` if the user agrees. */
    void requestQuit (std::function<void()> quit);
    void openFromCommandLine (const juce::File&);

    juce::ApplicationCommandManager commandManager;
    std::function<void (const juce::String&)> onTitleChanged;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return juce::JUCEApplication::getInstance(); }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

private:
    juce::PropertiesFile& settings;
    StudioLookAndFeel lnf;
    EngineHost host;
    std::unique_ptr<ProjectDocument> doc;
    std::unique_ptr<SessionController> session;

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<TransportBar> transport;
    SectionNav nav;
    std::unique_ptr<StudioPage> studioPage;
    std::unique_ptr<ToolsPage> toolsPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<SettingsPage> settingsPage;
    std::unique_ptr<TabPage> tabPage;
    std::unique_ptr<DrumPage> drumPage;
    std::unique_ptr<EffectsPage> effectsPage;
    juce::OwnedArray<MilestonePage> milestonePages;
    juce::Array<juce::Component*> pages;
    juce::Label statusLeft, statusRight;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    int currentSection = 0;

    void showSection (int index);
    void updateTitleAndStatus();
    void timerCallback() override;

    void withChangesHandled (std::function<void()> continuation);
    void saveThen (std::function<void()> continuation);
    void saveAsThen (std::function<void()> continuation);
    void openProjectDialog();
    void openProject (const juce::File& bundleOrDocument);
    void showError (const juce::String& title, const juce::String& message);
    void showShortcutEditor();
};
} // namespace ff
