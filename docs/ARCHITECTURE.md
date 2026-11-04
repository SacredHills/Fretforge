# FretForge Studio — Architecture & Technical Plan

Working title: **FretForge Studio** (rename freely; the name appears only in `CMakeLists.txt`, `src/app/AppInfo.h` and this folder).

This document covers the seven pre-implementation items requested in §18 of the brief: technology choice, application architecture, audio engine, project file format, plugin system, milestone plan, and technical risks — plus licensing.

---

## 1. Technology evaluation

| Option | Audio performance | Cross-platform audio APIs | Plugin hosting | UI fit for a DAW | Verdict |
|---|---|---|---|---|---|
| **JUCE 8 / C++20** | Native, deterministic, no GC | CoreAudio, WASAPI, DirectSound, **ASIO**, ALSA, **JACK** (PipeWire via its JACK/ALSA layers) out of the box | VST3, AU, LV2, (CLAP via `clap-juce-extensions` / JUCE 9 roadmap) | Custom-drawn, GPU-optional, proven in Tracktion, Waveform, many commercial apps | **Chosen** |
| Qt 6 + PortAudio/RtAudio | Native | Good, but MIDI, ASIO and device switching must be assembled by hand | None — must hand-write VST3/AU hosts | Excellent widgets, but DAW controls (waveforms, knobs, timelines) are custom anyway | Viable, more glue code |
| Tauri/Electron + Rust/C++ engine | Engine fine; UI over IPC adds latency to meters/cursors and doubles the state model | Via cpal / RtAudio | Must be done in native side | Web look the brief explicitly wants to avoid | Rejected |
| Rust (cpal + egui/iced, nih-plug) | Excellent | Good; ASIO support immature | VST3/CLAP hosting is immature in Rust | Immediate-mode UIs are weak for docking/notation | Rejected for now |

**Decision:** C++20 + JUCE 8, CMake build, Ninja/MSVC/Xcode generators. The UI is drawn with JUCE's `Graphics` (custom LookAndFeel), so it looks identical and "pro-audio" on all three OSes, with native menus on macOS and native file dialogs everywhere.

## 2. Application architecture

```
┌──────────────────────────── app (UI, message thread) ────────────────────────────┐
│  MainWindow ─ MenuBar ─ TransportBar ─ SectionNav ─ Pages ─ StatusBar            │
│  Pages: Studio · Tabs · Drums · Training · Effects&Amps · Mixer · Library ·      │
│         Tools(Tuner/Metronome) · Settings                                        │
│  ApplicationCommandManager (shortcuts)   UndoManager (project edits)             │
└───────────────▲──────────────────────────────────────────────┬───────────────────┘
                │ polls atomics / lock-free FIFOs (≈30–60 Hz)  │ atomics, SPSC command queue
┌───────────────┴────────────── engine (real-time) ────────────▼───────────────────┐
│  AudioEngine : AudioIODeviceCallback                                              │
│    Transport ─ MetronomeDsp ─ (M2: TrackGraph, Recorder, DiskStreamer)            │
│    Meters (atomic peak/RMS/clip) ─ Input tap → SPSC FIFO → analysis thread        │
│  EngineHost: AudioDeviceManager, device-state persistence, MIDI devices           │
└───────────────▲──────────────────────────────────────────────────────────────────┘
                │ pure functions / data types, no threads, no UI
┌───────────────┴────────────── core (testable library) ───────────────────────────┐
│  MusicTheory (notes, intervals, tunings) · PitchDetector (MPM/NSDF via FFT)       │
│  MetronomeDsp · TapTempo · ProjectFile/ProjectModel (ValueTree + JSON, atomic IO) │
│  SpscFifo · (M3: TabModel, GP/MusicXML importers) · (M6: ExerciseGenerator)       │
└──────────────────────────────────────────────────────────────────────────────────┘
```

