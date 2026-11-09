#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../EngineHost.h"
#include "../../ProjectDocument.h"
#include "../../Widgets.h"
#include "core/Exercises.h"

namespace ff
{
/** Practice history kept in the app settings (not in projects): seconds practised per technique per day and the best
    clean tempo reached for each technique over time. */
class PracticeLog
{
public:
    explicit PracticeLog (juce::PropertiesFile& s);
    void addSeconds (const juce::String& technique, double seconds);
    void recordTempo (const juce::String& technique, int bpm);
    double secondsOn (const juce::String& date) const;
    double secondsOn (const juce::String& date, const juce::String& technique) const;
    double totalSeconds() const;
    int bestTempo (const juce::String& technique) const;   // 0 if none
    std::vector<std::pair<juce::String, int>> tempoHistory (const juce::String& technique) const;   // date, bpm
    int streakDays() const;   // consecutive days up to today (or yesterday) with practice
    double secondsLastDays (const juce::String& technique, int days) const;
    void save();
    static juce::String dateString (juce::Time t) { return t.formatted ("%Y-%m-%d"); }

private:
    juce::PropertiesFile& settings;
    juce::ValueTree tree { "PRACTICE" };
    juce::ValueTree day (const juce::String& date, bool create);
};

/** Training centre: pick a technique, get a generated exercise as tab, play it with a tempo trainer that speeds up as
    you go, follow a practice plan, and see your practice time, streak and tempo records. */
class TrainingPage : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    TrainingPage (EngineHost&, ProjectDocument&, juce::PropertiesFile&);
    ~TrainingPage() override;
    void setActive (bool);
    void togglePlay();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EngineHost& host;
    ProjectDocument& doc;
    juce::PropertiesFile& settings;
    PracticeLog log;
    training::Settings ex;
    juce::ValueTree score;
    double bpm = 80.0;
    bool active = false;
    int loopsAtTempo = 0;
    double lastPos = 0.0, pendingSeconds = 0.0;
    juce::uint32 lastTick = 0;

    juce::ListBox list;
    juce::Label title, tip, planDesc, bpmLabel, statusLabel;
    juce::ComboBox planBox, barsBox, notesBox, tuningBox, stepBox, everyBox;
    juce::Slider positionSlider, startSlider, goalSlider;
    juce::Label positionCaption, startCaption, goalCaption, stepCaption, everyCaption, barsCaption, notesCaption, tuningCaption;
    juce::TextButton variationButton { "New variation" }, playButton { "Play" }, nextButton { "Next in plan" },
        cleanButton { "Played it clean" }, resetTempoButton { "Reset tempo" };
    juce::ToggleButton autoSpeed { "Speed up automatically" }, clickToggle { "Click" }, countToggle { "Count-in" };
    std::vector<std::unique_ptr<juce::TextButton>> planChips;

    struct TabView : public juce::Component
    {
        explicit TabView (TrainingPage& p) : page (p) {}
        void paint (juce::Graphics&) override;
        TrainingPage& page;
    } tabView { *this };

    /** Scale shapes: the fretboard with every note of the shape and its degree (roots highlighted), and the note being
        played lit up. */
    struct NeckView : public juce::Component
    {
        explicit NeckView (TrainingPage& p) : page (p) {}
        void paint (juce::Graphics&) override;
        TrainingPage& page;
    } neckView { *this };
    std::vector<training::Step::Note> diagram;   // of the current technique (empty: not a scale shape)
    int playingStep() const;                     // index into the pattern being played, -1 when stopped

    juce::Rectangle<int> leftBox, mainBox, trainerBox, statsBox, chartBox;

    // ListBoxModel
    int getNumRows() override { return (int) training::techniques().size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;

    void selectTechnique (int index);
    void rebuild();            // new score from the settings and tempo; hands it to the engine
    void pushEngine();
    void setTempo (double);
    void refreshPlan();
    void updateStatus();
    void timerCallback() override;
    juce::String currentId() const { return training::techniques()[(size_t) ex.technique].id; }
    void drawStats (juce::Graphics&);
};
} // namespace ff
