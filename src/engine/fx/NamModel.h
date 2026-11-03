#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include <memory>

namespace nam { class DSP; }

namespace ff::fx
{
/** Facts about a Neural Amp Modeler capture, read from its .nam file. */
struct NamInfo
{
    juce::String name, gearMake, gearModel, gearType, toneType, modeledBy, architecture, version;
    double sampleRate = 48000.0;       // training rate (48 kHz assumed when the file does not say)
    bool sampleRateKnown = false;
    double loudness = 0.0;             // dB, output for a "typical" input
    bool hasLoudness = false;
    juce::String displayName() const;  // "Make Model (name)" or the file name
};

/** Reads only the metadata (fast enough for a model browser). */
bool readNamInfo (const juce::File&, NamInfo& out, juce::String& error);

/** A loaded NAM capture plus the sample-rate adaptation around it.

    The network always runs at its training rate; when the device runs at another rate the audio is
    resampled on the way in and out (4-point Lagrange, with an anti-alias filter when decimating).
    load() and prepare() allocate and may take a while: call them off the audio thread.
    process() is real-time safe. */
class NamModel
{
public:
    ~NamModel();
    static std::unique_ptr<NamModel> load (const juce::File&, juce::String& error);

    const NamInfo& info() const noexcept { return meta; }
    const juce::File& file() const noexcept { return source; }

    /** Allocates buffers for the device rate / block size. Not real-time safe. */
    void prepare (double deviceRate, int maxBlock);
    double preparedRate() const noexcept { return devRate; }
    int preparedBlock() const noexcept { return maxDevBlock; }
    bool isPreparedFor (double rate, int block) const noexcept { return std::abs (devRate - rate) < 0.01 && block <= maxDevBlock; }
    bool isResampling() const noexcept { return resampling; }
    /** Extra latency added by resampling, in device samples. */
    int latencySamples() const noexcept { return latency; }
    /** Linear gain that brings the model to -18 dB loudness (1 if unknown). */
    float normalisationGain() const noexcept;

    /** Mono, in place, at the device rate. n <= the prepared block size. */
    void process (float* io, int n) noexcept;
    /** Clears the resampler state (not the network's internal history). */
    void reset() noexcept;

    static constexpr int maxModelBlock = 4096;

private:
    NamModel() = default;
    std::unique_ptr<nam::DSP> dsp;
    NamInfo meta;
    juce::File source;

    double devRate = 0.0;
    int maxDevBlock = 0, latency = 0;
    bool resampling = false, antiAlias = false;
    double speedUp = 1.0, speedDown = 1.0;   // input samples per output sample of each interpolator
    juce::LagrangeInterpolator up, down;
    Biquad aa1, aa2;
    std::vector<float> inBuf, outBuf, modelIn, modelOut;
    int inCount = 0, outCount = 0;
};
} // namespace ff::fx
