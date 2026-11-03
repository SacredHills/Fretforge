#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ProjectDocument.h"
#include "../../EngineHost.h"
#include "../../Widgets.h"
#include "core/TabModel.h"

namespace ff
{
/** Tablature view and editor for one track of the score (Guitar Pro style):
    systems of measures with fret numbers on string lines, rhythm stems/beams below,
    technique markings, a note cursor, beat selection and a playback cursor.

    Keyboard: digits enter frets (two quick digits = two-digit fret), arrows move,
    Ctrl+arrows change bar/track, Shift+arrows select, +/- change duration, '.' dot,
    T triplet, R rest, Ins insert beat, Del delete note, Backspace delete beat,
    H hammer/pull, S slide, B bend, V vibrato, P palm mute, X dead, G ghost,
    N natural / A artificial harmonic, L let ring, Shift+T tap, Z tie, > accent,
    ',' staccato, D/U pick stroke, W tremolo picking, Ctrl+C/X/V copy/cut/paste beats. */
class TabEditor : public juce::Component
{
public:
    TabEditor (ProjectDocument&, EngineHost&);

    void setScore (juce::ValueTree score);
    void setTrackIndex (int index);
    int getTrackIndex() const { return trackIndex; }
    juce::ValueTree track() const;

    // cursor & selection
    struct Pos { int bar = 0, beat = 0; bool operator< (const Pos& o) const { return bar < o.bar || (bar == o.bar && beat < o.beat); } };
    Pos cursor;
    int cursorString = 0;
    bool hasSelection() const { return selActive; }
    std::pair<Pos, Pos> selectionRange() const; // inclusive, ordered
    juce::Array<juce::ValueTree> selectedBeats() const;
    /** Quarter-note span of the selection (or the cursor's bar). */
    std::pair<double, double> selectionSpanQ() const;
    double cursorQ() const;

    // entry state (shown in the palette)
    int entryDur = 8, entryDots = 0, entryTuplet = 0;
    std::function<void()> onStateChanged; // cursor / entry state moved

    // editing operations (also used by the palette buttons)
    void setDuration (int dur);
    void toggleDots();
    void toggleTuplet();
    void toggleRest();
    void toggleNoteFlag (const juce::Identifier& id, const juce::String& undoName);
    void cycleBend();
    void cycleSlide();
    void cycleHarmonic (int kind);
    void setStroke (int stroke);
    void toggleBeatFlag (const juce::Identifier& id, const juce::String& undoName);
    void insertBeat();
    void deleteNote();
    void deleteBeat();
    void copy (bool cut);
    void paste();
    void transpose (int semitones);
    void insertMeasure (bool after);
    void deleteMeasure();
    void enterFret (int fret);
    /** MIDI keyboard entry: writes the pitches (a chord) into the cursor's beat with automatically chosen
        strings; with `advance` the cursor first moves to the next beat. */
    void enterPitches (const std::vector<int>& midiNotes, bool advance);

    void setPlayheadQ (double q) { playQ = q; repaint(); }
    void layoutChanged();

    /** Standard notation above the tab (stored in the score, default on). */
    bool notationVisible() const;
    /** Print styling: black on white, no cursor, selection or bar-fill warnings (PDF export). */
    void setPrintMode (bool shouldPrint);
    struct Row { int y = 0, height = 0; };
    const std::vector<Row>& rows() const { return rowInfo; }
    /** Beat under a point (for double-click editing from the page). */
    bool beatAt (juce::Point<int> p, Pos& pos) const { int s; return locate (p, pos, s); }
    std::function<void()> onDoubleClick;

    void paint (juce::Graphics&) override;
    void resized() override { layoutChanged(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    /** Keeps the cursor visible inside an enclosing viewport. */
    juce::Rectangle<int> cursorBounds() const;

private:
    ProjectDocument& doc;
    EngineHost& host;
    juce::ValueTree score;
    int trackIndex = 0;
    bool selActive = false;
    Pos selAnchor;
    double playQ = -1.0;
    juce::uint32 lastDigitTime = 0;
    int lastDigit = -1;
    Pos lastDigitPos;
    int lastDigitString = -1;
    std::vector<juce::ValueTree> clipboard;

    struct MeasureLayout
    {
        int bar = 0, row = 0;
        float x = 0, w = 0;
        std::vector<float> beatX; // centre of each beat
        float appendX = 0;        // slot after the last beat
    };
    std::vector<MeasureLayout> layout;
    std::vector<Row> rowInfo;
    std::vector<int> rowTabTop;      // y of the highest string line per row
    std::vector<int> rowStaffTop;    // y of the top notation line per row (-1 = no notation)
    std::vector<int> rowChordY;      // top of the chord band (-1 = none)
    std::vector<int> rowLyricY;      // baseline area for lyrics (-1 = none)
    static constexpr int lineGap = 14, techPad = 34, leftPad = 46, rhythmPad = 40, headerPad = 16;
    static constexpr float staffSpace = 7.0f;

    struct Ink { juce::Colour bg, text, textDim, outline, accent, accentDim, info, bad, warn, staff; } col;
    bool printMode = false;

    juce::ValueTree barNode (int b) const;
    juce::ValueTree beatAt (const Pos& p) const;
    int barCount() const;
    int numBeats (int b) const;
    int strings() const;
    float stringY (int row, int s) const;
    const MeasureLayout* layoutFor (int bar) const;
    bool locate (juce::Point<int>, Pos&, int& string) const;
    void moveCursor (int dBeat);
    void moveBar (int dBar);
    void setCursor (Pos p, bool extend);
    void changed();
    juce::ValueTree ensureBeatAtCursor();
    void begin (const juce::String& name) { doc.undo().beginNewTransaction (name); }
    juce::UndoManager* um() { return &doc.undo(); }

    void drawBar (juce::Graphics&, const MeasureLayout&);
    void drawRhythm (juce::Graphics&, const MeasureLayout&, const juce::ValueTree& bar);
    void drawNotation (juce::Graphics&, const MeasureLayout&, const juce::ValueTree& bar, bool rowStart);
    void drawChordDiagram (juce::Graphics&, float cx, float top, const juce::ValueTree& beat);
    float rowContentTop (int row) const;
    float rowContentBottom (int row) const;
    int prefixWidth (int bar, bool rowStart) const;
};
} // namespace ff
