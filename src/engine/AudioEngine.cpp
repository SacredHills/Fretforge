#include "AudioEngine.h"
#include <cmath>

namespace ff
{
void LevelMeter::update (const float* data, int n) noexcept
{
    if (data == nullptr || n <= 0)
        return;
    float pk = 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (data[i]);
        pk = juce::jmax (pk, a);
        sum += (double) data[i] * data[i];
    }
    if (pk > peak.load (std::memory_order_relaxed))
        peak.store (pk, std::memory_order_relaxed);
    rms.store ((float) std::sqrt (sum / n), std::memory_order_relaxed);
    if (pk >= 0.999f)
        clipped.store (true, std::memory_order_relaxed);
}

AudioEngine::AudioEngine()
{
    // Sized once, never resized: up to ~1 s at 192 kHz, so the analysis thread can never
    // observe a reallocation while the audio thread is pushing.
    analysis.prepare (1 << 18);
    retired.prepare (1024);
    retiredTabs.prepare (256);
}

AudioEngine::~AudioEngine()
{
    delete pendingGraph.exchange (nullptr);
    delete pendingTab.exchange (nullptr);
    collectGarbage();
    delete graph;
    delete tabSeq;
}

void AudioEngine::submitTabSequence (std::unique_ptr<TabSequence> seq)
{
    delete pendingTab.exchange (seq.release());
}

void AudioEngine::renderTab (float* const* bus, int numBus, int offset, int len) noexcept
{
    const double sr = sampleRate.load (std::memory_order_relaxed);
    if (numBus == 0 || sr <= 0.0)
        return;
    float* L = scratchL.data();
    float* R = scratchR.data();
    juce::FloatVectorOperations::clear (L, len);
    juce::FloatVectorOperations::clear (R, len);

    if (tabNeedsSeek && tabSeq != nullptr)
    {
        const double t = (double) pos / sr - 1.0e-9;
        const auto& notes = tabSeq->notes;
        tabNext = (size_t) (std::lower_bound (notes.begin(), notes.end(), t,
                                              [] (const SynthNote& n, double v) { return n.startSec < v; }) - notes.begin());
        tabNeedsSeek = false;
    }

    if (tabNeedsSeekClicks && tabSeq != nullptr)
    {
        const double t = (double) pos / sr - 1.0e-9;
        const auto& c = tabSeq->clicks;
        tabNextClick = (size_t) (std::lower_bound (c.begin(), c.end(), t,
                                                   [] (const TabSequence::Click& k, double v) { return k.sec < v; }) - c.begin());
        tabNeedsSeekClicks = false;
    }

    // score clicks (follow meter and tempo changes) — rendered straight into the bus
    if (playing && tabSeq != nullptr && clickWhilePlaying.load())
    {
        float* out[2] = { bus[0] + offset, numBus > 1 ? bus[1] + offset : nullptr };
        const double t1 = (double) (pos + len) / sr;
        const auto& c = tabSeq->clicks;
        int ccur = 0;
        while (tabNextClick < c.size() && c[tabNextClick].sec < t1)
        {
            const int off = juce::jlimit (ccur, len, (int) (std::llround (c[tabNextClick].sec * sr) - pos));
            float* seg[2] = { out[0] + ccur, out[1] != nullptr ? out[1] + ccur : nullptr };
            metronome.renderTriggered (seg, numBus, off - ccur);
            ccur = off;
            metronome.triggerClick (c[tabNextClick].accent);
            ++tabNextClick;
        }
        float* seg[2] = { out[0] + ccur, out[1] != nullptr ? out[1] + ccur : nullptr };
        metronome.renderTriggered (seg, numBus, len - ccur);
    }
    else if (playing && tabSeq != nullptr)
    {
        const double t1 = (double) (pos + len) / sr;
        while (tabNextClick < tabSeq->clicks.size() && tabSeq->clicks[tabNextClick].sec < t1) ++tabNextClick;
    }

    int cursor = 0;
    if (playing && tabSeq != nullptr)
    {
        const double t1 = (double) (pos + len) / sr;
        const auto& notes = tabSeq->notes;
        while (tabNext < notes.size() && notes[tabNext].startSec < t1)
        {
            const int off = juce::jlimit (cursor, len, (int) (std::llround (notes[tabNext].startSec * sr) - pos));
            synth.render (L + cursor, R + cursor, off - cursor);
            cursor = off;
            synth.start (notes[tabNext]);
            ++tabNext;
        }
    }
    synth.render (L + cursor, R + cursor, len - cursor);

    juce::FloatVectorOperations::add (bus[0] + offset, L, len);
    juce::FloatVectorOperations::add ((numBus > 1 ? bus[1] : bus[0]) + offset, R, len);
}