Modules map to CMake targets: `ff_core` (static lib, links only `juce_core`, `juce_data_structures`, `juce_audio_basics`, `juce_dsp`), `ff_engine` (adds `juce_audio_devices`, `juce_audio_formats`, `juce_audio_utils` for processors), `FretForgeStudio` (GUI app), `ff_tests` (console test runner, no GUI). Tests never need audio hardware: `AudioEngine::process()` takes raw buffers and is driven offline.

**Data model:** the project is a `juce::ValueTree` owned by `ProjectDocument`. All UI edits go through the tree with the `UndoManager`, giving undo/redo for free. The engine never touches the tree; changes are compiled on the message thread into immutable *engine snapshots* (M2+) and swapped in with an atomic pointer exchange; the old snapshot is released on the message thread (no deallocation on the audio thread).

## 3. Audio engine

Real-time rules (enforced by review + tests):

1. The device callback does **no** locks, allocation, file IO, logging, or UI calls.
2. UI → engine: scalar parameters are `std::atomic`; structural changes (M2) are `std::shared_ptr` snapshots swapped via a lock-free SPSC queue, with a "garbage" queue back to the message thread.
3. Engine → UI: meters as atomics (peak with hold handled in UI), playhead as `std::atomic<int64>`, analysis audio through `SpscFifo<float>` sized at prepare time.
4. Buffers are allocated in `audioDeviceAboutToStart`, sized for the device's maximum block size.
5. Recording (M2): the callback writes into a pre-allocated ring buffer; a disk-writer thread drains it to `juce::AudioFormatWriter::ThreadedWriter` in the project's `Audio/` folder. Playback uses `BufferingAudioReader` read-ahead threads.
6. Timeline is sample-based internally with a tempo map (PPQ ↔ seconds) for musical positions.

Device layer: `juce::AudioDeviceManager` with all compiled backends. Windows: WASAPI (exclusive & shared), DirectSound, ASIO (when built with `FF_ENABLE_ASIO=ON` and the ASIO SDK path). macOS: CoreAudio. Linux: ALSA and JACK; PipeWire systems work through `pipewire-jack` or `pipewire-alsa`. Device setup (type, in/out, sample rate, buffer) is persisted as XML in the user settings and restored at launch, with fallback to the default device if the saved one is missing.

## 4. Project file format (`.ffproj` bundle)

A project is a **folder** so media can live beside the document and the whole thing can be zipped, synced, or moved between OSes.

```
My Song.ffproj/
├── project.ffp          ← JSON document (UTF-8, LF), the source of truth
├── Audio/               ← recorded & imported audio (WAV/FLAC), relative paths only
├── MIDI/  Tabs/  Presets/  IRs/
├── Autosave/project.autosave.ffp
└── Backups/project-YYYYMMDD-HHMMSS.ffp   (rotated, last N kept)
```

`project.ffp` is a lossless JSON encoding of the ValueTree:

```json
{ "format": "FretForgeProject", "formatVersion": 1, "appVersion": "0.1.0",
  "root": { "type": "PROJECT", "props": { "name": "My Song", ... },
            "children": [ { "type": "TRANSPORT", "props": { "bpm": 120.0, "tsNum": 4, "tsDen": 4 } }, ... ] } }
```

- **Portability:** media references are stored relative to the bundle with `/` separators; no absolute paths, no platform line endings in data.
- **Corruption protection:** every save writes to a temp file in the same directory, flushes, then atomically replaces the target; the previous version is rotated into `Backups/` first.
- **Validation:** loader checks the envelope, version, required nodes, value ranges; unknown nodes are preserved (forward compatibility); a `migrate()` step upgrades older versions.
- **Crash recovery:** a session lock file records the open project. If the app starts and finds a stale lock and an autosave newer than the project file, it offers to recover.

## 5. Plugin system

Two kinds:

