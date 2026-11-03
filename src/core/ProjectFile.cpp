#include "ProjectFile.h"
#include "TabModel.h"
#include "DrumModel.h"

#ifndef FF_VERSION_STRING
 #define FF_VERSION_STRING "0.0.0"
#endif

namespace ff::project
{
juce::ValueTree createDefault (const juce::String& name)
{
    using namespace ids;
    const auto now = juce::Time::getCurrentTime().toISO8601 (true);

    juce::ValueTree root (PROJECT);
    root.setProperty (ids::name, name, nullptr);
    root.setProperty (created, now, nullptr);
    root.setProperty (modified, now, nullptr);
    root.setProperty (sampleRate, 48000, nullptr);
    root.setProperty (notes, juce::String(), nullptr);

    juce::ValueTree transport (TRANSPORT);
    transport.setProperty (bpm, 120.0, nullptr);
    transport.setProperty (tsNum, 4, nullptr);
    transport.setProperty (tsDen, 4, nullptr);
    transport.setProperty (keySignature, "C major", nullptr);
    transport.setProperty (loopEnabled, false, nullptr);
    transport.setProperty (loopStart, 0.0, nullptr);
    transport.setProperty (loopEnd, 8.0, nullptr);
    transport.setProperty (punchEnabled, false, nullptr);
    root.appendChild (transport, nullptr);

    juce::ValueTree metro (METRONOME);
    metro.setProperty (enabled, false, nullptr);
    metro.setProperty (volume, 0.8, nullptr);
    metro.setProperty (subdivision, 1, nullptr);
    metro.setProperty (sound, 0, nullptr);
    metro.setProperty (accents, "2111111111111111", nullptr);
    metro.setProperty (countInBars, 1, nullptr);
    root.appendChild (metro, nullptr);

    root.appendChild (juce::ValueTree (TRACKS), nullptr);
    root.appendChild (juce::ValueTree (MARKERS), nullptr);

    drums::getOrCreate (root);
    auto score = tab::getOrCreateScore (root);
    tab::addTrack (score, "Guitar", { 40, 45, 50, 55, 59, 64 }, "guitar", nullptr);
    return root;
}

juce::var treeToVar (const juce::ValueTree& tree)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("type", tree.getType().toString());

    auto* props = new juce::DynamicObject();
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto key = tree.getPropertyName (i);
        const auto& value = tree.getProperty (key);
        if (value.isBinaryData())
            props->setProperty (key, "base64:" + value.getBinaryData()->toBase64Encoding());
        else if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool() || value.isString())
            props->setProperty (key, value);
        else
            props->setProperty (key, value.toString());
    }
    obj->setProperty ("props", juce::var (props));

    juce::Array<juce::var> children;
    for (const auto& child : tree)
        children.add (treeToVar (child));
    obj->setProperty ("children", children);
    return juce::var (obj);
}

juce::ValueTree varToTree (const juce::var& v)
{
    const auto type = v.getProperty ("type", {}).toString();
    if (type.isEmpty() || ! juce::Identifier::isValidIdentifier (type))
        return {};

    juce::ValueTree tree { juce::Identifier (type) };
    if (auto* props = v.getProperty ("props", {}).getDynamicObject())
    {
        for (const auto& p : props->getProperties())
        {
            if (! juce::Identifier::isValidIdentifier (p.name.toString()))
                continue;
            const auto& val = p.value;
            if (val.isString() && val.toString().startsWith ("base64:"))
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (val.toString().substring (7)))
                {
                    tree.setProperty (p.name, juce::var (mb), nullptr);
                    continue;
                }
            }
            tree.setProperty (p.name, val, nullptr);
        }
    }

    if (auto* children = v.getProperty ("children", {}).getArray())
        for (const auto& c : *children)
        {
            auto child = varToTree (c);
            if (child.isValid())
                tree.appendChild (child, nullptr);
        }
    return tree;
}

juce::String serialise (const juce::ValueTree& projectTree)
{
    auto* env = new juce::DynamicObject();
    env->setProperty ("format", formatTag);
    env->setProperty ("formatVersion", currentFormatVersion);
    env->setProperty ("appVersion", FF_VERSION_STRING);
    env->setProperty ("root", treeToVar (projectTree));
    // juce::newLine is CRLF on every platform; project files use LF so they diff cleanly across OSes.
    return juce::JSON::toString (juce::var (env), juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine))
               .replace ("\r\n", "\n") + "\n";
}

static void ensureChild (juce::ValueTree& root, const juce::Identifier& type, const juce::ValueTree& defaults,
                         juce::StringArray& warnings)
{
    if (! root.getChildWithName (type).isValid())
    {
        root.appendChild (defaults.getChildWithName (type).createCopy(), nullptr);
        warnings.add ("Missing " + type.toString() + " section was recreated with defaults.");
    }
}

