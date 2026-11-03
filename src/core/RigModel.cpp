#include "RigModel.h"

namespace ff::rig
{
namespace
{
    // id, label, min, max, default, unit, block, switch, centre
    const ParamInfo params[numParams] = {
        { "inGain",        "Input",      -24.0f,  24.0f,   0.0f,  "dB", -1,     false, 0.0f },
        { "outGain",       "Output",     -40.0f,  12.0f,   0.0f,  "dB", -1,     false, -14.0f },
        { "gateOn",        "Gate",         0.0f,   1.0f,   1.0f,  "",   gate,   true,  0.5f },
        { "gateThresh",    "Threshold",  -90.0f, -10.0f, -62.0f,  "dB", gate,   false, -50.0f },
        { "gateRelease",   "Release",      5.0f, 500.0f,  80.0f,  "ms", gate,   false, 80.0f },
        { "compOn",        "Compressor",   0.0f,   1.0f,   0.0f,  "",   comp,   true,  0.5f },
        { "compThresh",    "Threshold",  -50.0f,   0.0f, -24.0f,  "dB", comp,   false, -25.0f },
        { "compRatio",     "Ratio",        1.0f,  20.0f,   4.0f,  ":1", comp,   false, 4.0f },
        { "compLevel",     "Level",      -12.0f,  24.0f,   6.0f,  "dB", comp,   false, 6.0f },
        { "driveOn",       "Overdrive",    0.0f,   1.0f,   0.0f,  "",   drive,  true,  0.5f },
        { "driveGain",     "Drive",        0.0f,   1.0f,   0.4f,  "%",  drive,  false, 0.5f },
        { "driveTone",     "Tone",         0.0f,   1.0f,   0.5f,  "%",  drive,  false, 0.5f },
        { "driveLevel",    "Level",      -24.0f,  12.0f,   0.0f,  "dB", drive,  false, -6.0f },
        { "ampOn",         "Amp",          0.0f,   1.0f,   1.0f,  "",   amp,    true,  0.5f },
        { "ampGain",       "Gain",         0.0f,   1.0f,   0.5f,  "%",  amp,    false, 0.5f },
        { "ampLevel",      "Level",      -24.0f,  24.0f,   0.0f,  "dB", amp,    false, 0.0f },
        { "namNormalize",  "Normalise",    0.0f,   1.0f,   1.0f,  "",   amp,    false, 0.5f },
        { "eqOn",          "EQ",           0.0f,   1.0f,   1.0f,  "",   eq,     true,  0.5f },
        { "bass",          "Bass",       -12.0f,  12.0f,   0.0f,  "dB", eq,     false, 0.0f },
        { "mid",           "Mid",        -12.0f,  12.0f,   0.0f,  "dB", eq,     false, 0.0f },
        { "treble",        "Treble",     -12.0f,  12.0f,   0.0f,  "dB", eq,     false, 0.0f },
        { "presence",      "Presence",   -12.0f,  12.0f,   0.0f,  "dB", eq,     false, 0.0f },
        { "cabOn",         "Cabinet",      0.0f,   1.0f,   1.0f,  "",   cab,    true,  0.5f },
        { "cabLowCut",     "Low cut",     20.0f, 300.0f,  70.0f,  "Hz", cab,    false, 80.0f },
        { "cabHighCut",    "High cut",  2000.0f, 20000.0f, 12000.0f, "Hz", cab, false, 7000.0f },
        { "chorusOn",      "Chorus",       0.0f,   1.0f,   0.0f,  "",   chorus, true,  0.5f },
        { "chorusRate",    "Rate",         0.05f,  5.0f,   0.8f,  "Hz", chorus, false, 1.0f },
        { "chorusDepth",   "Depth",        0.0f,   1.0f,   0.4f,  "%",  chorus, false, 0.5f },
        { "chorusMix",     "Mix",          0.0f,   1.0f,   0.5f,  "%",  chorus, false, 0.5f },
        { "delayOn",       "Delay",        0.0f,   1.0f,   0.0f,  "",   delay,  true,  0.5f },
        { "delayTime",     "Time",        20.0f, 2000.0f, 380.0f, "ms", delay,  false, 400.0f },
        { "delayFeedback", "Feedback",     0.0f,   0.95f,  0.35f, "%",  delay,  false, 0.45f },
        { "delayTone",     "Tone",         0.0f,   1.0f,   0.6f,  "%",  delay,  false, 0.5f },
        { "delayMix",      "Mix",          0.0f,   1.0f,   0.25f, "%",  delay,  false, 0.5f },
        { "reverbOn",      "Reverb",       0.0f,   1.0f,   0.0f,  "",   reverb, true,  0.5f },
        { "reverbSize",    "Size",         0.0f,   1.0f,   0.5f,  "%",  reverb, false, 0.5f },
        { "reverbDamp",    "Damping",      0.0f,   1.0f,   0.5f,  "%",  reverb, false, 0.5f },
        { "reverbMix",     "Mix",          0.0f,   1.0f,   0.2f,  "%",  reverb, false, 0.5f },
    };