1. **Audio plugins (M5):** `juce::AudioPluginFormatManager` with VST3 (all OSes), AU (macOS), LV2 (Linux). Scanning runs **out of process** (a `--scan` mode of the same executable) so a crashing plugin cannot take down the app; results cached in `KnownPluginList` XML with a blacklist. CLAP via `clap-juce-extensions`'s hosting work or a thin custom host once JUCE ships native CLAP hosting.
2. **Built-in processors:** every built-in effect/amp is an `ff::Processor` (subclass of `juce::AudioProcessor`) registered in a `ProcessorRegistry` by stable string ID (`ff.amp.modern-high-gain`), so built-ins and third-party plugins share the same chain, preset, automation and serialization code.

Extension points for later: content packs (drum kits, IR packs, lesson plans as JSON) loaded from `Library/` folders; AI providers behind an `IAssistantProvider` interface (local: llama.cpp/Ollama HTTP; online: user-configured keys), never required.

## 6. Milestones (status)

| # | Milestone | Status |
|---|---|---|
| 1 | Foundation: shell, project system, device config, engine, metronome, tuner | **Done** |
| 2 | Recording studio: tracks, recording, playback, waveforms, timeline, editing, mixer, export | **Done** (comping lanes, time-stretch, tempo map, automation still to come) |
| 3 | Tab system: editor, notation, playback, MIDI, Guitar Pro import, PDF | **Done**: editor, techniques, synth playback, standard notation, Guitar Pro 3-7 and MusicXML import, MusicXML/MIDI/PDF export, tempo/key changes, chords, lyrics, MIDI-keyboard entry. Open: grace notes, multiple voices, repeat signs kept as signs |
| 4 | Drum studio: sequencer, piano roll, drum instruments, generator, arrangement | **Done**: grid sequencer, velocity, swing, humanize, 17-style generator, song arrangement, synthesized and sample kits, MIDI song import/export, Guitar Pro drum import, tab play-along, render to audio. Open: piano roll, articulations, velocity layers |
| 5 | Effects & amps: built-ins, amp/cab sim, IRs, plugin hosting | **Done**: VST3/AU hosting in the rig chain (out-of-process scanning), plus Neural Amp Modeler amp, IR cabinet, gate/comp/drive/EQ/chorus/delay/reverb, reorderable chain, live rig and per-track rigs, presets, offline export. Open: VST3/AU hosting, more effects |
| 6 | Training centre: exercise generator, lessons, progressive practice, stats | Planned |
| 7 | Theory, songwriting, analysis, assistance, mastering | Planned |
| 8 | Release: installers, optimisation, docs, onboarding | Packaging scaffolding started (CPack) |

Every unimplemented section in the app shows a clearly labelled "Not yet implemented — Milestone N" page with its planned scope and **no controls**.

## 7. Technical risks

| Risk | Mitigation |
|---|---|
| Audio dropouts / latency | Strict RT rules above; pre-allocation; CPU meter in transport; offline engine tests; later: `rtsan` (Clang RealtimeSanitizer) in CI |
| Linux audio fragmentation (ALSA/JACK/PipeWire/Pulse) | Ship ALSA+JACK backends; document PipeWire-JACK; graceful "no device" mode so the app still runs |
| ASIO licensing | SDK not bundled; opt-in CMake flag (see §8) |
| Guitar Pro import (binary, undocumented, 5 format generations) | Separate `ff_import` module, fuzz-tested parser per version (gp3/4/5 binary, gpx BCFZ container, gp7+ zip/XML); TuxGuitar & alphaTab source as format references |
| Notation rendering & PDF export | Own engraving layer on top of JUCE `Graphics`; PDF via a `Graphics` backend to libHaru (zlib licence) or Cairo |
| Pitch/onset detection accuracy (low B, drop tunings, distortion) | MPM with confidence; adaptive window for down to ~27 Hz; always show confidence and allow manual correction |
| Time-stretch / pitch-shift quality | Signalsmith Stretch (MIT) rather than Rubber Band (GPL/commercial) |
| Plugin crashes | Out-of-process scanning; later optional sandboxed hosting |
| Scope | Milestone gating; each milestone must build, pass tests, and run before the next |

## 8. Licensing & distribution

