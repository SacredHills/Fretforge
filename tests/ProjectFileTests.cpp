#include <juce_core/juce_core.h>
#include "core/ProjectFile.h"

using namespace ff::project;

struct ProjectFileTests : juce::UnitTest
{
    ProjectFileTests() : juce::UnitTest ("Project files", "core") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("ff_tests_" + juce::String (juce::Random::getSystemRandom().nextInt()));
        tmp.createDirectory();

        beginTest ("JSON round trip is lossless");
        {
            auto p = createDefault ("Riff Ideas");
            p.getChildWithName (ids::TRANSPORT).setProperty (ids::bpm, 187.5, nullptr);
            juce::ValueTree track ("TRACK");
            track.setProperty ("id", "t-1", nullptr);
            track.setProperty ("name", "Rhythm L", nullptr);
            track.setProperty ("muted", true, nullptr);
            track.setProperty ("blob", juce::var (juce::MemoryBlock ("\x01\x02\x03", 3)), nullptr);
            p.getChildWithName (ids::TRACKS).appendChild (track, nullptr);

            auto r = parse (serialise (p));
            expect (r.result.wasOk(), r.result.getErrorMessage());
            expect (r.tree.isEquivalentTo (p), "tree changed in round trip");
            expect (r.warnings.isEmpty(), r.warnings.joinIntoString ("; "));
        }

        beginTest ("a track's amp rig and plugins survive saving and loading");
        {
            auto p = createDefault ("Amp");
            juce::ValueTree track ("TRACK");
            track.setProperty ("id", "t-2", nullptr);
            juce::ValueTree rig ("RIG");
            rig.setProperty ("enabled", true, nullptr);
            rig.setProperty ("namFile", "/x/amp.nam", nullptr);
            juce::ValueTree plugin ("PLUGIN");
            plugin.setProperty ("slot", 0, nullptr);
            rig.appendChild (plugin, nullptr);
            track.appendChild (rig, nullptr);
            juce::ValueTree inserts ("INSERTS");
            inserts.appendChild (plugin.createCopy(), nullptr);
            track.appendChild (inserts, nullptr);
            juce::ValueTree badClip ("CLIP");   // still repaired
            track.appendChild (badClip, nullptr);
            p.getChildWithName (ids::TRACKS).appendChild (track, nullptr);

            auto r = parse (serialise (p));
            const auto t = r.tree.getChildWithName (ids::TRACKS).getChild (0);
            expect (t.getChildWithName ("RIG").isValid(), "the amp rig was dropped");
            expectEquals (t.getChildWithName ("RIG")["namFile"].toString(), juce::String ("/x/amp.nam"));
            expect (t.getChildWithName ("RIG").getChildWithName ("PLUGIN").isValid());
            expect (t.getChildWithName ("INSERTS").getChildWithName ("PLUGIN").isValid());
            expect (! t.getChildWithName ("CLIP").isValid(), "the broken clip is still removed");
        }

        beginTest ("rejects garbage, foreign and future files");
        {
            expect (parse ("{ not json").result.failed());
            expect (parse ("{\"format\":\"Other\",\"formatVersion\":1}").result.failed());
            auto future = serialise (createDefault ("x")).replace ("\"formatVersion\": 1", "\"formatVersion\": 99");
            auto r = parse (future);
            expect (r.result.failed() && r.result.getErrorMessage().contains ("newer"), r.result.getErrorMessage());
        }

        beginTest ("validation repairs missing and out-of-range data");
        {
            auto p = createDefault ("x");
            p.removeChild (p.getChildWithName (ids::METRONOME), nullptr);
            p.getChildWithName (ids::TRANSPORT).setProperty (ids::bpm, 5000.0, nullptr);
            p.getChildWithName (ids::TRANSPORT).setProperty (ids::tsDen, 7, nullptr);
            auto r = parse (serialise (p));
            expect (r.result.wasOk());
            expect (r.tree.getChildWithName (ids::METRONOME).isValid());
            expectEquals ((double) r.tree.getChildWithName (ids::TRANSPORT)[ids::bpm], 999.0);
            expectEquals ((int) r.tree.getChildWithName (ids::TRANSPORT)[ids::tsDen], 4);
            expect (r.warnings.size() >= 3);
        }

        beginTest ("bundle save/load, backups rotate, autosave");
        {
            auto bundle = Bundle::normaliseBundlePath (tmp.getChildFile ("Song"));
            expect (bundle.getFileName() == "Song.ffproj");
            auto p = createDefault ("Song");
            for (int i = 0; i < 5; ++i)
            {
                p.getChildWithName (ids::TRANSPORT).setProperty (ids::bpm, 100.0 + i, nullptr);
                const auto res = Bundle::save (p, bundle, 3);
                expect (res.wasOk(), res.getErrorMessage());
                juce::Thread::sleep (1100); // distinct backup timestamps
            }
            for (auto& f : Bundle::mediaFolders())
                expect (bundle.getChildFile (f).isDirectory(), f);
            expectEquals (Bundle::backupsDir (bundle).findChildFiles (juce::File::findFiles, false, "*.ffp").size(), 3);

            auto r = Bundle::load (bundle);
            expect (r.result.wasOk());
            expectEquals ((double) r.tree.getChildWithName (ids::TRANSPORT)[ids::bpm], 104.0);

            expect (! Bundle::hasNewerAutosave (bundle));
            p.setProperty (ids::notes, "unsaved idea", nullptr);
            juce::Thread::sleep (1100);
            expect (Bundle::writeAutosave (p, bundle).wasOk());
            expect (Bundle::hasNewerAutosave (bundle));
            auto a = Bundle::loadAutosave (bundle);
            expectEquals (a.tree[ids::notes].toString(), juce::String ("unsaved idea"));
            expect (Bundle::save (p, bundle).wasOk());
            expect (! Bundle::autosaveFile (bundle).exists(), "save should discard autosave");

            expect (! tmp.getChildFile ("NotAProject").isDirectory() || ! Bundle::isBundle (tmp.getChildFile ("NotAProject")));
            expect (Bundle::load (tmp.getChildFile ("Missing.ffproj")).result.failed());
        }

        beginTest ("atomic write leaves no temp files and preserves content");
        {
            auto f = tmp.getChildFile ("atomic.txt");
            expect (writeFileAtomically (f, "first").wasOk());
            expect (writeFileAtomically (f, "second").wasOk());
            expectEquals (f.loadFileAsString(), juce::String ("second"));
            expectEquals (tmp.findChildFiles (juce::File::findFiles, false, "*.tmp").size(), 0);
            expectEquals (tmp.findChildFiles (juce::File::findFiles, false, ".*").size(), 0);
        }

        beginTest ("portable media paths");
        {
            auto bundle = tmp.getChildFile ("P.ffproj");
            auto media = bundle.getChildFile ("Audio").getChildFile ("take 1.wav");
            expectEquals (Bundle::toPortablePath (bundle, media), juce::String ("Audio/take 1.wav"));
            expect (Bundle::fromPortablePath (bundle, "Audio/take 1.wav") == media);
            expect (Bundle::toPortablePath (bundle, tmp.getChildFile ("outside.wav")).isEmpty());
            expect (Bundle::fromPortablePath (bundle, "../escape.wav") == juce::File());
            expect (Bundle::fromPortablePath (bundle, "/etc/passwd") == juce::File());
            expect (Bundle::fromPortablePath (bundle, "C:/x.wav") == juce::File());
        }

        tmp.deleteRecursively();
    }
};
static ProjectFileTests projectFileTests;