    const char* blockIds[numBlocks]   = { "gate", "comp", "drive", "amp", "eq", "cab", "chorus", "delay", "reverb" };
    const char* blockNames[numBlocks] = { "Noise gate", "Compressor", "Overdrive", "Amp", "EQ", "Cabinet", "Chorus", "Delay", "Reverb" };
    const int switches[numBlocks]     = { gateOn, compOn, driveOn, ampOn, eqOn, cabOn, chorusOn, delayOn, reverbOn };

    juce::Identifier pid (int p) { return juce::Identifier (params[p].id); }
}

const ParamInfo& info (int p) { return params[juce::jlimit (0, numParams - 1, p)]; }
int switchFor (int block) { return switches[juce::jlimit (0, numBlocks - 1, block)]; }
const char* blockId (int block) { return blockIds[juce::jlimit (0, numBlocks - 1, block)]; }
const char* blockName (int block) { return blockNames[juce::jlimit (0, numBlocks - 1, block)]; }

juce::String formatValue (int p, float v)
{
    const auto& pi = info (p);
    const juce::String unit (pi.unit);
    if (pi.isSwitch || p == namNormalize) return v >= 0.5f ? "On" : "Off";
    if (unit == "%")  return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
    if (unit == "ms") return juce::String (juce::roundToInt (v)) + " ms";
    if (unit == ":1") return juce::String (v, 1) + ":1";
    if (unit == "dB") return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
    return juce::String (v, 2);
}

juce::ValueTree find (const juce::ValueTree& parent)
{
    return parent.hasType (ids::RIG) ? parent : parent.getChildWithName (ids::RIG);
}

juce::ValueTree makeDefault()
{
    juce::ValueTree r (ids::RIG);
    r.setProperty (ids::enabled, true, nullptr);
    r.setProperty (ids::order, "gate,comp,drive,amp,eq,cab,chorus,delay,reverb", nullptr);
    return r;
}

juce::ValueTree getOrCreate (juce::ValueTree parent, juce::UndoManager* um)
{
    if (auto r = find (parent); r.isValid()) return r;
    auto r = makeDefault();
    r.setProperty (ids::enabled, false, nullptr);
    parent.appendChild (r, um);
    return r;
}

float get (const juce::ValueTree& rig, int p)
{
    const auto& pi = info (p);
    const auto* v = rig.getPropertyPointer (pid (p));
    if (v == nullptr || v->isVoid()) return pi.def;
    return juce::jlimit (pi.min, pi.max, (float) (double) *v);
}

void set (juce::ValueTree rig, int p, float value, juce::UndoManager* um)
{
    const auto& pi = info (p);
    value = juce::jlimit (pi.min, pi.max, value);
    if (pi.isSwitch || p == namNormalize) value = value >= 0.5f ? 1.0f : 0.0f;
    rig.setProperty (pid (p), (double) value, um);
}

bool isEnabled (const juce::ValueTree& rig) { return rig.isValid() && (bool) rig.getProperty (ids::enabled, false); }

Order defaultOrder()
{
    Order o {};
    for (int i = 0; i < numBlocks; ++i) o[(size_t) i] = i;
    return o;
}

Order getOrder (const juce::ValueTree& rig)
{
    auto parts = juce::StringArray::fromTokens (rig[ids::order].toString(), ",", "");
    Order o {};
    int n = 0;
    std::array<bool, numBlocks> used {};
    for (auto& s : parts)
        for (int b = 0; b < numBlocks; ++b)
            if (s.trim() == blockIds[b] && ! used[(size_t) b] && n < numBlocks)
            {
                o[(size_t) n++] = b;
                used[(size_t) b] = true;
            }
    for (int b = 0; b < numBlocks; ++b)   // missing blocks keep their default place at the end
        if (! used[(size_t) b]) o[(size_t) n++] = b;
    return o;
}

void setOrder (juce::ValueTree rig, const Order& o, juce::UndoManager* um)
{
    juce::StringArray s;
    for (int b : o) s.add (blockIds[b]);
    rig.setProperty (ids::order, s.joinIntoString (","), um);
}

void moveBlock (juce::ValueTree rig, int block, int direction, juce::UndoManager* um)
{
    auto o = getOrder (rig);
    for (int i = 0; i < numBlocks; ++i)
        if (o[(size_t) i] == block)
        {
            const int j = i + (direction < 0 ? -1 : 1);
            if (j < 0 || j >= numBlocks) return;
            std::swap (o[(size_t) i], o[(size_t) j]);
            setOrder (rig, o, um);
            return;
        }
}

juce::uint64 packOrder (const Order& o) noexcept
{
    juce::uint64 v = 0;
    for (int i = 0; i < numBlocks; ++i) v |= (juce::uint64) (o[(size_t) i] & 0xf) << (4 * i);
    return v;
}

Order unpackOrder (juce::uint64 v) noexcept
{
    Order o {};
    for (int i = 0; i < numBlocks; ++i) o[(size_t) i] = juce::jlimit (0, numBlocks - 1, (int) ((v >> (4 * i)) & 0xf));
    return o;
}

juce::var toPreset (const juce::ValueTree& rig, const juce::String& presetName)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "FretForge rig");
    o->setProperty ("version", 1);
    o->setProperty ("name", presetName);
    o->setProperty ("order", rig[ids::order].toString().isNotEmpty() ? rig[ids::order].toString()
                                                                     : makeDefault()[ids::order].toString());
    o->setProperty ("namFile", rig[ids::namFile].toString());
    o->setProperty ("irFile", rig[ids::irFile].toString());
    auto* p = new juce::DynamicObject();
    for (int i = 0; i < numParams; ++i) p->setProperty (params[i].id, (double) get (rig, i));
    o->setProperty ("params", juce::var (p));
    return juce::var (o);
}

