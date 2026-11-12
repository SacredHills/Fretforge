#include "NamModel.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/model_config.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/convnet.h"
#include "NAM/container.h"
#include "NAM/sequential.h"
#include "NAM/wavenet/model.h"
#include <cmath>

namespace ff::fx
{
namespace
{
    juce::String str (const nlohmann::json& j, const char* key)
    {
        if (j.is_object() && j.contains (key) && j[key].is_string())
            return juce::String::fromUTF8 (j[key].get<std::string>().c_str());
        return {};
    }

    bool parseFile (const juce::File& f, nlohmann::json& j, juce::String& error)
    {
        juce::MemoryBlock mb;
        if (! f.existsAsFile() || ! f.loadFileAsData (mb))
        {
            error = "Cannot read " + f.getFullPathName();
            return false;
        }
        try
        {
            j = nlohmann::json::parse ((const char*) mb.getData(), (const char*) mb.getData() + mb.getSize());
        }
        catch (const std::exception& e)
        {
            error = f.getFileName() + " is not a valid .nam file (" + juce::String (e.what()) + ").";
            return false;
        }
        if (! j.is_object() || ! j.contains ("architecture"))
        {
            error = f.getFileName() + " is not a Neural Amp Modeler model.";
            return false;
        }
        return true;
    }

    /** NAM registers its architectures from static objects; in a static library the linker drops object files
        nobody references. Referencing each factory here pulls them in (and registers them if the static
        registration still did not run). */
    void ensureArchitectures()
    {
        static const bool done = [] {
            auto& reg = nam::ConfigParserRegistry::instance();
            auto add = [&reg] (const char* name, nam::ConfigParserFunction f) { if (! reg.has (name)) reg.registerParser (name, std::move (f)); };
            add ("Linear", nam::linear::create_config);
            add ("LSTM", nam::lstm::create_config);
            add ("ConvNet", nam::convnet::create_config);
            add ("SlimmableContainer", nam::container::create_config);
            add ("Sequential", nam::sequential::create_config);
            add ("WaveNet", nam::wavenet::create_config);
            return true;
        }();
        juce::ignoreUnused (done);
    }

