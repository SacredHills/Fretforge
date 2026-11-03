#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "core/ProjectFile.h"
#include "EngineHost.h"

namespace ff
{
/** The open project: ValueTree + UndoManager + bundle location, autosave, recent
    files and crash-recovery session lock. Pushes musical settings to the engine. */
class ProjectDocument : private juce::ValueTree::Listener, private juce::Timer
{
public:
    ProjectDocument (EngineHost&, juce::PropertiesFile& settings);
    ~ProjectDocument() override;

    juce::ValueTree& tree() noexcept { return root; }
    juce::UndoManager& undo() noexcept { return undoManager; }
    juce::ValueTree transport() { return root.getChildWithName (project::ids::TRANSPORT); }
    juce::ValueTree metronome() { return root.getChildWithName (project::ids::METRONOME); }

    bool hasLocation() const { return bundle != juce::File(); }
    juce::File getBundle() const { return bundle; }
    /** Folder media is read from / recorded into: the bundle, or a scratch bundle while untitled. */
    juce::File mediaBundle() const;
    juce::ValueTree tracks() { return root.getChildWithName (project::ids::TRACKS); }
    juce::ValueTree markers() { return root.getChildWithName (project::ids::MARKERS); }
    bool isDirty() const { return dirty; }
    juce::String displayName() const;

    /** Re-sends tempo, meter, metronome and loop settings to the engine (after a temporary override). */
    void refreshEngine() { pushAllToEngine(); }

    void newUntitled();
    juce::Result openBundle (const juce::File& bundleDir);
    juce::Result saveTo (const juce::File& bundleDir);   // Save As (also used for first save)
    juce::Result save();                                  // requires hasLocation()

    // ---- typed accessors (undoable edits) ----
    double tempo() { return (double) transport()[project::ids::bpm]; }
    int tsNum() { return (int) transport()[project::ids::tsNum]; }
    int tsDen() { return (int) transport()[project::ids::tsDen]; }
    void setTempo (double bpm);
    void setTimeSignature (int num, int den);
    /** Generic undoable property edit. */
    void set (juce::ValueTree node, const juce::Identifier& id, const juce::var& value, bool undoable = true);

    // ---- recent files ----
    juce::RecentlyOpenedFilesList recent;

    // ---- autosave & recovery ----
    void setAutosaveIntervalSeconds (int seconds);
    int getAutosaveIntervalSeconds() const { return autosaveSeconds; }
    juce::Time lastAutosave() const { return lastAutosaveTime; }

    struct Recovery { juce::File bundle; bool untitled = false; };
    /** Call once at startup: finds an autosave left behind by a crashed session. */
    std::optional<Recovery> findCrashRecovery();
    juce::Result recoverFrom (const Recovery&);
    /** Starts writing the session lock (call after findCrashRecovery()). */
    void beginSession() { sessionStarted = true; writeSessionLock(); }
    /** Clean shutdown: removes the lock and the untitled recovery autosave. */
    void endSession();

    std::function<void()> onStateChanged;  // title/dirty/location changed
    std::function<void()> onTreeReplaced;  // whole project replaced (pages must rebind)

    static juce::File appDataDir();

private:
    EngineHost& host;
    juce::PropertiesFile& settings;
    juce::ValueTree root;
    juce::UndoManager undoManager { 0, 60 };
    juce::File bundle;
    bool dirty = false;
    bool autosaveDirty = false;
    int autosaveSeconds = 120;
    juce::Time lastAutosaveTime;
    bool sessionStarted = false;
    void writeSessionLock();
    void wipeUntitledMedia();
    juce::Result copyMediaTo (const juce::File& newBundle);

    void replaceTree (juce::ValueTree newTree, const juce::File& newBundle, bool markDirty);
    void pushAllToEngine();
    void markDirty();
    void timerCallback() override;
    juce::File untitledRecoveryBundle() const;
    juce::File sessionLockFile() const;
    void addToRecent (const juce::File&);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { markDirty(); }
};
} // namespace ff