- **JUCE 8** is dual-licensed: AGPLv3 or a commercial JUCE licence (a free Starter tier exists for small revenue). If you distribute closed-source builds you need a commercial tier; if the app is AGPLv3 open source, no fee. Verify the current terms at juce.com before release.
- **VST3 SDK**: Steinberg relicensed it under MIT in October 2025, so VST3 hosting no longer needs a separate Steinberg agreement.
- **ASIO SDK**: now dual-licensed (GPLv3 or proprietary Steinberg agreement). Not bundled here; build with `-DFF_ENABLE_ASIO=ON -DFF_ASIO_SDK_DIR=...` after downloading it. A closed-source app needs Steinberg's proprietary ASIO licence.
- **FLAC / Ogg** via JUCE (BSD-style). **MP3 decode** via JUCE's MP3 reader (patents expired); **MP3 encode** via LAME (LGPL) — invoked as an external binary or dynamically linked.
- **Guitar Pro / MusicXML importers**: written from scratch for FretForge. The alphaTab (MPL-2.0) and PyGuitarPro
  (LGPL-3.0) projects were used only as references and test oracles during development — none of their code or test
  files are shipped. The committed GP fixtures in `tests/data` were generated by `tests/data/make_gp_fixtures.py`.
  Guitar Pro is a trademark of Arobas Music; FretForge only reads the file format.
- **SoundFont playback (later)**: FluidSynth (LGPL-2.1, dynamic linking) or a small built-in SF2 player; bundled instruments must be redistributable (e.g. GeneralUser GS licence or a self-made CC0 set).
- **CLAP**: MIT.
- **Neural Amp Modeler core** (Steven Atkinson, MIT) is vendored unmodified in `third_party/nam`, with the nlohmann/json
  header it ships (MIT) and the Eigen 3.4 headers (MPL-2.0, built with `EIGEN_MPL2_ONLY`); see `third_party/README.md`.
  No amp captures are bundled — `.nam` files are the users' own (each capture has its own licence). The test networks
  in `tests/data/nam` come from NeuralAmpModelerCore's own examples (MIT).
- The built-in amp is original and unnamed ("Built-in amp") — no trademarks.

## 9. Milestone 2 design notes

- **Model:** `TRACK` nodes hold `CLIP` children. Clip positions, lengths, source offsets and fades are in **seconds**, so a
  sample-rate change never moves audio. Child order is layer order (later = on top).
- **Layering:** `timeline::visiblePieces()` turns overlapping clips into audible pieces: the top clip masks lower clips
  where it is at full level; during its fades the lower clip keeps playing with the opposite equal-power fade. Loop
  passes are simply stacked clips, so "choosing a take" is moving a clip to the top.
- **Playback snapshots:** `buildPlaybackGraph()` converts the tree into an immutable `PlaybackGraph` (pieces in engine
  samples, gains, inputs, recorders). The message thread hands it over with an atomic pointer exchange; the audio
  thread retires the old one through a lock-free FIFO; `collectGarbage()` frees it on the message thread.
- **Audio cache:** files are decoded into memory on a loader thread (with min/max peaks for waveforms). The real-time
  path never touches the disk. Disk streaming is the planned upgrade for very long sessions.
- **Recording:** one `TrackRecorder` per armed track writes a single WAV through a lock-free FIFO drained by the
  `DiskWriter` thread. The audio thread records *segments* (timeline start, file offset, length) for each contiguous
  stretch — every loop pass or punch range. When recording stops, segments become clips pointing into the file, with
  the device's input+output latency compensated.
- **Export:** the same graph and renderer run offline at the requested sample rate on a background thread.

## 10. Milestone 3 design notes (part 1)

- **Model:** a `SCORE` node in the project holds shared `MEASURE` headers (meter, marker) and `TABTRACK`s; each track
  has one `BAR` per measure containing `BEAT`s (value, dots, tuplet, rest, stroke, tremolo) with `NOTE`s (string, fret,
  technique flags). Strings are numbered from the lowest. Everything is a ValueTree, so editing is undoable and saved
  with the project.
