# FretForge Studio

A cross-platform guitar workstation (DAW + tab editor + drum studio + training centre), built in C++20 with JUCE 8.
See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the technology evaluation, architecture, audio-engine design,
project format, plugin system, milestone plan, risks and licensing.

## Status: Milestone 5 — Effects & Amps with Neural Amp Modeler and plugin hosting (working)

**Studio:** "Follow" (scroll with the playhead, on by default) and "Slice" (click a clip to cut it at the mouse, snapped; Alt = no snap; Esc leaves) buttons in the Studio toolbar.

**Track plugins (new):** every track can have up to 8 insert plugins after its NAM amp and before the fader: the
Mixer strips have a "+ Plugin" row (click a plugin to open it, right-click to bypass / move / replace / remove), and the
Studio track menu has "Add plugin..." and the track's plugins. Exports include them.

**Plugin formats:** VST3 everywhere, Audio Units on macOS, LV2 everywhere (common on Linux). **VST2** cannot be shipped:
it needs Steinberg's VST 2 SDK, which is closed and has not been licensed to new developers since 2018 (the 2025 MIT
release covers VST 3 only). If you have the SDK under a VST 2 licence, build with `-DFF_VST2_SDK_DIR=<path>` and VST2
hosting is switched on (scanned and used like the others).

