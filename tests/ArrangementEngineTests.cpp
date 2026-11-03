#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "core/ProjectFile.h"
#include "core/Timeline.h"
#include <cmath>

using namespace ff;
namespace tl = ff::timeline;

namespace
{
    float signalAt (int i) { return (float) ((i % 97) - 48) / 64.0f; } // deterministic, non-periodic in 2^n

    juce::File writeWav (const juce::File& f, double sr, int numSamples, std::function<float (int)> fn, int channels = 1)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::AudioBuffer<float> b (channels, numSamples);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < numSamples; ++i)
                b.setSample (c, i, fn (i) * (c == 0 ? 1.0f : -1.0f));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), sr, (unsigned) channels, 32, {}, 0));
        out.release();
        w->writeFromAudioSampleBuffer (b, 0, numSamples);
        return f;
    }

    juce::AudioBuffer<float> readFile (const juce::File& f, double* srOut = nullptr)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        if (srOut) *srOut = r->sampleRate;
        return b;
    }

    struct Rig
    {
        juce::File dir;
        juce::ValueTree project = ff::project::createDefault ("T");
        juce::ValueTree tracks = project.getChildWithName (ff::project::ids::TRACKS);
        AudioCache cache;

        Rig()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ff_arr_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) + ".ffproj");
            ff::project::Bundle::createStructure (dir);
        }
        ~Rig() { dir.deleteRecursively(); }

        juce::ValueTree addTrack (const juce::String& name)
        {
            auto t = tl::createTrack (name, tracks.getNumChildren());
            tracks.appendChild (t, nullptr);
            return t;
        }
        juce::ValueTree addClip (juce::ValueTree t, const juce::String& file, double start, double len, double offset, bool noFades = true)
        {
            auto c = tl::createClip (file, file, start, len, offset);
            if (noFades) { c.setProperty (tl::ids::fadeIn, 0.0, nullptr); c.setProperty (tl::ids::fadeOut, 0.0, nullptr); }
            t.appendChild (c, nullptr);
            return c;
        }
        std::unique_ptr<PlaybackGraph> graph (double sr, const std::map<juce::String, std::shared_ptr<TrackRecorder>>* rec = nullptr)
        {
            GraphBuildOptions o;
            o.sampleRate = sr;
            o.waitForAudio = true;
            o.recorders = rec;
            return buildPlaybackGraph (tracks, dir, cache, o);
        }
    };

    struct Runner
    {
        AudioEngine& e;
        int n;
        std::vector<float> in, l, r;
        std::vector<float> capturedL;
        Runner (AudioEngine& eng, int block) : e (eng), n (block), in ((size_t) block), l ((size_t) block), r ((size_t) block) {}

        void run (int blocks, std::function<float (juce::int64)> input = {})
        {
            for (int b = 0; b < blocks; ++b)
            {
                const auto base = (juce::int64) capturedL.size();
                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = input ? input (base + i) : 0.0f;
                const float* ins[1] = { in.data() };
                float* outs[2] = { l.data(), r.data() };
                e.process (ins, 1, outs, 2, n);
                capturedL.insert (capturedL.end(), l.begin(), l.end());
            }
        }
    };
}

struct ArrangementEngineTests : juce::UnitTest
{
    ArrangementEngineTests() : juce::UnitTest ("Arrangement engine", "engine") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("clip renders sample-exactly at its timeline position");
        {
            Rig rig;
            writeWav (rig.dir.getChildFile ("Audio/a.wav"), sr, 96000, signalAt);
            auto t = rig.addTrack ("Gtr");
            rig.addClip (t, "Audio/a.wav", 0.5, 1.0, 0.1);
            auto g = rig.graph (sr);
            expectEquals ((int) g->tracks.size(), 1);
            expectEquals (g->missingClips, 0);

            std::vector<float> l (96000, 0.0f), r (96000, 0.0f);
            renderTrackClips (g->tracks[0], 0, 96000, l.data(), r.data());
            bool exact = true;
            for (int k = 0; k < 48000; ++k)
                exact = exact && std::abs (l[(size_t) (24000 + k)] - signalAt (4800 + k)) < 1.0e-6f;
            expect (exact, "clip content differs");
            expectEquals (l[23999], 0.0f);
            expectEquals (l[72000], 0.0f);
            float pl, pr;
            panGains (0.0f, 1.0f, pl, pr);
            expectWithinAbsoluteError (pl, 1.0f, 1e-6f);
            expectWithinAbsoluteError (pr, 1.0f, 1e-6f);
        }