void AudioEngine::setTimeSignature (int num, int den) noexcept
{
    metronome.beatsPerBar = juce::jlimit (1, MetronomeDsp::maxBeats, num);
    metronome.beatUnit = juce::jlimit (1, 32, den);
}

void AudioEngine::setRecording (bool shouldRecord, juce::int64 countInSamples) noexcept
{
    if (shouldRecord && ! playRequested.load())
    {
        countInRequest = juce::jmax ((juce::int64) 0, countInSamples);
        recordRequested = true;
        playRequested = true;
    }
    else
    {
        recordRequested = shouldRecord;
    }
}

void AudioEngine::submitGraph (std::unique_ptr<PlaybackGraph> g)
{
    delete pendingGraph.exchange (g.release()); // a snapshot never seen by the audio thread
}

void AudioEngine::collectGarbage()
{
    PlaybackGraph* old[64];
    int n;
    while ((n = retired.pop (old, 64)) > 0)
        for (int i = 0; i < n; ++i)
            delete old[i];
    TabSequence* oldTabs[32];
    while ((n = retiredTabs.pop (oldTabs, 32)) > 0)
        for (int i = 0; i < n; ++i)
            delete oldTabs[i];
}

void AudioEngine::prepare (double sr, int maximumBlockSize)
{
    sampleRate = sr;
    blockSize = maximumBlockSize;
    metronome.prepare (sr);
    synth.prepare (sr);
    tabNeedsSeek = tabNeedsSeekClicks = true;
    smoothedGain.reset (sr, 0.02);
    smoothedGain.setCurrentAndTargetValue (masterGain.load());
    const int scratch = juce::jmax (maximumBlockSize, 4096);
    scratchL.assign ((size_t) scratch, 0.0f);
    scratchR.assign ((size_t) scratch, 0.0f);
    silence.assign ((size_t) scratch, 0.0f);
    playing = false;
    practiceRunning = false;
    playingState = false;
    countingIn = false;
    countInRemaining = 0;
}

double AudioEngine::beatsAt (juce::int64 samplePos) const noexcept
{
    const double sr = sampleRate.load (std::memory_order_relaxed);
    if (sr <= 0.0)
        return 0.0;
    return (double) samplePos / sr * metronome.bpm.load() / 60.0 * metronome.beatUnit.load() / 4.0;
}

void AudioEngine::closeRecordSegments() noexcept
{
    if (graph != nullptr)
        for (auto& t : graph->tracks)
            if (t.recorder != nullptr)
                t.recorder->closeSegment();
}