juce::StringArray applyPreset (juce::ValueTree rig, const juce::var& preset, juce::UndoManager* um)
{
    juce::StringArray missing;
    if (! preset.isObject()) return missing;
    const auto ps = preset["params"];
    for (int i = 0; i < numParams; ++i)
    {
        const auto v = ps[params[i].id];
        set (rig, i, v.isVoid() ? params[i].def : (float) (double) v, um);
    }
    if (preset["order"].toString().isNotEmpty()) rig.setProperty (ids::order, preset["order"].toString(), um);
    for (auto* key : { "namFile", "irFile" })
    {
        if (! preset.hasProperty (key)) continue;   // built-in presets leave the loaded model / IR alone
        const auto path = preset[key].toString();
        const bool exists = path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile();
        if (path.isNotEmpty() && ! exists) missing.add (juce::File (path).getFileName());
        rig.setProperty (juce::String (key) == "namFile" ? ids::namFile : ids::irFile, exists ? path : juce::String(), um);
    }
    rig.setProperty (ids::rigName, preset["name"].toString(), um);
    return missing;
}

std::vector<FactoryPreset> factoryPresets()
{
    struct Def { const char* name; std::vector<std::pair<int, float>> values; };
    const Def defs[] = {
        { "Clean",        { { ampGain, 0.2f }, { ampLevel, 2.0f }, { bass, 1.0f }, { treble, 2.0f }, { compOn, 1 }, { compThresh, -26 }, { compRatio, 3 }, { compLevel, 4 },
                            { chorusOn, 1 }, { chorusMix, 0.35f }, { reverbOn, 1 }, { reverbMix, 0.18f } } },
        { "Crunch",       { { ampGain, 0.55f }, { mid, 2.0f }, { treble, 1.0f }, { reverbOn, 1 }, { reverbMix, 0.12f } } },
        { "Lead",         { { driveOn, 1 }, { driveGain, 0.35f }, { driveLevel, 3.0f }, { ampGain, 0.75f }, { ampLevel, -3.0f }, { mid, 3.0f },
                            { delayOn, 1 }, { delayTime, 420 }, { delayMix, 0.22f }, { reverbOn, 1 }, { reverbMix, 0.15f } } },
        { "High gain",    { { gateThresh, -52.0f }, { driveOn, 1 }, { driveGain, 0.1f }, { driveTone, 0.6f }, { driveLevel, 6.0f },
                            { ampGain, 0.9f }, { ampLevel, -6.0f }, { bass, 2.0f }, { mid, -3.0f }, { treble, 2.0f }, { presence, 2.0f } } },
        { "Ambient",      { { ampGain, 0.3f }, { compOn, 1 }, { chorusOn, 1 }, { chorusDepth, 0.6f }, { chorusMix, 0.4f },
                            { delayOn, 1 }, { delayTime, 600 }, { delayFeedback, 0.55f }, { delayMix, 0.35f },
                            { reverbOn, 1 }, { reverbSize, 0.85f }, { reverbMix, 0.4f } } },
    };
    std::vector<FactoryPreset> out;
    for (const auto& d : defs)
    {
        auto r = makeDefault();
        for (auto [p, v] : d.values) set (r, p, v);
        auto json = toPreset (r, d.name);
        if (auto* o = json.getDynamicObject()) { o->removeProperty ("namFile"); o->removeProperty ("irFile"); }
        out.push_back ({ d.name, json });
    }
    return out;
}
} // namespace ff::rig