- **Compile:** `tab::compile()` turns the score into timed note events, resolving ties, let-ring, hammer-on legato and
  slide targets, harmonics and capo.
- **Synth:** `GuitarSynth` is an extended Karplus-Strong string model with fractional delay (accurate tuning, smooth
  bends), pick filtering, palm-mute/dead-note damping, harmonic excitation, legato re-excitation and tremolo re-picks.
  One voice per string, so re-plucking a string stops its previous note as on a real guitar.
- **Engine:** the tab page switches the engine into `tabMode`; a `TabSequence` snapshot is swapped in like the playback
  graph and rendered sample-accurately. Practice speed rebuilds the sequence at a scaled tempo, so pitch is unaffected.

## 11. Milestone 3 design notes (part 2)

- **Tempo and key changes:** `MEASURE.tempo` (> 0) changes the tempo at a bar line, `MEASURE.key` sets the key signature.
  `tab::tempoMap()` maps quarters to seconds through the changes; the synth sequence, playhead, loop range and count-in
  all use it. In tab mode the metronome is driven by click events in the `TabSequence` (`MetronomeDsp::triggerClick`),
  so the click follows meter and tempo changes sample-accurately.
- **Import pipeline:** every reader (GP3/4/5 binary, GP6 `.gpx` BCFZ/BCFS container, GP7+ `.gp` zip, MusicXML and
  `.mxl`) produces a format-neutral `gp::Song`; `gp::applySong()` writes it into the score in one undoable transaction.
  Repeats and alternate endings are written out (`gp::playbackOrder`) so playback follows the song form. Unsupported
  content is reported as warnings: drum tracks, extra voices, grace notes and unusual tuplets.
- **Verification:** 175 real-world Guitar Pro files import; GP3-5 rhythms and notes match PyGuitarPro exactly (bar by
  bar), GPX/GP7 files match their GP5 counterparts, every MusicXML file exported from those songs is read by music21,
  and exported pitches match the source. Optional sweeps: `FF_GP_TESTDATA=dir` / `FF_MUSICXML_TESTDATA=dir ff_tests`.
- **Notation:** drawn by `TabEditor` from the same model (no separate notation model): treble-8vb or bass-8vb clef,
  key signatures with cancellation, pitch spelling by key, accidentals that persist through the bar, ledger lines,
  displaced seconds, stems and beams by beat groups, flags, dots, tuplets, ties and rests. Glyphs are vector paths
  (`NotationGlyphs`) so no music font is needed.
- **PDF:** an offscreen `TabEditor` in print styling renders each A4 page at 200 dpi; `ScorePdf` writes the pages as
  JPEG images into a minimal PDF (systems are never split). Printing opens that PDF in the system viewer — JUCE has no
  cross-platform print API.
- **MIDI entry:** with MIDI In on, notes struck within 90 ms form one chord; strings are chosen by `assignStrings`
  near the previous position, and each new chord advances the cursor.

## 12. Milestone 4 design notes (part 1)

- **Model:** `DRUMS` holds `PATTERN`s (meter, bars, editing grid, swing) with `HIT {lane, tick, vel}` children at 960
  ticks per quarter, and a `SONG` of `SECTION {pattern, repeats}`. The grid is only an editing aid; hits keep exact ticks.
- **Compile:** `drums::compile` expands a pattern or the song into quarter-note events, applies swing, and adds
  humanize offsets from a hash of (repetition, tick, piece) so a humanized groove sounds the same on every playback.
- **Generator:** rule-based per style (backbeat placement, cymbal pulse, kick vocabulary, ghost notes, blast types,
  gallops, 3-over-4 groupings), shaped by complexity and half-time, with a fill and crash option; `juce::Random` with a
  fixed seed makes every variation reproducible (unit-tested per style).
- **Sound:** `DrumSynth` synthesizes every piece (pitch-swept sines, TPT state-variable filtered noise, six inharmonic
  square oscillators for metal), with hi-hat choke groups, velocity and constant-power pan. Drum hits travel in the same
  `TabSequence` snapshot as tab notes, so the engine's score mode renders guitars and drums sample-accurately with no
  locks; `renderOffline` produces the audio for "Render to Studio".