void AudioEngine::renderSegment (const float* const* inputs, int numInputs, float* const* bus, int numBus,
                                 int offset, int len, bool rolling, bool recordingNow) noexcept
{
    if (graph == nullptr || numBus == 0)
        return;

    auto input = [&] (int ch) -> const float* {
        return (ch >= 0 && ch < numInputs && inputs[ch] != nullptr) ? inputs[ch] + offset : nullptr;
    };

    float* sL = scratchL.data();
    float* sR = scratchR.data();
    for (auto& t : graph->tracks)
    {
        juce::FloatVectorOperations::clear (sL, len);
        juce::FloatVectorOperations::clear (sR, len);

        if (rolling)
            renderTrackClips (t, pos, len, sL, sR);

        const float* inL = input (t.inputLeft);
        const float* inR = t.inputRight >= 0 ? input (t.inputRight) : inL;

        if (t.monitor && inL != nullptr)
        {
            juce::FloatVectorOperations::add (sL, inL, len);
            juce::FloatVectorOperations::add (sR, inR != nullptr ? inR : inL, len);
        }

        if (recordingNow && t.recorder != nullptr && inL != nullptr)
            t.recorder->push (inL, t.recorder->numChannels > 1 ? inR : nullptr, len, pos);

        auto& meter = trackMeters[(size_t) juce::jlimit (0, timeline::maxTracks - 1, t.meterSlot)];
        if (! t.audible)
        {
            meter.rms.store (0.0f, std::memory_order_relaxed);
            continue;
        }
        juce::FloatVectorOperations::multiply (sL, t.gainL, len);
        juce::FloatVectorOperations::multiply (sR, t.gainR, len);
        meter.update (sL, len);
        meter.update (sR, len);

        juce::FloatVectorOperations::add (bus[0] + offset, sL, len);
        if (numBus > 1)
            juce::FloatVectorOperations::add (bus[1] + offset, sR, len);
        else
            juce::FloatVectorOperations::add (bus[0] + offset, sR, len);
    }
}

