#pragma once
#include "TabEditor.h"

namespace ff
{
/** PDF export of the tab score (notation + tab, chords, lyrics) in print styling.

    Each A4 page is rendered at 200 dpi with the same drawing code as the editor and stored as an image,
    so what you see is what prints. Systems are never split across pages. One track per section;
    each track starts on a new page with the song header. */
struct ScorePdf
{
    struct Options
    {
        bool allTracks = true;
        int track = 0;                // used when allTracks is false
        bool letter = false;          // US Letter instead of A4
    };
    static juce::Result write (ProjectDocument&, EngineHost&, const juce::ValueTree& score, const juce::File&, const Options&);

    /** Minimal PDF writer: one full-page JPEG per page. Pages are in points (1/72 inch). */
    static bool writeImagesAsPdf (const std::vector<juce::Image>& pages, float pageWidthPt, float pageHeightPt,
                                  const juce::String& title, const juce::File& file);
};
} // namespace ff
