#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <cmath>
#include <vector>

/** Track / clip / marker model helpers and the clip layering ("take masking") algorithm.
    Timeline positions are stored in seconds, so audio stays put when the sample rate changes. */
namespace ff::timeline
{
namespace ids
{
    #define FF_TL_ID(name) inline const juce::Identifier name { #name };
    FF_TL_ID (TRACK) FF_TL_ID (CLIP) FF_TL_ID (MARKER)
    FF_TL_ID (id) FF_TL_ID (name) FF_TL_ID (colour) FF_TL_ID (gainDb) FF_TL_ID (pan) FF_TL_ID (mute) FF_TL_ID (solo)
    FF_TL_ID (armed) FF_TL_ID (monitor) FF_TL_ID (inputChannel) FF_TL_ID (inputStereo)
    FF_TL_ID (file) FF_TL_ID (start) FF_TL_ID (length) FF_TL_ID (offset) FF_TL_ID (fadeIn) FF_TL_ID (fadeOut)
    FF_TL_ID (muted) FF_TL_ID (takeGroup) FF_TL_ID (time)
    FF_TL_ID (kind) FF_TL_ID (group) FF_TL_ID (output) FF_TL_ID (SEND) FF_TL_ID (target) FF_TL_ID (level) FF_TL_ID (pre)
    #undef FF_TL_ID
}

constexpr int maxTracks = 128;
constexpr double defaultFade = 0.005; // 5 ms fades avoid clicks at clip edges

juce::String newId();
/** Colour palette as ARGB hex strings (UI converts to juce::Colour). */
const juce::StringArray& trackPalette();

juce::ValueTree createTrack (const juce::String& name, int paletteIndex);
juce::ValueTree createClip (const juce::String& portableFile, const juce::String& name,
                            double start, double length, double offset);
juce::ValueTree createMarker (const juce::String& name, double time);

// ------------------------------------------------------------------ routing (bus and FX tracks)

/** Audio tracks hold clips and record; bus tracks mix the tracks routed to them; FX (return) tracks are fed
    by sends. All three can have an amp rig and plugins, a fader and pan, and an output. */
enum class TrackKind { audio, bus, fx, instrument };
TrackKind kindOf (const juce::ValueTree& track);
inline bool isAudioTrack (const juce::ValueTree& t) { return kindOf (t) == TrackKind::audio; }
/** Instrument tracks hold MIDI clips played by their instrument plugin. */
inline bool isInstrumentTrack (const juce::ValueTree& t) { return kindOf (t) == TrackKind::instrument; }
/** Audio and instrument tracks make sound of their own; bus and FX tracks mix what is routed to them. */
inline bool isSourceKind (TrackKind k) { return k == TrackKind::audio || k == TrackKind::instrument; }
inline bool isSourceTrack (const juce::ValueTree& t) { return isSourceKind (kindOf (t)); }
/** Whether a clip may live on a track: audio clips on audio tracks, MIDI clips on instrument tracks. */
bool acceptsClip (const juce::ValueTree& track, const juce::ValueTree& clip);
juce::String kindName (TrackKind);
juce::ValueTree createBusTrack (const juce::String& name, int paletteIndex, TrackKind kind);
juce::ValueTree findTrack (const juce::ValueTree& tracks, const juce::String& id);
/** The bus a track plays into ("" = master); a missing bus also means master. */
juce::String outputOf (const juce::ValueTree& track);
/** False if routing `from` into `to` (output or send) would feed `to` back into itself, or `to` cannot take input. */
bool canRouteTo (const juce::ValueTree& tracks, const juce::ValueTree& from, const juce::ValueTree& to);
/** Sends: SEND children { target (track id), level (dB), pre (pre-fader) }. */
constexpr int maxSends = 8;
juce::ValueTree addSend (juce::ValueTree track, const juce::String& targetId, double levelDb, bool preFader, juce::UndoManager*);
/** Removes sends and outputs that point at a track that is gone (call after deleting a track). */
void removeRoutesTo (juce::ValueTree tracks, const juce::String& id, juce::UndoManager*);

double clipEnd (const juce::ValueTree& clip);
/** End time of the last unmuted clip on any track (0 if none). */
double songEnd (const juce::ValueTree& tracks);

// ------------------------------------------------------------------ layering

struct ClipSpan
{
    double start = 0, length = 0, offset = 0, fadeIn = 0, fadeOut = 0, gain = 1;
    bool muted = false;
    int index = 0; // caller's identifier
};

/** A contiguous region of one clip that is audible after layering. Fade ranges are
    absolute timeline times; gain at t = gain * fadeInCurve(t) * fadeOutCurve(t). */
struct Piece
{
    int clip = 0;
    double start = 0, end = 0, srcAtStart = 0;
    double fadeInStart = 0, fadeInEnd = 0, fadeOutStart = 0, fadeOutEnd = 0;
    double gain = 1;
};

/** Clips later in the vector are layered on top. A clip hides the clips beneath it
    everywhere it is at full level; during its fades the lower clip keeps playing and
    fades the opposite way, giving equal-power crossfades. Muted clips are ignored. */
std::vector<Piece> visiblePieces (const std::vector<ClipSpan>& clipsBottomToTop);

/** Equal-power fade curve value for t inside [a, b]; 1 outside, rising (fadeIn) or falling. */
inline double fadeGain (double t, double a, double b, bool rising) noexcept
{
    if (b <= a) return 1.0;
    if (t <= a) return rising ? 0.0 : 1.0;
    if (t >= b) return rising ? 1.0 : 0.0;
    const double x = (t - a) / (b - a) * 1.5707963267948966;
    return rising ? std::sin (x) : std::cos (x);
}
} // namespace ff::timeline