        beginTest ("sample-rate conversion keeps pitch (44.1 kHz file in a 48 kHz session)");
        {
            Rig rig;
            writeWav (rig.dir.getChildFile ("Audio/s.wav"), 44100.0, 44100,
                      [] (int i) { return (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 44100.0) * 0.5f; });
            auto t = rig.addTrack ("Gtr");
            rig.addClip (t, "Audio/s.wav", 0.0, 1.0, 0.0);
            auto g = rig.graph (sr);
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            renderTrackClips (g->tracks[0], 0, 48000, l.data(), r.data());
            int crossings = 0;
            for (int i = 1; i < 48000; ++i)
                if ((l[(size_t) i - 1] < 0.0f) != (l[(size_t) i] < 0.0f)) ++crossings;
            expect (std::abs (crossings - 880) <= 2, juce::String (crossings));
            // interpolation error vs. an ideal 440 Hz sine
            double err = 0.0;
            for (int i = 100; i < 47000; ++i)
                err = juce::jmax (err, std::abs ((double) l[(size_t) i] - 0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr)));
            expect (err < 0.002, "interpolation error " + juce::String (err));
        }

        beginTest ("engine plays the graph, honours mute/solo, gain and pan");
        {
            Rig rig;
            writeWav (rig.dir.getChildFile ("Audio/a.wav"), sr, 48000, [] (int) { return 0.25f; });
            auto t1 = rig.addTrack ("A");
            auto t2 = rig.addTrack ("B");
            rig.addClip (t1, "Audio/a.wav", 0.0, 1.0, 0.0);
            rig.addClip (t2, "Audio/a.wav", 0.0, 1.0, 0.0);

            AudioEngine e;
            e.prepare (sr, 256);
            e.submitGraph (rig.graph (sr));
            Runner run (e, 256);
            e.play();
            run.run (4);
            expectWithinAbsoluteError (run.capturedL[512], 0.5f, 1e-5f);     // two tracks summed

            t2.setProperty (tl::ids::mute, true, nullptr);
            e.submitGraph (rig.graph (sr));
            run.run (1);
            expectWithinAbsoluteError (run.capturedL.back(), 0.25f, 1e-5f);

            t2.setProperty (tl::ids::mute, false, nullptr);
            t2.setProperty (tl::ids::solo, true, nullptr);
            t2.setProperty (tl::ids::gainDb, -6.0206, nullptr);
            t2.setProperty (tl::ids::pan, 1.0, nullptr);       // hard right -> nothing on the left
            e.submitGraph (rig.graph (sr));
            run.run (1);
            expectWithinAbsoluteError (run.capturedL.back(), 0.0f, 1e-5f);
            expectWithinAbsoluteError (run.r.back(), 0.25f * 0.5f * 1.41421356f, 1e-4f);
            e.collectGarbage();
        }

        beginTest ("loop region wraps the playhead and the click");
        {
            AudioEngine e;
            e.prepare (sr, 480);
            e.loopStartSec = 0.5;
            e.loopEndSec = 1.0;
            e.loopEnabled = true;
            Runner run (e, 480);
            e.play();
            run.run (200); // 2 s
            const auto p = e.getPositionSamples();
            expect (p >= 24000 && p < 48000, juce::String (p));
            expectEquals (p, (juce::int64) (24000 + (96000 - 48000) % 24000));
        }

        beginTest ("recording: loop passes become segments, file matches the input");
        {
            Rig rig;
            auto t = rig.addTrack ("Gtr");
            juce::String err;
            auto rec = TrackRecorder::create (rig.dir.getChildFile ("Audio/rec.wav"), 1, 0, -1, sr, 24, err);
            expect (rec != nullptr, err);
            std::map<juce::String, std::shared_ptr<TrackRecorder>> recs { { t[tl::ids::id].toString(), rec } };

            AudioEngine e;
            e.prepare (sr, 256);
            e.loopStartSec = 0.0;
            e.loopEndSec = 0.5;
            e.loopEnabled = true;
            e.submitGraph (rig.graph (sr, &recs));
            Runner run (e, 256);
            e.setRecording (true);
            run.run (375, [] (juce::int64 i) { return signalAt ((int) i) * 0.5f; }); // 96000 samples = 4 passes
            expect (e.isRecordingActive());
            e.stop();
            run.run (2);
            expect (! e.isRecordingActive());
            expect (rec->finish().wasOk());

            expectEquals (rec->getNumSegments(), 4);
            for (int s = 0; s < rec->getNumSegments(); ++s)
            {
                expectEquals (rec->segment (s).timelineStart.load(), (juce::int64) 0);
                expectEquals (rec->segment (s).length.load(), (juce::int64) 24000);
                expectEquals (rec->segment (s).fileOffset.load(), (juce::int64) (24000 * s));
            }
            auto b = readFile (rec->file);
            expectEquals (b.getNumSamples(), 96000);
            double maxErr = 0.0;
            for (int i = 0; i < 96000; ++i)
                maxErr = juce::jmax (maxErr, (double) std::abs (b.getSample (0, i) - signalAt (i) * 0.5f));
            expect (maxErr < 1.0e-6, "24-bit file differs from input by " + juce::String (maxErr));
            e.collectGarbage();
        }

        beginTest ("punch in/out records only inside the range");
        {
            Rig rig;
            auto t = rig.addTrack ("Gtr");
            juce::String err;
            auto rec = TrackRecorder::create (rig.dir.getChildFile ("Audio/punch.wav"), 1, 0, -1, sr, 24, err);
            std::map<juce::String, std::shared_ptr<TrackRecorder>> recs { { t[tl::ids::id].toString(), rec } };
            AudioEngine e;
            e.prepare (sr, 1000);
            e.loopStartSec = 1.0;
            e.loopEndSec = 2.0;
            e.punchEnabled = true;
            e.submitGraph (rig.graph (sr, &recs));
            Runner run (e, 1000);
            e.setRecording (true);
            run.run (144); // 3 s
            e.stop();
            run.run (1);
            rec->finish();
            expectEquals (rec->getNumSegments(), 1);
            expectEquals (rec->segment (0).timelineStart.load(), (juce::int64) 48000);
            expectEquals (rec->segment (0).length.load(), (juce::int64) 48000);
            e.collectGarbage();
        }

        beginTest ("count-in holds the playhead and clicks before recording starts");
        {
            AudioEngine e;
            e.prepare (sr, 500);
            e.setRecording (true, 24000);
            Runner run (e, 500);
            run.run (40); // 20000 samples, still counting in
            expect (e.isCountingIn());
            expectEquals (e.getPositionSamples(), (juce::int64) 0);
            float click = 0.0f;
            for (auto v : run.capturedL) click = juce::jmax (click, std::abs (v));
            expect (click > 0.3f, "count-in click missing");
            run.run (20);
            expect (! e.isCountingIn());
            expectEquals (e.getPositionSamples(), (juce::int64) 6000);
        }

        beginTest ("export: mix, stems, normalise, FLAC, 16-bit");
        {
            Rig rig;
            writeWav (rig.dir.getChildFile ("Audio/a.wav"), sr, 48000, [] (int i) { return signalAt (i) * 0.25f; });
            auto t1 = rig.addTrack ("Lead");
            auto t2 = rig.addTrack ("Rhythm");
            rig.addClip (t1, "Audio/a.wav", 0.0, 1.0, 0.0);
            rig.addClip (t2, "Audio/a.wav", 0.5, 0.5, 0.0);

            ExportSettings s;
            s.sampleRate = sr;
            s.bitDepth = 32;
            s.startSec = 0.0;
            s.endSec = tl::songEnd (rig.tracks);
            expectEquals (s.endSec, 1.0);
            s.destination = rig.dir.getChildFile ("Export/mix.wav");
            s.destination.getParentDirectory().createDirectory();
            auto res = exportAudio (rig.tracks, rig.dir, rig.cache, s);
            expect (res.result.wasOk(), res.result.getErrorMessage());
            auto b = readFile (res.files[0]);
            expectEquals (b.getNumChannels(), 2);
            expectEquals (b.getNumSamples(), 48000);
            expectWithinAbsoluteError (b.getSample (0, 1000), signalAt (1000) * 0.25f, 1e-6f);
            expectWithinAbsoluteError (b.getSample (0, 30000), signalAt (30000) * 0.25f + signalAt (6000) * 0.25f, 1e-6f);

            s.normalize = true;
            s.bitDepth = 24;
            s.destination = rig.dir.getChildFile ("Export/norm.wav");
            res = exportAudio (rig.tracks, rig.dir, rig.cache, s);
            b = readFile (res.files[0]);
            expectWithinAbsoluteError (b.getMagnitude (0, b.getNumSamples()), 0.8913f, 0.001f);

            s.stems = true;
            s.normalize = false;
            s.format = ExportSettings::Format::flac;
            s.bitDepth = 16;
            s.destination = rig.dir.getChildFile ("Export/Stems");
            s.baseName = "Song";
            res = exportAudio (rig.tracks, rig.dir, rig.cache, s);
            expect (res.result.wasOk(), res.result.getErrorMessage());
            expectEquals (res.files.size(), 2);
            expectEquals (res.files[1].getFileName(), juce::String ("Song - Rhythm.flac"));
            double fsr = 0;
            b = readFile (res.files[1], &fsr);
            expectEquals (fsr, sr);
            expectWithinAbsoluteError (b.getSample (0, 24000 + 500), signalAt (500) * 0.25f, 2.0f / 32768.0f);
            expectWithinAbsoluteError (b.getSample (0, 1000), 0.0f, 2.0f / 32768.0f);

            s.endSec = s.startSec;
            expect (exportAudio (rig.tracks, rig.dir, rig.cache, s).result.failed());
        }

        beginTest ("graph swap is safe while the engine runs (garbage collected off the audio thread)");
        {
            Rig rig;
            writeWav (rig.dir.getChildFile ("Audio/a.wav"), sr, 4800, [] (int) { return 0.1f; });
            auto t = rig.addTrack ("A");
            rig.addClip (t, "Audio/a.wav", 0.0, 0.1, 0.0);
            AudioEngine e;
            e.prepare (sr, 64);
            Runner run (e, 64);
            e.play();
            for (int i = 0; i < 200; ++i)
            {
                e.submitGraph (rig.graph (sr));
                e.submitGraph (rig.graph (sr)); // replaced before the audio thread saw it
                run.run (1);
                if (i % 10 == 0) e.collectGarbage();
            }
            e.collectGarbage();
            expect (true);
        }
    }
};
static ArrangementEngineTests arrangementEngineTests;
