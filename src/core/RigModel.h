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
    FF_RIG_ID (input) FF_RIG_ID (PLUGIN) FF_RIG_ID (slot) FF_RIG_ID (desc) FF_RIG_ID (state) FF_RIG_ID (name)
    FF_RIG_ID (INSERTS) FF_RIG_ID (bypass)
    #undef FF_RIG_ID
}

enum Block { gate, comp, drive, amp, eq, cab, chorus, delay, reverb, plug1, plug2, plug3, plug4, numBlocks };
constexpr int numPluginSlots = 4;
inline bool isPluginBlock (int b) { return b >= plug1 && b <= plug4; }
inline int pluginSlotOf (int b) { return b - plug1; }

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
    namCalibrate, calLevel,          // NAM input calibration: interface level (dBu at 0 dBFS)
    plug1On, plug2On, plug3On, plug4On,
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
/** On/off parameters: block switches, NAM normalisation and calibration. */
bool isToggle (int param);
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

// ---- plugin slots (VST3 / AU) ----
/** PLUGIN {slot, desc (PluginDescription XML), state (base64 plugin state), name} children of a RIG. */
juce::ValueTree pluginNode (const juce::ValueTree& rig, int slot);
/** Puts a plugin in a slot (replacing what was there); the block keeps its place in the chain and is switched on. */
void setPlugin (juce::ValueTree rig, int slot, const juce::String& descXml, const juce::String& displayName, juce::UndoManager* = nullptr);
void removePlugin (juce::ValueTree rig, int slot, juce::UndoManager* = nullptr);
/** First empty slot, -1 if all are used. */
int freePluginSlot (const juce::ValueTree& rig);
/** Chain label of a block: the plugin's name for an occupied plugin slot. */
juce::String blockLabel (const juce::ValueTree& rig, int block);

// ---- track inserts: plugins on a track after its amp rig, before the fader ----
//   TRACK / INSERTS / PLUGIN {slot, desc, state, name, bypass}; slots play in order 0..maxInserts-1
constexpr int maxInserts = 64;   // per track; effectively unlimited
juce::ValueTree findInserts (const juce::ValueTree& track);
juce::ValueTree insertNode (const juce::ValueTree& track, int slot);
int numInserts (const juce::ValueTree& track);
/** Adds a plugin after the last insert; returns its slot (-1 if the track is full). */
int addInsert (juce::ValueTree track, const juce::String& descXml, const juce::String& displayName, juce::UndoManager* = nullptr,
              const juce::String& state = {}, bool bypassed = false);
void replaceInsert (juce::ValueTree track, int slot, const juce::String& descXml, const juce::String& displayName, juce::UndoManager* = nullptr);
/** Removes a slot; the ones after it move up. */
void removeInsert (juce::ValueTree track, int slot, juce::UndoManager* = nullptr);
/** Swaps a slot with its neighbour (direction -1 / +1). */
void moveInsert (juce::ValueTree track, int slot, int direction, juce::UndoManager* = nullptr);

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
