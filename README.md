# FretForge Studio

A cross-platform guitar workstation (DAW + tab editor + drum studio + training centre), built in C++20 with JUCE 8.
See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the technology evaluation, architecture, audio-engine design,
project format, plugin system, milestone plan, risks and licensing.

## Status: Milestone 3 (part 1) — Guitar tab editor (working)

| Area | What works now |
|---|---|
| Tab tracks | Any number of tab tracks per project; 4–8 string guitar and bass presets (24 tunings) or custom tunings; capo; mute/solo/volume |
| Editing | Click or arrow-key cursor; type frets (two quick digits for 10+); note values 1–1/64, dotted, triplets, rests; insert/delete beats and bars; per-bar time signatures; copy/cut/paste beats; transpose ±1 semitone (re-strings notes that fall off the neck); full undo |
| Techniques | Hammer-on/pull-off, slides (to next / in / out), bends (½, full, 1½, 2) and pre-bends, vibrato, palm mute, dead and ghost notes, natural and artificial harmonics, let ring, tapping, ties, accents, staccato, pick strokes, tremolo picking |
| Display | Systems of bars with fret numbers, technique markings, rhythm stems with beams/flags/dots/triplets, bar-fill warnings (incomplete / too long), selection and playback cursor |
| Playback | Built-in physically-modelled guitar/bass synth (Karplus-Strong) that performs the techniques; practice speed 25–150% without pitch change; loop the selection; metronome click; count-in |
| Import / export | MIDI import (quantised, automatic string assignment) and export; ASCII tab import (rhythm estimated) |

**Still to come in Milestone 3:** standard notation above the tab, Guitar Pro (.gp3/.gp4/.gp5/.gpx/.gp) and MusicXML import,
MusicXML export, PDF export and printing, MIDI-keyboard note entry, chord diagrams and lyrics, tempo changes inside a score.

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

Sections without working features (Guitar Tabs, Drum Studio, Training, Effects & Amps, Library) still show a labelled
"Not yet implemented — Milestone N" page with their planned scope and no controls.

## Building

**Windows, one click:** install Visual Studio 2022+ ("Desktop development with C++") and Git, then double-click
`build-windows.bat`. It finds the compiler, downloads JUCE, builds, runs the tests, makes a ZIP (and a setup .exe if
NSIS is installed) and offers to launch the app. Options: `clean`, `debug`, `asio C:\path\to\asiosdk`.


Requirements: CMake ≥ 3.22, a C++20 compiler (MSVC 2022, Xcode 15+, GCC 11+/Clang 14+), Ninja recommended.
JUCE 8.0.4 is fetched automatically (or place a checkout in `deps/JUCE`).

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