## 13. Milestone 4 design notes (part 2)

- **MIDI import:** the whole file becomes one pattern (up to 4000 bars) with every hit at its exact tick.
- **Guitar Pro drums:** `gp::drumBars` writes out the repeats and `drums::importAsOnePattern` stores the whole drum
  part as one pattern (leading empty bars kept, so it lines up with the tab for play-along).
- **Autosave** serialises a snapshot of the tree on a background thread; line endings are normalised in linear time
  (the earlier `String::replace` was quadratic and froze the UI for seconds on large projects).
- **Guitar Pro drums:** GP3-5 store the MIDI drum note in the fret field; GP7 maps a note's InstrumentArticulation to
  the track's articulation table (OutputMidiNumber); GP6 uses element/variation numbers mapped to General MIDI.
- **Sample kits:** `DRUMS/KIT` holds a project-relative path per piece (files copied to `Audio/DrumKit`, and copied on
  Save As). The message thread decodes and resamples to the device rate, hands the buffer to the audio thread through a
  lock-free FIFO; the audio thread stops that piece's voices, swaps the pointer and returns the old buffer through a
  second FIFO to be freed on the message thread — no locks or allocation on the audio thread.
- **Play-along:** `addDrumSong` adds the drum song to the tab `TabSequence`, timed through the score's tempo map.

## 14. Milestone 5 design notes (part 1)

- **Model:** a `RIG` node (on a `TRACK`, or standalone for the live rig kept in the app settings) stores one float per
  parameter (defaults apply to missing properties), the block order as a list of ids, and the model / IR paths.
- **`fx::AmpRig`** runs gate, compressor, overdrive, amp, EQ, cabinet, chorus, delay and reverb in the stored order.
  Blocks before the first stereo block (chorus/delay/reverb) are mono; a mono block after a stereo one sums to mono.
  Parameters are atomics; the order is packed into one `atomic<uint64>` (4 bits per slot). Filters use an in-place RBJ
  biquad so retuning never allocates; gains are smoothed.
- **`fx::NamModel`** wraps `nam::DSP`. The network always runs at its training rate (`Reset(modelRate, 4096)`); a
  streaming Lagrange resampler converts in and out when the device runs at another rate (a 4th-order anti-alias
  low-pass when decimating, 16 primed model samples so the output side never runs dry; reported latency is measured
  exactly in the tests). NAM registers its architectures from static objects, which a static library would drop, so
  `NamModel` references every factory (and registers them itself if needed).
- **Hand-over:** models are loaded and prepared on a background thread (`RigBank`), handed to the audio thread through
  an atomic pointer, and replaced models travel back through a lock-free FIFO to be freed on the message thread. A
  model prepared for another device rate is never run: the rig flags it and the bank reloads it. IRs go through
  `juce::dsp::Convolution`'s wait-free loader (shared message queue).
- **Engine:** `PlayTrack::rig` (a `shared_ptr`, so a rig removed from the project lives until the graph that uses it
  is freed on the message thread) is applied after clips and monitoring, before fader and pan. The live rig is a raw
  pointer the bank keeps alive; on teardown the bank clears it and waits for one audio callback. `AudioEngine::prepare`
  calls a hook so the bank re-prepares every rig when the device restarts.
- **Export** builds its own rigs with `makeOfflineRig` (model and IR loaded synchronously) and resets them before
  each render, so mixes and stems start from silence.
- **Tests** check that the wrapper is bit-identical to NAM itself, the resampler's latency and quality at 32/44.1/96 kHz,
  hand-over and garbage collection, the gate, delay timing, IR alignment, block order, the engine insert and live rig,
  export, the bank following the tree, and (category `perf`) the real-time factor of a standard WaveNet.

## 15. Milestone 5 design notes (part 1, continued)

