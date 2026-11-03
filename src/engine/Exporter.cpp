#include "Exporter.h"
#include "core/Timeline.h"
#include "core/RigModel.h"
#include "fx/RigBank.h"

namespace ff
{
namespace
{
    juce::Result writeFile (const juce::File& file, const juce::AudioBuffer<float>& audio, const ExportSettings& s)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        if (out == nullptr)
            return juce::Result::fail ("Cannot create " + file.getFullPathName());

        std::unique_ptr<juce::AudioFormat> fmt;
        if (s.format == ExportSettings::Format::flac) fmt = std::make_unique<juce::FlacAudioFormat>();
        else fmt = std::make_unique<juce::WavAudioFormat>();

        const int bits = (s.format == ExportSettings::Format::flac) ? juce::jmin (24, s.bitDepth) : s.bitDepth;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt->createWriterFor (out.get(), s.sampleRate, (unsigned int) audio.getNumChannels(),
                                                                          bits, {}, 0));
        if (w == nullptr)
            return juce::Result::fail ("The " + fmt->getFormatName() + " writer does not support " + juce::String (bits) + "-bit output.");
        out.release();
        if (! w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return juce::Result::fail ("Writing " + file.getFileName() + " failed (disk full?).");
        return juce::Result::ok();
    }

    void ditherTo16 (juce::AudioBuffer<float>& b)
    {
        juce::Random rnd (0x5eed);
        const float lsb = 1.0f / 32768.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] += (rnd.nextFloat() - rnd.nextFloat()) * lsb; // TPDF
        }
    }
}

ExportResult exportAudio (const juce::ValueTree& tracks, const juce::File& mediaBundle, AudioCache& cache,
                          const ExportSettings& s, std::function<bool (double)> progress)
{
    ExportResult res;
    if (s.endSec <= s.startSec)
    {
        res.result = juce::Result::fail ("Nothing to export: the range is empty (add or record some audio first).");
        return res;
    }
    if (s.sampleRate < 8000.0 || s.sampleRate > 384000.0)
    {
        res.result = juce::Result::fail ("Unsupported sample rate.");
        return res;
    }

    GraphBuildOptions opt;
    opt.sampleRate = s.sampleRate;
    opt.waitForAudio = true;
    juce::StringArray rigWarnings;
    opt.rigFor = [&] (const juce::ValueTree& t) -> std::shared_ptr<fx::AmpRig> {
        const auto r = rig::find (t);
        return rig::isEnabled (r) ? fx::makeOfflineRig (r, s.sampleRate, 4096, rigWarnings) : nullptr;
    };
    juce::ScopedNoDenormals noDenormals;
    auto graph = buildPlaybackGraph (tracks, mediaBundle, cache, opt);
    if (graph->missingClips > 0)
        res.warnings.add (juce::String (graph->missingClips) + " clip(s) could not be loaded and are silent in the export.");
    res.warnings.addArray (rigWarnings);

    const auto start = (juce::int64) std::llround (s.startSec * s.sampleRate);
    const auto total = (juce::int64) std::llround ((s.endSec - s.startSec) * s.sampleRate);
    if (total > (juce::int64) std::numeric_limits<int>::max())
    {
        res.result = juce::Result::fail ("Export range is too long.");
        return res;
    }

    // Which renders: one mix, or one per track.
    struct Job { juce::String name; std::vector<int> trackIndices; };
    std::vector<Job> jobs;
    if (s.stems)
    {
        int i = 0;
        for (const auto& t : tracks)
        {
            if (t.hasType (timeline::ids::TRACK) && i < (int) graph->tracks.size())
            {
                if (graph->tracks[(size_t) i].audible && ! graph->tracks[(size_t) i].pieces.empty())
                    jobs.push_back ({ juce::File::createLegalFileName (t[timeline::ids::name].toString()), { i } });
                ++i;
            }
        }
        if (jobs.empty())
        {
            res.result = juce::Result::fail ("No audible tracks with audio to export as stems.");
            return res;
        }
        if (! s.destination.createDirectory())
        {
            res.result = juce::Result::fail ("Cannot create folder " + s.destination.getFullPathName());
            return res;
        }
    }
    else
    {
        Job j { s.baseName, {} };
        for (int i = 0; i < (int) graph->tracks.size(); ++i)
            if (graph->tracks[(size_t) i].audible)
                j.trackIndices.push_back (i);
        jobs.push_back (j);
    }

    const int block = 4096;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    juce::StringArray usedNames;

    for (size_t ji = 0; ji < jobs.size(); ++ji)
    {
        const auto& job = jobs[ji];
        juce::AudioBuffer<float> out (2, (int) total);
        out.clear();
        for (int ti : job.trackIndices)
            if (auto& rg = graph->tracks[(size_t) ti].rig) rg->reset();   // every render starts from silence

        for (juce::int64 done = 0; done < total; done += block)
        {
            const int n = (int) juce::jmin ((juce::int64) block, total - done);
            for (int ti : job.trackIndices)
            {
                const auto& t = graph->tracks[(size_t) ti];
                std::fill (l.begin(), l.begin() + n, 0.0f);
                std::fill (r.begin(), r.begin() + n, 0.0f);
                renderTrackClips (t, start + done, n, l.data(), r.data());
                if (t.rig != nullptr) t.rig->process (l.data(), r.data(), n);
                out.addFrom (0, (int) done, l.data(), n, t.gainL);
                out.addFrom (1, (int) done, r.data(), n, t.gainR);
            }
            if (progress && ! progress (((double) ji + (double) done / (double) total) / (double) jobs.size()))
            {
                res.result = juce::Result::fail ("Export cancelled.");
                return res;
            }
        }

        const float peak = out.getMagnitude (0, out.getNumSamples());
        if (s.normalize && peak > 0.0f)
            out.applyGain (juce::Decibels::decibelsToGain ((float) s.normalizeDb) / peak);
        else if (peak > 1.0f && s.bitDepth != 32)
            res.warnings.add (job.name + ": peaks reach " + juce::String (juce::Decibels::gainToDecibels (peak), 1)
                              + " dBFS and will clip. Enable normalisation or lower the faders.");
        if (s.bitDepth == 16)
            ditherTo16 (out);

        juce::File file;
        if (s.stems)
        {
            auto name = s.baseName + " - " + job.name;
            while (usedNames.contains (name)) name += "_";
            usedNames.add (name);
            file = s.destination.getChildFile (name + s.extension());
        }
        else
        {
            file = s.destination.withFileExtension (s.extension());
        }

        auto wr = writeFile (file, out, s);
        if (wr.failed())
        {
            res.result = wr;
            return res;
        }
        res.files.add (file);
    }
    if (progress) progress (1.0);
    return res;
}
} // namespace ff
