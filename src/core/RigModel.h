#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

/** Amp rig model (Effects & Amps).

    A rig is a RIG node: on a TRACK (track insert, applies to the track's clips and live monitoring) or
    standalone (the live practice rig, kept in the app settings).

      RIG {enabled, order, namFile, irFile, rigName, <one property per Param>}

    `order` is a comma-separated list of block ids (gate,comp,drive,amp,eq,cab,chorus,delay,reverb).
    Every numeric parameter is stored as a float in its natural unit (dB, Hz, ms, 0..1); switches are 0/1.
    Missing properties read as the parameter's default, so rigs from older projects stay valid. */
namespace ff::rig
{
namespace ids
{
    #define FF_RIG_ID(n) inline const juce::Identifier n { #n };
    FF_RIG_ID (RIG) FF_RIG_ID (enabled) FF_RIG_ID (order) FF_RIG_ID (namFile) FF_RIG_ID (irFile) FF_RIG_ID (rigName)
    FF_RIG_ID (input)
    #undef FF_RIG_ID
}

enum Block { gate, comp, drive, amp, eq, cab, chorus, delay, reverb, numBlocks };

enum Param
{
    inGain, outGain,
    gateOn, gateThresh, gateRelease,
    compOn, compThresh, compRatio, compLevel,
    driveOn, driveGain, driveTone, driveLevel,
    ampOn, ampGain, ampLevel, namNormalize,
    eqOn, bass, mid, treble, presence,
    cabOn, cabLowCut, cabHighCut,
    chorusOn, chorusRate, chorusDepth, chorusMix,
    delayOn, delayTime, delayFeedback, delayTone, delayMix,
    reverbOn, reverbSize, reverbDamp, reverbMix,
    numParams
};

struct ParamInfo
{
    const char* id;
    const char* label;
    float min, max, def;
    const char* unit;      // "dB", "Hz", "ms", "%", ""
    int block;             // Block, or -1 for the rig's input/output
    bool isSwitch;         // on/off of its block
    float centre;          // value at the middle of a knob (skew), = (min+max)/2 for linear
};

const ParamInfo& info (int param);
/** The on/off switch parameter of a block. */
int switchFor (int block);
const char* blockId (int block);
const char* blockName (int block);
/** Text for a value, e.g. "-12.0 dB", "350 ms", "40 %". */
juce::String formatValue (int param, float value);

// ---- tree access ----
juce::ValueTree find (const juce::ValueTree& parent);
juce::ValueTree getOrCreate (juce::ValueTree parent, juce::UndoManager* = nullptr);
juce::ValueTree makeDefault();
float get (const juce::ValueTree& rig, int param);
void set (juce::ValueTree rig, int param, float value, juce::UndoManager* = nullptr);
bool isEnabled (const juce::ValueTree& rig);

using Order = std::array<int, numBlocks>;
Order defaultOrder();
Order getOrder (const juce::ValueTree& rig);
void setOrder (juce::ValueTree rig, const Order&, juce::UndoManager* = nullptr);
/** Moves `block` one place towards the input (-1) or output (+1). */
void moveBlock (juce::ValueTree rig, int block, int direction, juce::UndoManager* = nullptr);
/** 4 bits per slot, slot 0 in the low bits (for lock-free hand-over to the audio thread). */
juce::uint64 packOrder (const Order&) noexcept;
Order unpackOrder (juce::uint64) noexcept;

// ---- presets ----
/** All rig settings as JSON (a preset). Model and IR are stored as absolute paths. */
juce::var toPreset (const juce::ValueTree& rig, const juce::String& presetName);
/** Applies a preset; returns the names of referenced model / IR files that do not exist (for a warning).
    A preset without namFile / irFile keys (the built-in ones) keeps the rig's current model and IR. */
juce::StringArray applyPreset (juce::ValueTree rig, const juce::var& preset, juce::UndoManager* = nullptr);
struct FactoryPreset { juce::String name; juce::var json; };
/** Presets built into the app: pedal and tone settings only, they keep whatever model / IR is loaded. */
std::vector<FactoryPreset> factoryPresets();
} // namespace ff::rig