- **Tool windows:** `ToolWindow` is a floating `DocumentWindow` (always on top, remembers its bounds) showing a
  component that lives elsewhere; the tuner, the metronome, each track's NAM amp (`RigEditor` with a track id) and the
  TONE3000 browser use it. The tuner analysis now runs whenever the app is open (the transport readout).
- **Calibration:** `NamInfo::inputLevelDbu` comes from the capture's `input_level_dbu` metadata; with calibration on,
  the amp block multiplies its input by `10^((interfaceDbu - inputLevelDbu)/20)` before the network. The interface
  level is a rig parameter that presets never overwrite; the last value used becomes the default for new track amps.
- **TONE3000:** `t3k::Client` follows TONE3000's MIT-licensed JUCE example: OAuth 2 authorisation code with PKCE in the
  system browser, redirected to a loopback HTTP listener on 127.0.0.1 (no redirect URI to register); tokens kept in
  the app settings and refreshed 60 s before expiry (single-flight, one retry on 401). The "select tone" prompt lets
  users search TONE3000's own catalogue page; the app then lists the tone's models (A1/custom and A2), downloads with
  the bearer token into `Documents/FretForge/TONE3000/<tone - creator>/` and loads the file into the requesting rig.
  On Linux `juce::URL` needs libcurl (`JUCE_USE_CURL=1`).
- **Play-along drums** repeat the compiled drum song (or the selected pattern when no song is arranged) until the end
  of the tab sequence, cutting the last pass at the tab's end.

## 16. Milestone 5 design notes (plugin hosting)

- **Scanning** uses JUCE's `KnownPluginList` with a custom scanner that sends each file to a child process: the app
  re-launches its own executable with a command-line id (`fretforge-plugin-scanner`), which turns that copy into a
  `ChildProcessWorker` (no window, single-instance check skipped). A crash or a 60 s hang marks the plugin as failed;
  JUCE's dead-man's-pedal file covers a crash of the app itself during a scan. The list is saved in the settings.
- **Slots:** each rig has four plugin blocks (`plug1..plug4`) in the same reorderable order as the built-in blocks
  (4 bits per block, 13 blocks), with on/off parameters; `PLUGIN {slot, desc, state, name}` children hold the
  `PluginDescription` XML and the base64 plugin state. Empty slots are hidden in the chain view.
- **Engine:** `AmpRig` owns the instances (`shared_ptr`, message thread) and publishes raw pointers to the audio thread
  through atomics; a plugin runs as a stereo block (mono plugins get the sum and feed both sides), under its callback
  lock, with a pre-allocated 8-channel buffer. Replacing or removing a plugin closes its editor, swaps the pointer,
  waits for one audio callback (`AudioEngine::waitForAudioCallback`) and only then releases it on the message thread.
  `AmpRig::prepare` re-prepares plugins when the device restarts.
- **Creation** goes through `fx::PluginHost` (implemented by the app's `PluginManager`): asynchronous on the message
  thread for live rigs, blocking (waiting for the message thread) for offline export rigs.
- **State** is copied into the PLUGIN nodes every two seconds and right before saving; presets carry plugins too.
- **Tests:** `-DFF_BUILD_TEST_PLUGIN=ON` builds "FF Test Gain", a tiny VST3; with `FF_TEST_VST3` pointing to it the tests
  scan, load, process, bypass, save/restore state and render it offline. Linux CI runs them.

- **Track inserts:** `TRACK/INSERTS/PLUGIN {slot, desc, state, name, bypass}` (up to 8, played by slot number).
  `fx::InsertChain` keeps plugins in fixed physical slots with a packed play order (8 x 4 bits + count), so moving an
  insert never reloads it; the bank maps PLUGIN nodes to physical slots. `PlayTrack::inserts` runs after the rig,
  before fader and pan; exports build offline chains (`makeOfflineInserts`).
- **Formats:** VST3, AU (macOS) and LV2 (JUCE's bundled lilv) are always built in. VST2 needs Steinberg's proprietary
  VST 2 SDK: `-DFF_VST2_SDK_DIR` enables `JUCE_PLUGINHOST_VST` for licence holders; it is never bundled.