void validate (juce::ValueTree& root, juce::StringArray& warnings)
{
    using namespace ids;
    const auto defaults = createDefault ("Untitled");

    if (root.getProperty (ids::name).toString().trim().isEmpty())
        root.setProperty (ids::name, "Untitled", nullptr);

    for (auto id : { TRANSPORT, METRONOME, TRACKS, MARKERS })
        ensureChild (root, id, defaults, warnings);

    auto t = root.getChildWithName (TRANSPORT);
    auto clampProp = [&] (juce::ValueTree& node, const juce::Identifier& prop, double lo, double hi, double def)
    {
        const auto& v = node.getProperty (prop);
        const double d = (v.isVoid() || ! (v.isDouble() || v.isInt() || v.isInt64())) ? def : (double) v;
        const double c = juce::jlimit (lo, hi, d);
        if (v.isVoid() || ! juce::exactlyEqual (c, d))
            warnings.add ("Property " + prop.toString() + " was invalid and has been corrected.");
        node.setProperty (prop, c, nullptr);
    };
    clampProp (t, bpm, 10.0, 999.0, 120.0);
    clampProp (t, tsNum, 1.0, 16.0, 4.0);
    t.setProperty (tsNum, (int) t.getProperty (tsNum), nullptr);

    const int den = (int) t.getProperty (tsDen, 4);
    if (den != 1 && den != 2 && den != 4 && den != 8 && den != 16 && den != 32)
    {
        warnings.add ("Time signature denominator was invalid and has been reset to 4.");
        t.setProperty (tsDen, 4, nullptr);
    }

    const std::pair<juce::Identifier, juce::var> addedIn02[] = { { loopEnabled, false }, { punchEnabled, false },
                                                                 { loopStart, 0.0 }, { loopEnd, 8.0 } };
    for (const auto& [id, def] : addedIn02)
        if (! t.hasProperty (id))
            t.setProperty (id, def, nullptr); // added in 0.2; silently defaulted
    if ((double) t[loopEnd] <= (double) t[loopStart])
        t.setProperty (loopEnd, (double) t[loopStart] + 8.0, nullptr);

    // tracks & clips: drop malformed entries, clamp values
    auto tracks = root.getChildWithName (TRACKS);
    for (int i = tracks.getNumChildren(); --i >= 0;)
    {
        auto tr = tracks.getChild (i);
        if (! tr.hasType ("TRACK") || tr["id"].toString().isEmpty())
        {
            tracks.removeChild (i, nullptr);
            warnings.add ("An invalid track entry was removed.");
            continue;
        }
        for (int c = tr.getNumChildren(); --c >= 0;)
        {
            auto clip = tr.getChild (c);
            const double len = clip["length"];
            if (! clip.hasType ("CLIP") || clip["file"].toString().isEmpty() || ! (len > 0.0) || (double) clip["start"] < 0.0)
            {
                tr.removeChild (c, nullptr);
                warnings.add ("An invalid clip on track \"" + tr["name"].toString() + "\" was removed.");
            }
        }
    }

    auto m = root.getChildWithName (METRONOME);
    clampProp (m, volume, 0.0, 1.0, 0.8);
    clampProp (m, subdivision, 1.0, 8.0, 1.0);
    m.setProperty (subdivision, (int) m.getProperty (subdivision), nullptr);
}

LoadResult parse (const juce::String& json)
{
    LoadResult r;
    juce::var parsed;
    const auto res = juce::JSON::parse (json, parsed);
    if (res.failed())
    {
        r.result = juce::Result::fail ("The project file is not valid JSON: " + res.getErrorMessage());
        return r;
    }
    if (parsed.getProperty ("format", {}).toString() != formatTag)
    {
        r.result = juce::Result::fail ("This file is not a FretForge project.");
        return r;
    }
    const int version = (int) parsed.getProperty ("formatVersion", 0);
    if (version < 1)
    {
        r.result = juce::Result::fail ("The project file has an unknown format version.");
        return r;
    }
    if (version > currentFormatVersion)
    {
        r.result = juce::Result::fail ("This project was saved by a newer version of FretForge Studio (format "
                                       + juce::String (version) + "). Please update the application.");
        return r;
    }

    auto tree = varToTree (parsed.getProperty ("root", {}));
    if (! tree.isValid() || tree.getType() != ids::PROJECT)
    {
        r.result = juce::Result::fail ("The project file has no valid PROJECT root.");
        return r;
    }

    // Migration hook: when formatVersion increases, upgrade older trees here, step by step.
    validate (tree, r.warnings);
    r.tree = tree;
    return r;
}

// ------------------------------------------------------------------ files

