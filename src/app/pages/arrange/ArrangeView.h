#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ProjectDocument.h"
#include "../../EngineHost.h"
#include "../../SessionController.h"
#include "../../Widgets.h"
#include "core/Timeline.h"

namespace ff
{
class ArrangeView;

/** Shared view geometry for the arrange window (ruler, lanes, headers). */
struct ArrangeGeometry
{
    double pixelsPerSecond = 60.0;
    double scrollX = 0.0;        // seconds at the left edge of the lanes
    int scrollY = 0;             // pixels
    int trackHeight = 78;
    double snapBeats = 1.0;      // grid in quarter notes; 0 = off
    double bpm = 120.0;
    int tsNum = 4, tsDen = 4;

    float xFor (double t) const { return (float) ((t - scrollX) * pixelsPerSecond); }
    double timeFor (float x) const { return scrollX + x / pixelsPerSecond; }
    double quarterSec() const { return 60.0 / bpm; }
    double barSec() const { return tsNum * 4.0 / tsDen * quarterSec(); }
    double gridSec() const { return snapBeats * quarterSec(); }
    double snap (double t, bool enabled = true) const
    {
        if (! enabled || snapBeats <= 0.0) return juce::jmax (0.0, t);
        const double g = gridSec();
        return juce::jmax (0.0, std::round (t / g) * g);
    }
};

class TrackHeader : public juce::Component, private juce::ValueTree::Listener
{
public:
    TrackHeader (ArrangeView&, juce::ValueTree track, int index);
    ~TrackHeader() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tickMeter();
    void refreshInputs();
    juce::ValueTree track;
    int index;

private:
    ArrangeView& owner;
    juce::Label name;
    juce::TextButton arm { "R" }, mon { "I" }, mute { "M" }, solo { "S" };
    juce::ComboBox input;
    juce::TextButton route;
    juce::Slider gain, pan;
    juce::Array<std::pair<int, bool>> inputValues;
    std::vector<std::pair<juce::ValueTree, double>> groupStart; // other selected tracks' values at drag start
    void beginGroupDrag (const juce::Identifier& id);
    void applyGroupDelta (const juce::Identifier& id, double delta, double lo, double hi);
    float meterLevel = -100.0f;
    bool updating = false, draggingTrack = false;
    void refresh();
    void showMenu();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
};

class TimelineCanvas : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TimelineCanvas (ArrangeView& v) : owner (v) { setWantsKeyboardFocus (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override { dropX = x; dropY = y; repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { dropX = x; dropY = y; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropX = -1; repaint(); }

private:
    ArrangeView& owner;
    enum class Mode { none, move, trimLeft, trimRight, fadeIn, fadeOut, marquee };
    juce::Rectangle<int> marqueeRect;
    float sliceX = -1.0f;   // slice tool: where the cut would go (hover)
    void mouseExit (const juce::MouseEvent&) override { sliceX = -1.0f; repaint(); }
    bool keyPressed (const juce::KeyPress& k) override;
    struct Orig { juce::ValueTree clip; double start, length, offset, fadeIn, fadeOut; int track; };
    Mode mode = Mode::none;
    std::vector<Orig> originals;
    juce::ValueTree anchor;
    int trackDelta = 0;
    bool dragged = false;
    int dropX = -1, dropY = -1;

    Mode modeAt (const juce::ValueTree& clip, juce::Point<float> p) const;
    void drawClip (juce::Graphics&, const juce::ValueTree& clip, juce::Rectangle<float> r, juce::Colour colour, bool selected);
    void showClipMenu (juce::ValueTree clip);
    void showLaneMenu (int trackIndex, double time);
};

class Ruler : public juce::Component
{
public:
    explicit Ruler (ArrangeView& v) : owner (v) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    static constexpr int markerLane = 18;
private:
    ArrangeView& owner;
    double dragStart = -1.0;
    juce::ValueTree markerAt (float x) const;
};

/** The Studio arrange window: toolbar, ruler, track headers and the clip timeline. */
class ArrangeView : public juce::Component, private juce::Timer, private juce::ValueTree::Listener, private juce::ScrollBar::Listener,
                    private juce::ChangeListener
{
public:
    ArrangeView (ProjectDocument&, EngineHost&, SessionController&, juce::PropertiesFile&);
    ~ArrangeView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void rebind();
    void deviceChanged();

    // ---- editing commands ----
    void addTrack();
    void importAudioDialog();
    void deleteSelection();
    void splitAtPlayhead();
    void duplicateSelection();
    void copySelection (bool cut);
    void paste();
    void selectAll();
    void addMarkerAtPlayhead();
    void zoom (double factor, float aroundX = -1.0f);
    void zoomToFit();
    bool hasSelection() const { return ! selection.isEmpty(); }
    /** Reorders: moves `tracks` as a block so they start at `insertIndex` (0..numTracks). */
    void moveTracks (juce::Array<juce::ValueTree> tracks, int insertIndex);
    void setTrackDropIndex (int index);
    int trackDropIndex() const { return dropIndex; }
    void paintOverChildren (juce::Graphics&) override;

    // ---- shared with children ----
    ProjectDocument& doc;
    EngineHost& host;
    SessionController& session;
    juce::PropertiesFile& settings;
    ArrangeGeometry geo;
    juce::Array<juce::ValueTree> selection;

    juce::ValueTree trackAtIndex (int i) { return doc.tracks().getChild (i); }
    int trackIndexAtY (int canvasY) const;
    int numTracks() { return doc.tracks().getNumChildren(); }
    juce::Colour trackColour (const juce::ValueTree& t) const;
    void select (const juce::ValueTree& clip, bool add);
    /** Clips on the other tracks of a clip's track group that overlap it (they are edited with it; Alt: none). */
    juce::Array<juce::ValueTree> groupPartners (const juce::ValueTree& clip) const;
    void clearSelection() { selection.clear(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void locate (double seconds) { host.engine.locateSeconds (seconds); }
    double playhead() const { return host.engine.getPositionSeconds(); }
    void updateScrollBars();
    void repaintAll();
    void showError (const juce::String& title, const juce::String& message);
    void renameDialog (const juce::String& title, const juce::String& current, std::function<void (juce::String)> onDone);
    juce::Rectangle<int> lanesArea() const { return canvas.getBounds(); }

private:
    Ruler ruler { *this };
    TimelineCanvas canvas { *this };
    juce::Component headerHolder;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar hScroll { false }, vScroll { true };
    juce::TextButton addTrackButton { "+ Track" }, importButton { "Import" }, zoomInButton { "+" }, zoomOutButton { "-" },
        fitButton { "Fit" }, followButton { "Follow" }, sliceButton { "Slice" };
public:
    bool sliceMode() const { return sliceButton.getToggleState(); }
    void setSliceMode (bool on) { sliceButton.setToggleState (on, juce::sendNotificationSync); }
    /** Splits the clips at time t (seconds). */
    void splitClipsAt (const juce::Array<juce::ValueTree>& clips, double t);
private:
    juce::ComboBox snapCombo;
    juce::Label snapLabel;
    juce::ValueTree root;
    std::unique_ptr<juce::FileChooser> chooser;
    struct ClipboardItem { juce::ValueTree clip; int trackOffset; double timeOffset; };
    std::vector<ClipboardItem> clipboard;
    bool rebuildPending = false;
    int dropIndex = -1;

    void rebuildHeaders();
    void layoutHeaders();
    void syncGeometry();
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override { rebuildHeaders(); }

    static constexpr int headerWidth = 250;
    static constexpr int rulerHeight = 46;
    static constexpr int toolbarHeight = 38;
};
} // namespace ff