void AudioEngine::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                           int numSamples) noexcept
{
    const double sr = sampleRate.load (std::memory_order_relaxed);

    // ---- pick up a new arrangement snapshot ----
    if (auto* g = pendingGraph.exchange (nullptr))
    {
        if (graph != nullptr && retired.push (&graph, 1) == 0)
        {
            // FIFO full (message thread stalled): leaking is safer than freeing here.
        }
        graph = g;
    }
    if (auto* t = pendingTab.exchange (nullptr))
    {
        if (tabSeq != nullptr && retiredTabs.push (&tabSeq, 1) == 0) {}
        tabSeq = t;
        tabNeedsSeek = tabNeedsSeekClicks = true;
    }
    const bool tab = tabMode.load();

    // ---- transport commands ----
    const auto loc = locateRequest.exchange (-1);
    if (loc >= 0)
    {
        pos = loc;
        closeRecordSegments();
        tabNeedsSeek = tabNeedsSeekClicks = true;
        synth.allNotesOff();
        if (playing && countInRemaining == 0)
            metronome.resetAtBeat (beatsAt (pos));
    }

    const bool wantPlay = playRequested.load();
    if (wantPlay && ! playing)
    {
        playing = true;
        tabNeedsSeek = tabNeedsSeekClicks = true;
        countInRemaining = countInRequest.exchange (0);
        if (countInRemaining > 0)
            metronome.reset();
        else
            metronome.resetAtBeat (beatsAt (pos));
    }
    else if (! wantPlay && playing)
    {
        playing = false;
        countInRemaining = 0;
        synth.allNotesOff();
    }

    const bool practice = practiceMetronome.load();
    if (practice && ! practiceRunning && ! playing)
        metronome.reset();
    practiceRunning = practice;

    activeInputs.store (numInputs, std::memory_order_relaxed);
    activeOutputs.store (numOutputs, std::memory_order_relaxed);

    // ---- analysis tap & input meter ----
    const int ach = analysisInputChannel.load (std::memory_order_relaxed);
    if (ach >= 0 && ach < numInputs && inputs[ach] != nullptr)
    {
        inputMeter.update (inputs[ach], numSamples);
        analysis.push (inputs[ach], numSamples);
    }
    else
    {
        inputMeter.rms.store (0.0f, std::memory_order_relaxed);
    }

    // ---- output bus ----
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    float* bus[2] = { nullptr, nullptr };
    int numBus = 0;
    for (int i = 0; i < numOutputs && numBus < 2; ++i)
        if (outputs[i] != nullptr)
            bus[numBus++] = outputs[i];

    const bool recordWanted = recordRequested.load();
    const int scratchSize = (int) scratchL.size();

    int done = 0;
    while (done < numSamples)
    {
        int len = juce::jmin (numSamples - done, scratchSize);
        float* clickOut[2] = { numBus > 0 ? bus[0] + done : nullptr, numBus > 1 ? bus[1] + done : nullptr };

        if (playing && countInRemaining > 0)
        {
            len = (int) juce::jmin ((juce::int64) len, countInRemaining);
            renderSegment (inputs, numInputs, bus, numBus, done, len, false, false); // live monitoring only
            if (numBus > 0)
                metronome.process (clickOut, numBus, len);
            countInRemaining -= len;
            if (countInRemaining == 0)
                metronome.resetAtBeat (beatsAt (pos));
            done += len;
            continue;
        }

        const auto loopS = (juce::int64) (loopStartSec.load() * sr);
        const auto loopE = (juce::int64) (loopEndSec.load() * sr);
        const bool rangeValid = loopE > loopS + 64;
        const bool looping = playing && loopEnabled.load() && rangeValid && pos < loopE;
        const bool punch = punchEnabled.load() && rangeValid;
        if (looping)
            len = (int) juce::jmin ((juce::int64) len, loopE - pos);
        if (playing && punch)
        {
            if (pos < loopS && pos + len > loopS) len = (int) (loopS - pos);
            else if (pos < loopE && pos + len > loopE) len = (int) (loopE - pos);
        }

        const bool inPunch = ! punch || (pos >= loopS && pos < loopE);
        const bool recordingNow = playing && recordWanted && inPunch;
        if (wasRecording && ! recordingNow)
            closeRecordSegments();
        wasRecording = recordingNow;

        renderSegment (inputs, numInputs, bus, numBus, done, len, playing && ! tab, recordingNow && ! tab);
        if (tab)
            renderTab (bus, numBus, done, len);

        if (numBus > 0 && ((playing && clickWhilePlaying.load() && ! tab) || (! playing && practice)))
            metronome.process (clickOut, numBus, len);

        if (playing)
        {
            pos += len;
            if (looping && pos >= loopE)
            {
                pos = loopS;
                closeRecordSegments();
                metronome.resetAtBeat (beatsAt (pos));
                tabNeedsSeek = tabNeedsSeekClicks = true;
                synth.allNotesOff();
            }
        }
        done += len;
    }

    recordAck.store (playing && recordWanted, std::memory_order_relaxed);
    countingIn.store (playing && countInRemaining > 0, std::memory_order_relaxed);
    playingState.store (playing, std::memory_order_relaxed);

    // ---- master gain & meters ----
    smoothedGain.setTargetValue (juce::jlimit (0.0f, 2.0f, masterGain.load (std::memory_order_relaxed)));
    if (smoothedGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = smoothedGain.getNextValue();
            for (int c = 0; c < numBus; ++c)
                bus[c][i] *= g;
        }
    }
    else
    {
        const float g = smoothedGain.getTargetValue();
        for (int c = 0; c < numBus; ++c)
            juce::FloatVectorOperations::multiply (bus[c], g, numSamples);
    }

    if (numBus > 0) outputMeterL.update (bus[0], numSamples);
    if (numBus > 1) outputMeterR.update (bus[1], numSamples);
    else if (numBus > 0) outputMeterR.update (bus[0], numSamples);

    positionSamples.store (pos, std::memory_order_relaxed);
    if (sr > 0.0)
    {
        const double tempo = metronome.rampEnabled.load() ? metronome.effectiveBpm.load() : metronome.bpm.load();
        positionPpq.store ((double) pos / sr * tempo / 60.0, std::memory_order_relaxed);
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                                    int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    process (inputs, numInputs, outputs, numOutputs, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
    playing = false;
    playingState = false;
    recordAck = false;
    closeRecordSegments();
}

void AudioEngine::audioDeviceError (const juce::String&)
{
    deviceErrorCounter.fetch_add (1);
}
} // namespace ff
