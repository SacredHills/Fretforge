#pragma once
#include "PlaybackGraph.h"
#include "core/TempoMap.h"

namespace ff
{
struct ExportSettings
{
    enum class Format { wav, flac };
    Format format = Format::wav;
    int bitDepth = 24;              // 16, 24 (wav/flac) or 32 (wav float)
    double sampleRate = 48000.0;
    bool stems = false;             // one file per track instead of a mix
    bool normalize = false;         // peak-normalise to normalizeDb
    double normalizeDb = -1.0;
    double startSec = 0.0, endSec = 0.0;
    juce::File destination;         // mix: the file; stems: the folder
    juce::String baseName = "Mix";
    /** The master bus (its plugins process every render). If invalid, the MASTER node next to `tracks` is used. */
    juce::ValueTree master;
    /** The project's tempo and time signature: what plugins see as the host's tempo while rendering. */
    TempoMap tempoMap;
    int tsNum = 4, tsDen = 4;

    juce::String extension() const { return format == Format::flac ? ".flac" : ".wav"; }
};

struct ExportResult
{
    juce::Result result = juce::Result::ok();
    juce::Array<juce::File> files;
    juce::StringArray warnings;
};

/** Renders the arrangement offline (no audio device) and writes the files. `progress`
    receives 0..1 and may return false to cancel. Safe to call from a background thread. */
ExportResult exportAudio (const juce::ValueTree& tracks, const juce::File& mediaBundle, AudioCache& cache,
                          const ExportSettings& settings, std::function<bool (double)> progress = {});
} // namespace ff