**Plugins:** VST3 plugins (and Audio Units on macOS) in any amp rig. Track > Plugins... (or "Manage plugins..." on
the Effects page) scans for them in a separate process, so a plugin that crashes while being scanned cannot take the app
down; it is just listed as failed. Click the "+ Plugin" tile at the end of a rig's chain to add one (up to four per rig);
drag its tile to move it anywhere in the chain, click its lamp to bypass it, "Open plugin window" shows its own editor.
Plugin settings are saved with the project (and with the live rig and rig presets), and exports render them.
Plugin latency is compensated on playback and in exports (each track's clips are read ahead by its amp + plugin latency); live monitoring through a plugin still hears its latency, as in any DAW. Instruments are not listed (effects only).

**Latest changes:** the transport bar is centred and has a live tuner readout (click it for the tuner window);
right-click the click button (or Transport > Metronome...) for the metronome window — the Tools section is gone.
Right-click a track (Studio header or Mixer strip) > "Add NAM amp..." opens that track's amp in its own window
(the Mixer strips also have a NAM AMP button); the Effects & Amps page is the live practice rig. The cabinet block
has an IR browser like the model browser. NAM input calibration. A built-in TONE3000 browser downloads captures and
IRs straight into a rig. The Mixer can add and rename tracks. Drum play-along in Guitar Tabs repeats the drum song
(or the selected pattern) until the end of the tab.

| Area | What works now |
|---|---|
| Amp | **Neural Amp Modeler** built in (the real NAM engine, vendored): load any `.nam` capture of an amp or pedal (WaveNet, LSTM, ConvNet, A2/slimmable); runs at the capture's own sample rate with automatic resampling when the interface runs at another rate; optional loudness normalisation; or a built-in two-stage preamp when no capture is loaded |
| Model browser | Point it at your folder of captures: searchable list, one click loads; metadata (gear, tone type, architecture, rate, loudness, input level) shown |
| Input calibration | For captures that store their input level (`input_level_dbu`): set your interface's level (dBu at 0 dBFS) and the signal reaches the model at the level the capture was made at, so gain behaves like the real amp |
| TONE3000 | Built-in browser: sign in with your TONE3000 account in the browser (OAuth with PKCE, loopback redirect), pick a tone from TONE3000's catalogue ("Browse TONE3000...") or from your favourites / downloads / own tones / trending / latest, see its models and double-click to download and load. Needs a TONE3000 publishable API key (free, from your TONE3000 account); search inside the app needs TONE3000's commercial API tier, the catalogue page in the browser does not |
| Cabinet | Impulse-response loader and browser (.wav/.aif/.flac, mono or stereo, any rate; trimmed and normalised) or a built-in speaker filter; low/high cut |
| Pedals | Noise gate (hysteresis), compressor, overdrive (2x oversampled), 4-band EQ, stereo chorus, delay (tone-controlled repeats), reverb; input and output level |
| Chain | Drag blocks to reorder them, click a lamp to switch a block on/off; per-block help |
| Live rig | The Effects & Amps page: play through the rig with no track and no transport (practice mode), choose the input; always starts switched off |
| Track rigs | Right-click a track > "Add NAM amp..." (or the Mixer's NAM AMP button) opens the track's amp window. Any Studio track can have its own rig: it shapes the track's clips and its live monitoring, recordings stay dry so you can change the amp afterwards (re-amping); exports render the rigs offline |
| Presets | Five built-in presets (pedal and tone settings, they keep your loaded capture/IR) plus your own presets saved as `.ffrig` files (they include the model and IR paths); undo for track rigs |
| Status | Per-rig CPU load, latency added by resampling, gate state, in/out meters |

**Not yet in Milestone 5:** VST3 / Audio Unit plugin hosting, more built-in effects (wah, phaser, flanger, tremolo,
pitch shifter, octaver), stereo IRs kept stereo, copying `.nam` / IR files into the project bundle on Save As (they are
referenced by absolute path for now), tempo-synced delay. No amp captures ship with the app: each capture has its own
licence, so you load your own.

## Milestone 4 — Drum Studio (working)

| Area | What works now |
|---|---|
| Patterns | Any number of patterns; 2/4 to 12/8 meters; 1–16 bars; grid 1/8, 1/8T, 1/16, 1/16T, 1/32 (hits keep exact positions when the grid changes); swing; new / duplicate / rename / delete; full undo |
| Grid editor | 19-piece kit lanes (2 kicks, snare, side stick, clap, 3 hi-hats, 4 toms, 2 crashes, china, splash, ride, ride bell, cowbell); click to add/remove, Shift-click ghost, Alt-click accent, drag a hit for velocity, drag across to paint, right-click velocity presets, click a lane name to hear the sound |
| Generator | 17 styles: rock, hard rock, pop, punk, funk, blues shuffle, reggae one-drop, jazz swing, metal, thrash, d-beat, doom, death metal, black metal, blast beats (traditional / hammer / bomb), double-bass gallop, djent 3-over-4; half-time, complexity, fill, crash on 1; each click gives a new variation |
| Humanize | Timing (ms) and velocity (%) variation, identical on every playback |
| Song | Arrange patterns into sections with repeats; reorder; play the song with a moving playhead |
| Sound | Built-in synthesized kit (no samples needed; hi-hat choke, velocity, stereo placement), or your own samples: right-click a drum name to load a sample, or load a whole kit folder (files matched by name: kick, snare, hihat open, tom 2, ride bell...); samples are copied into the project |
| Play-along | Guitar Tabs "Drums" button plays the drum song with the tab, following the score's tempo changes and practice speed; Guitar Pro drum tracks are imported into the Drum Studio automatically, as one pattern holding the whole song; the Drum Studio's Import button also takes Guitar Pro files (drums only) |
| Output | MIDI export (pattern or song, General MIDI drums); MIDI import of whole drum songs of any length as one continuous pattern (every hit where it is in the file; tempo adopted); render the song to an audio track in the Studio |

**Not yet in Milestone 4:** a piano roll for melodic MIDI, flams/drags and cymbal chokes as articulations, velocity layers
and round-robin in sample kits, tempo changes inside a drum song (in Guitar Tabs the drums follow the score's tempo).

## Milestone 3 — Guitar tab editor (working)

| Area | What works now |
|---|---|
| Tab tracks | Any number of tab tracks per project; 4–8 string guitar and bass presets (24 tunings) or custom tunings; capo; mute/solo/volume |
| Editing | Click or arrow-key cursor; type frets (two quick digits for 10+); note values 1–1/64, dotted, triplets, rests; insert/delete beats and bars; per-bar time signatures; copy/cut/paste beats; transpose ±1 semitone (re-strings notes that fall off the neck); full undo |
| Techniques | Hammer-on/pull-off, slides (to next / in / out), bends (½, full, 1½, 2) and pre-bends, vibrato, palm mute, dead and ghost notes, natural and artificial harmonics, let ring, tapping, ties, accents, staccato, pick strokes, tremolo picking |
| Standard notation | Shown above the tab (toggle "Notation"): treble-8 or bass-8 clef, key signatures, accidentals, ledger lines, stems, beams, flags, dots, tuplets, ties, rests |
| Score text | Tempo changes, key signatures and section markers per bar ("Bar..."); chord names with diagrams, lyrics and text per beat ("Text..." or double-click) |
| Playback | Built-in physically-modelled guitar/bass synth that performs the techniques; follows tempo changes; practice speed 25–150% without pitch change; loop the selection or the whole song; metronome click that follows meter and tempo changes; count-in |
| Import | **Guitar Pro** 3, 4, 5, 6 (.gpx) and 7/8 (.gp); **MusicXML** (.musicxml, .xml, compressed .mxl); MIDI; ASCII tab. Replace the score or add the tracks; repeats are written out; drag a file onto the Tabs page |
| Export | MIDI (with tempo changes), MusicXML (TAB staff with fingering and techniques), PDF (notation + tab, A4), print (via the PDF) |
| MIDI entry | "MIDI In": play notes or chords on a MIDI keyboard / MIDI guitar interface to write them at the cursor |

**Not yet in Milestone 3:** grace notes, second voices (only voice 1 is imported), drum tracks from Guitar Pro files
(they wait for the Drum Studio), repeat signs kept as signs (they are written out on import), direct printing without
the PDF step, PDF with selectable text (pages are images).

## Milestone 2 — Recording studio (working)

| Area | What works now |
|---|---|
| Tracks | Add / duplicate / delete / reorder audio tracks; name, colour, input (mono or stereo pair), record arm, input monitoring, volume, pan, mute, solo, per-track meters |
| Recording | Real recording from the interface to 24-bit WAV in the project's `Audio/` folder via a lock-free FIFO and a disk-writer thread; multiple tracks at once; count-in (off / 1 / 2 bars); punch in/out on the ruler range; loop recording that turns every pass into a take; latency compensation; live red "recording" region |
| Timeline | Bars/beats ruler, zoom (buttons, Ctrl+wheel, fit), scrolling, snap grid (bar to 1/16, Alt bypasses), loop range by dragging the ruler, markers (add, rename, delete, jump), auto-follow playhead, click-to-locate |
| Clip editing | Waveforms, select / multi-select, move (also across tracks), trim both edges, fade-in/out handles, split at playhead, cut/copy/paste, duplicate, delete, mute, normalise/reset clip gain, rename, choose take — all non-destructive and undoable |
| Layering & crossfades | The top clip hides the ones beneath; fades on overlapping clips become equal-power crossfades automatically |
| Playback | Sample-accurate clip playback with Hermite resampling for files at other sample rates; loop playback |
| Import | WAV, AIFF, FLAC, OGG, MP3 via Import button, Ctrl+I or drag-and-drop (copied into the project) |
| Mixer | A channel strip per track (fader, pan, arm/mute/solo, meter with peak hold) plus the main output |
| Export | Full mix or stems; WAV (16/24/32-float) or FLAC; session rate or 44.1–96 kHz; whole song or loop range; optional peak normalisation; TPDF dither for 16-bit; runs in the background with progress |
| Projects | Unsaved projects record into a scratch bundle that is recovered after a crash; Save As copies all referenced audio into the new bundle |

Plus everything from Milestone 1 (transport, devices, metronome, tuner, project safety, shortcuts).

**Shortcuts added:** R record, L loop, P punch, S split, K / Insert marker, Ctrl+T add track, Ctrl+I import,
Ctrl+E export, Ctrl+D duplicate, Ctrl+C/X/V, Ctrl+A, Del, = / - zoom, F fit.

**Not yet in Milestone 2:** comping lanes (swipe-to-comp; takes are chosen per clip for now), time-stretch and pitch-shift,
tempo/meter changes along the timeline, track folders/groups, automation lanes, MIDI and instrument tracks, MP3 export
(needs LAME), and disk streaming (audio is loaded into memory; fine for songs, not for hour-long sessions).

Sections without working features (Training, Library) still show a labelled
"Not yet implemented — Milestone N" page with their planned scope and no controls.

## Building

**macOS:** install the Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`), then run
`./build-mac.sh` in Terminal from the project folder. It builds a universal app (Apple Silicon + Intel), runs the tests
and makes a `.dmg` in `build-mac/`. Options: `clean`, `native`.

**GitHub Actions:** `.github/workflows/build.yml` builds macOS, Windows and Linux on every push; download the results from
the run's Artifacts section.

**Windows, one click:** install Visual Studio 2022+ ("Desktop development with C++") and Git, then double-click
`build-windows.bat`. It finds the compiler, downloads JUCE, builds, runs the tests, makes a ZIP (and a setup .exe if
NSIS is installed) and offers to launch the app. Options: `clean`, `debug`, `asio C:\path\to\asiosdk`.


Requirements: CMake ≥ 3.22, a C++20 compiler (MSVC 2022, Xcode 15+, GCC 11+/Clang 14+), Ninja recommended.
JUCE 8.0.15 is fetched automatically (or place a checkout in `deps/JUCE`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ff_tests_artefacts/Release/ff_tests          # unit tests (no audio hardware needed)
```

Linux packages (Debian/Ubuntu):
```bash
sudo apt install build-essential cmake ninja-build libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
```

Windows ASIO: download the Steinberg ASIO SDK, then configure with
`-DFF_ENABLE_ASIO=ON -DFF_ASIO_SDK_DIR=C:/path/to/asiosdk` (read the licensing note in the architecture doc).

Headless CI build (core + tests only): `-DFF_BUILD_APP=OFF`.

Packaging: `cpack --config build/CPackConfig.cmake` (NSIS/ZIP on Windows, DMG on macOS, TGZ/DEB on Linux).
Code signing / notarisation is not configured yet (Milestone 8).

## Layout

```
src/core     UI-free, unit-tested: music theory, pitch detector, metronome DSP, tap tempo, project file format
src/engine   real-time audio engine + tuner analysis thread
src/app      JUCE GUI: main window, transport, pages, look-and-feel, project document, device host
tests        unit tests (JUCE UnitTest runner)
docs         architecture & plan
```

## Verification performed (Milestone 2)

- 618 unit-test checks, including: clip layering/crossfade maths, sample-exact clip playback, 44.1→48 kHz resampling
  (pitch and < 0.002 error), mute/solo/gain/pan, loop wrap, loop recording into takes (file bit-identical to the input),
  punch range, count-in, mix/stem/FLAC/16-bit/normalised export, and safe snapshot swapping while the engine runs.
- End-to-end on Linux/JACK: recorded a 110 Hz tone with a one-bar count-in, played it back (output 110 Hz at the same
  level as the file), split and undid, loop-recorded three takes, exported a 24-bit mix whose peak matched the two
  summed tracks, saved as a new project (audio copied in) and reopened it with all clips and waveforms.

## Verification performed for Milestone 1

- 529 unit-test checks: pitch accuracy within 1.5 cents from B0 to D6 at 44.1/48/96 kHz, octave-error and noise robustness;
  sample-accurate metronome timing across block sizes, accents, swing, compound meter, ramp, position alignment;
  lossless project round-trip, corruption/future-version rejection, repair, atomic writes, backup rotation, autosave,
  path-escape protection; engine transport, metering, gain, no fake output.
- End-to-end on Linux against a JACK device: a detuned A2 tone fed into the input read **A2, +12.0 cents, 110.77 Hz**;
  the recorded metronome output had beats exactly 24 000 samples apart at 120 BPM / 48 kHz with the edited accent
  pattern; crash-recovery prompt restored an autosaved session after a forced kill.
- Not yet verified on real Windows/macOS hardware (built only on Linux so far).
