#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace ff::project
{
constexpr int currentFormatVersion = 1;
constexpr const char* formatTag = "FretForgeProject";
constexpr const char* bundleExtension = ".ffproj";
constexpr const char* documentName = "project.ffp";
constexpr const char* autosaveName = "project.autosave.ffp";

namespace ids
{
    #define FF_ID(name) inline const juce::Identifier name { #name };
    FF_ID (PROJECT) FF_ID (TRANSPORT) FF_ID (METRONOME) FF_ID (TRACKS) FF_ID (MARKERS)
    FF_ID (name) FF_ID (notes) FF_ID (sampleRate) FF_ID (created) FF_ID (modified) FF_ID (appVersion)
    FF_ID (bpm) FF_ID (tsNum) FF_ID (tsDen) FF_ID (keySignature)
    FF_ID (loopEnabled) FF_ID (loopStart) FF_ID (loopEnd) FF_ID (punchEnabled) FF_ID (snap) FF_ID (zoom)
    FF_ID (enabled) FF_ID (volume) FF_ID (subdivision) FF_ID (sound) FF_ID (accents) FF_ID (countInBars)
    #undef FF_ID
}

/** Fresh project tree with all required nodes. */
juce::ValueTree createDefault (const juce::String& name);

/** Lossless ValueTree <-> JSON var conversion ({type, props, children}). */
juce::var treeToVar (const juce::ValueTree& tree);
juce::ValueTree varToTree (const juce::var& v);

struct LoadResult
{
    juce::ValueTree tree;
    juce::Result result = juce::Result::ok();
    juce::StringArray warnings;
};

/** Serialises with the file envelope (format tag + version). */
juce::String serialise (const juce::ValueTree& project);
/** Parses, validates and migrates a document string. */
LoadResult parse (const juce::String& json);
/** Repairs missing nodes / out-of-range values in place, recording warnings. */
void validate (juce::ValueTree& project, juce::StringArray& warnings);

/** A project bundle is a folder `<Name>.ffproj` holding the JSON document and media folders. */
struct Bundle
{
    static const juce::StringArray& mediaFolders();
    static juce::File documentFile (const juce::File& bundleDir) { return bundleDir.getChildFile (documentName); }
    static juce::File autosaveFile (const juce::File& bundleDir) { return bundleDir.getChildFile ("Autosave").getChildFile (autosaveName); }
    static juce::File backupsDir (const juce::File& bundleDir) { return bundleDir.getChildFile ("Backups"); }

    /** Ensures `dir` ends with the bundle extension. */
    static juce::File normaliseBundlePath (const juce::File& dir);
    static bool isBundle (const juce::File& dir) { return documentFile (dir).existsAsFile(); }

    /** Creates folder structure (idempotent). */
    static juce::Result createStructure (const juce::File& bundleDir);

    /** Atomic save: previous document is rotated into Backups/, new content written via temp file + rename. */
    static juce::Result save (const juce::ValueTree& project, const juce::File& bundleDir, int backupsToKeep = 10);
    static LoadResult load (const juce::File& bundleDir);

    static juce::Result writeAutosave (const juce::ValueTree& project, const juce::File& bundleDir);
    static bool hasNewerAutosave (const juce::File& bundleDir);
    static LoadResult loadAutosave (const juce::File& bundleDir);
    static void discardAutosave (const juce::File& bundleDir) { autosaveFile (bundleDir).deleteFile(); }

    /** Media references are stored relative to the bundle with '/' separators. */
    static juce::String toPortablePath (const juce::File& bundleDir, const juce::File& media);
    /** Returns an invalid File if the path escapes the bundle or is absolute. */
    static juce::File fromPortablePath (const juce::File& bundleDir, const juce::String& portable);
};

/** Writes text atomically (temp file in the same folder, flushed, then renamed over the target). */
juce::Result writeFileAtomically (const juce::File& target, const juce::String& text);
} // namespace ff::project