juce::Result writeFileAtomically (const juce::File& target, const juce::String& textIn)
{
    const auto text = textIn.replace ("\r\n", "\n");
    if (! target.getParentDirectory().createDirectory())
        return juce::Result::fail ("Cannot create folder " + target.getParentDirectory().getFullPathName());

    juce::TemporaryFile temp (target, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return juce::Result::fail ("Cannot write " + temp.getFile().getFullPathName() + ": " + out.getStatus().getErrorMessage());
        if (! out.writeText (text, false, false, nullptr))
            return juce::Result::fail ("Writing failed (disk full?)");
        out.flush();
        if (out.getStatus().failed())
            return juce::Result::fail ("Writing failed: " + out.getStatus().getErrorMessage());
    }

    // Verify what hit the disk before replacing the good file.
    if (temp.getFile().loadFileAsString() != text)
        return juce::Result::fail ("Verification of the written file failed; original left untouched.");

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace " + target.getFullPathName());
    return juce::Result::ok();
}

const juce::StringArray& Bundle::mediaFolders()
{
    static const juce::StringArray f { "Audio", "MIDI", "Tabs", "Presets", "IRs", "Autosave", "Backups" };
    return f;
}

juce::File Bundle::normaliseBundlePath (const juce::File& dir)
{
    if (dir.hasFileExtension (bundleExtension))
        return dir;
    return dir.getParentDirectory().getChildFile (dir.getFileName() + bundleExtension);
}

juce::Result Bundle::createStructure (const juce::File& bundleDir)
{
    if (bundleDir.existsAsFile())
        return juce::Result::fail (bundleDir.getFullPathName() + " exists and is a file, not a project folder.");
    if (! bundleDir.createDirectory())
        return juce::Result::fail ("Cannot create " + bundleDir.getFullPathName());
    for (auto& f : mediaFolders())
        if (! bundleDir.getChildFile (f).createDirectory())
            return juce::Result::fail ("Cannot create " + bundleDir.getChildFile (f).getFullPathName());
    return juce::Result::ok();
}

juce::Result Bundle::save (const juce::ValueTree& projectTree, const juce::File& bundleDir, int backupsToKeep)
{
    auto r = createStructure (bundleDir);
    if (r.failed())
        return r;

    auto copy = projectTree.createCopy();
    copy.setProperty (ids::modified, juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    copy.setProperty (ids::appVersion, FF_VERSION_STRING, nullptr);
    const auto text = serialise (copy);

    const auto doc = documentFile (bundleDir);
    if (doc.existsAsFile() && backupsToKeep > 0)
    {
        const auto dir = backupsDir (bundleDir);
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        auto backup = dir.getChildFile ("project-" + stamp + ".ffp").getNonexistentSibling (false);
        doc.copyFileTo (backup);

        auto backups = dir.findChildFiles (juce::File::findFiles, false, "project-*.ffp");
        backups.sort();
        while (backups.size() > backupsToKeep)
        {
            backups.getFirst().deleteFile();
            backups.remove (0);
        }
    }

    r = writeFileAtomically (doc, text);
    if (r.wasOk())
        discardAutosave (bundleDir);
    return r;
}

LoadResult Bundle::load (const juce::File& bundleDir)
{
    const auto doc = documentFile (bundleDir);
    if (! doc.existsAsFile())
    {
        LoadResult r;
        r.result = juce::Result::fail ("No project document found in " + bundleDir.getFullPathName());
        return r;
    }
    if (doc.getSize() > 256 * 1024 * 1024)
    {
        LoadResult r;
        r.result = juce::Result::fail ("Project document is implausibly large; refusing to load.");
        return r;
    }
    return parse (doc.loadFileAsString());
}

juce::Result Bundle::writeAutosave (const juce::ValueTree& projectTree, const juce::File& bundleDir)
{
    return writeFileAtomically (autosaveFile (bundleDir), serialise (projectTree));
}

bool Bundle::hasNewerAutosave (const juce::File& bundleDir)
{
    const auto a = autosaveFile (bundleDir);
    const auto d = documentFile (bundleDir);
    return a.existsAsFile() && (! d.existsAsFile() || a.getLastModificationTime() > d.getLastModificationTime());
}

LoadResult Bundle::loadAutosave (const juce::File& bundleDir)
{
    return parse (autosaveFile (bundleDir).loadFileAsString());
}

juce::String Bundle::toPortablePath (const juce::File& bundleDir, const juce::File& media)
{
    if (! media.isAChildOf (bundleDir))
        return {};
    return media.getRelativePathFrom (bundleDir).replaceCharacter ('\\', '/');
}

juce::File Bundle::fromPortablePath (const juce::File& bundleDir, const juce::String& portable)
{
    if (portable.isEmpty() || portable.startsWithChar ('/') || portable.containsChar (':')
        || portable.contains ("../") || portable == ".." || portable.contains ("\\"))
        return {};
    auto f = bundleDir.getChildFile (portable);
    return f.isAChildOf (bundleDir) ? f : juce::File();
}
} // namespace ff::project
