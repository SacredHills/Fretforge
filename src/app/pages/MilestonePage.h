#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ff
{
/** Clearly-labelled page for a section that is not implemented yet. It lists the
    planned scope and deliberately contains no controls. */
class MilestonePage : public juce::Component
{
public:
    MilestonePage (juce::String sectionTitle, int milestoneNumber, juce::String milestoneName, juce::StringArray plannedScope,
                   juce::String architectureNote = {});
    void paint (juce::Graphics&) override;

private:
    juce::String title, milestone, note;
    int number;
    juce::StringArray scope;
};
} // namespace ff
