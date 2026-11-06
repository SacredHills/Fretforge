#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../ProjectDocument.h"
#include "../../EngineHost.h"
#include "../../StudioLookAndFeel.h"
#include "core/MidiClip.h"

namespace ff
{
/** MIDI editor for one MIDI clip: piano keyboard, note grid on the project's bars and beats, velocity lane.

    Mouse: drag notes to move them (Alt: no snap), drag a note's right edge to change its length, drag on empty grid to
    select with a box, double-click empty grid to add a note, double-click a note to delete it. With Draw on, a single
    click adds a note and dragging sets its length. Keys: Delete, Ctrl+A, arrows (Shift+Up / Down: octave), Ctrl+Z / Y,
    Q quantise. The keyboard plays the track's instrument. Every edit is one undo step in the project. */
class PianoRoll : public juce::Component, private juce::Timer, private juce::ValueTree::Listener, private juce::ScrollBar::Listener
{
public:
    PianoRoll (ProjectDocument&, EngineHost&, juce::ValueTree clip);
    ~PianoRoll() override;

    juce::ValueTree getClip() const { return clip; }
    std::function<void()> onClipGone;   // the clip was deleted

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    ProjectDocument& doc;
    EngineHost& host;
    juce::ValueTree clip;
    std::vector<midiclip::Note> notes;
    std::vector<midiclip::Pedal> pedal;
    std::vector<char> selected;

    // view
    double pps = 160.0;         // pixels per second
    double scrollT = 0.0;       // seconds (timeline) at the grid's left edge
    float keyH = 12.0f;         // pixels per semitone
    float topPitch = 84.0f;     // pitch at the top of the grid
    static constexpr int toolbarH = 36, rulerH = 20, keysW = 56, velH = 72, sbW = 12;
    juce::ScrollBar hbar { false }, vbar { true };
    double lastPlayhead = -1.0;
    int flashTicks = 0;              // info shows a message (e.g. after Quantize) for a moment
    int velTip = -1;                 // velocity drag: value shown by the cursor
    juce::Point<float> velTipPos;

    juce::TextButton drawButton { "Draw" }, quantizeButton { "Quantize" }, zoomIn { "+" }, zoomOut { "-" }, fitButton { "Fit" };
    juce::ComboBox gridBox, lengthBox;
    juce::Label gridLabel, lengthLabel, info;

    // gesture
    enum class Drag { none, move, resize, marquee, velocity, draw, keyboard, scrub } drag = Drag::none;
    struct Orig { double start, length; int pitch, velocity; };
    std::vector<Orig> origs;
    int anchor = -1;
    juce::Point<float> downPos;
    juce::Rectangle<float> marquee;
    int keyboardNote = -1;
    bool changed = false, writing = false, fitted = false;

    // geometry
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velArea() const;
    juce::Rectangle<int> keysArea() const;
    juce::Rectangle<int> rulerArea() const;
    void updateScrollBars();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void flash (const juce::String&);
    float xFor (double t) const;
    double timeFor (float x) const;
    float yFor (int pitch) const;        // top of the pitch row
    int pitchFor (float y) const;
    double clipStart() const;
    double clipEnd() const;
    double offset() const;
    double absStart (const midiclip::Note& n) const { return clipStart() + n.start - offset(); }
    juce::Rectangle<float> noteRect (const midiclip::Note&) const;
    int noteAt (juce::Point<float>, bool& onRightEdge) const;
    double quarterSec() const;
    double gridSec() const;
    double snap (double t, bool on) const;
    double newNoteLength() const;

    void load();
    void commit (const juce::String& undoName = {});   // writes the notes (one undo step per gesture)
    void beginEdit (const juce::String& name);
    void audition (int pitch, int velocity);
    void stopAudition();
    int trackIndex() const;
    void quantize();
    void fit();
    void deleteSelected();
    void transpose (int semitones);
    void nudge (int steps);
    int numSelected() const;
    void updateInfo();

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { if (! writing) load(); }
    void valueTreeParentChanged (juce::ValueTree&) override;
};
} // namespace ff