    void fillInfo (const nlohmann::json& j, const juce::File& f, NamInfo& out)
    {
        out = {};
        out.architecture = str (j, "architecture");
        out.version = str (j, "version");
        if (j.contains ("sample_rate") && j["sample_rate"].is_number())
        {
            const double sr = j["sample_rate"].get<double>();
            if (sr > 1000.0) { out.sampleRate = sr; out.sampleRateKnown = true; }
        }
        if (j.contains ("metadata") && j["metadata"].is_object())
        {
            const auto& m = j["metadata"];
            out.name = str (m, "name");
            out.gearMake = str (m, "gear_make");
            out.gearModel = str (m, "gear_model");
            out.gearType = str (m, "gear_type");
            out.toneType = str (m, "tone_type");
            out.modeledBy = str (m, "modeled_by");
            if (m.contains ("input_level_dbu") && m["input_level_dbu"].is_number())
            {
                out.inputLevelDbu = m["input_level_dbu"].get<double>();
                out.hasInputLevel = std::isfinite (out.inputLevelDbu);
            }
            if (m.contains ("loudness") && m["loudness"].is_number())
            {
                out.loudness = m["loudness"].get<double>();
                out.hasLoudness = std::isfinite (out.loudness);
            }
        }
        if (out.name.isEmpty()) out.name = f.getFileNameWithoutExtension();
    }
}

juce::String NamInfo::displayName() const
{
    const auto gear = (gearMake + " " + gearModel).trim();
    if (gear.isNotEmpty() && name.isNotEmpty() && ! gear.containsIgnoreCase (name)) return gear + " - " + name;
    return gear.isNotEmpty() ? gear : name;
}

bool readNamInfo (const juce::File& f, NamInfo& out, juce::String& error)
{
    nlohmann::json j;
    if (! parseFile (f, j, error)) return false;
    fillInfo (j, f, out);
    return true;
}

NamModel::~NamModel() = default;

std::unique_ptr<NamModel> NamModel::load (const juce::File& f, juce::String& error)
{
    ensureArchitectures();
    nlohmann::json j;
    if (! parseFile (f, j, error)) return nullptr;

    std::unique_ptr<NamModel> m (new NamModel());
    m->source = f;
    fillInfo (j, f, m->meta);
    try
    {
        nam::DspLoadOptions opt;
        opt.prewarm = false;
        m->dsp = nam::get_dsp (j, opt);
    }
    catch (const std::exception& e)
    {
        error = f.getFileName() + ": " + juce::String (e.what());
        return nullptr;
    }
    if (m->dsp == nullptr)
    {
        error = f.getFileName() + ": unsupported model.";
        return nullptr;
    }
    if (m->dsp->NumInputChannels() != 1 || m->dsp->NumOutputChannels() < 1)
    {
        error = f.getFileName() + ": only mono-input models are supported.";
        return nullptr;
    }
    try
    {
        m->dsp->Reset (m->meta.sampleRate, maxModelBlock);   // allocates and prewarms
    }
    catch (const std::exception& e)
    {
        error = f.getFileName() + ": " + juce::String (e.what());
        return nullptr;
    }
    m->modelIn.assign ((size_t) maxModelBlock, 0.0f);
    m->modelOut.assign ((size_t) maxModelBlock * 2, 0.0f);   // room for a second output channel (ignored)
    return m;
}

float NamModel::normalisationGain() const noexcept
{
    if (! meta.hasLoudness) return 1.0f;
    return (float) std::pow (10.0, juce::jlimit (-40.0, 40.0, -14.0 - meta.loudness) / 20.0);
}

float NamModel::calibrationGain (float interfaceDbu) const noexcept
{
    if (! meta.hasInputLevel) return 1.0f;
    return (float) std::pow (10.0, juce::jlimit (-40.0, 40.0, (double) interfaceDbu - meta.inputLevelDbu) / 20.0);
}

void NamModel::prepare (double rate, int block)
{
    devRate = rate;
    maxDevBlock = juce::jmax (1, block);
    const double mr = meta.sampleRate;
    resampling = std::abs (rate - mr) > 0.5;
    speedUp = rate / mr;
    speedDown = mr / rate;
    antiAlias = resampling && rate > mr * 1.1;
    if (antiAlias)
    {
        aa1.set (Biquad::lowPass, rate, mr * 0.45, 0.541);   // 4th-order Butterworth
        aa2.set (Biquad::lowPass, rate, mr * 0.45, 1.307);
    }
    const int modelBlock = (int) std::ceil (maxDevBlock * speedDown) + 64;
    constexpr int prime = 16;
    inBuf.assign ((size_t) maxDevBlock + 64, 0.0f);
    outBuf.assign ((size_t) modelBlock + prime + 64, 0.0f);
    if ((int) modelIn.size() < modelBlock) { modelIn.assign ((size_t) modelBlock, 0.0f); modelOut.assign ((size_t) modelBlock * 2, 0.0f); }
    // priming + the two Lagrange stages (2 input samples each) + the anti-alias filter's group delay
    double lat = prime * speedUp + 2.0 + 2.0 * speedUp;
    if (antiAlias) lat += 2.613 / (2.0 * juce::MathConstants<double>::pi * mr * 0.45) * rate;
    latency = resampling ? (int) std::lround (lat) : 0;
    reset();
}

void NamModel::reset() noexcept
{
    up.reset();
    down.reset();
    aa1.reset();
    aa2.reset();
    inCount = 0;
    outCount = resampling ? 16 : 0;   // primed with silence so the output side never runs dry
    std::fill (outBuf.begin(), outBuf.end(), 0.0f);
}

void NamModel::process (float* io, int n) noexcept
{
    if (dsp == nullptr || n <= 0) return;
    if (! resampling)
    {
        // in place is fine: runNetwork copies through its own buffers
        int done = 0;
        while (done < n)
        {
            const int len = juce::jmin (maxModelBlock, n - done);
            NAM_SAMPLE* ip[1] = { modelIn.data() };
            NAM_SAMPLE* op[2] = { modelOut.data(), modelOut.data() + maxModelBlock };
            std::copy (io + done, io + done + len, modelIn.data());
            dsp->process (ip, op, len);
            std::copy (modelOut.data(), modelOut.data() + len, io + done);
            done += len;
        }
        return;
    }

    n = juce::jmin (n, maxDevBlock);
    // 1. device rate -> model rate
    float* in = inBuf.data() + inCount;
    std::copy (io, io + n, in);
    if (antiAlias) { aa1.process (in, n); aa2.process (in, n); }
    inCount += n;
    int m = (int) std::floor (((double) inCount - 2.0) / speedUp);
    m = juce::jlimit (0, (int) outBuf.size() - outCount, m);
    m = juce::jmin (m, (int) modelIn.size());
    const int used = m > 0 ? up.process (speedUp, inBuf.data(), modelIn.data(), m) : 0;
    if (used > 0)
    {
        std::memmove (inBuf.data(), inBuf.data() + used, sizeof (float) * (size_t) (inCount - used));
        inCount -= used;
    }

    // 2. the network
    if (m > 0)
    {
        int done = 0;
        while (done < m)
        {
            const int len = juce::jmin (maxModelBlock, m - done);
            NAM_SAMPLE* ip[1] = { modelIn.data() + done };
            NAM_SAMPLE* op[2] = { modelOut.data(), modelOut.data() + maxModelBlock };
            dsp->process (ip, op, len);
            std::copy (modelOut.data(), modelOut.data() + len, outBuf.data() + outCount);
            outCount += len;
            done += len;
        }
    }

    // 3. model rate -> device rate
    const int needed = (int) std::ceil (n * speedDown) + 2;
    if (outCount < needed)
    {
        // should not happen (the output side is primed); never read past the data
        std::fill (outBuf.data() + outCount, outBuf.data() + needed, 0.0f);
        outCount = needed;
    }
    const int used2 = down.process (speedDown, outBuf.data(), io, n);
    std::memmove (outBuf.data(), outBuf.data() + used2, sizeof (float) * (size_t) (outCount - used2));
    outCount -= used2;
}
} // namespace ff::fx
