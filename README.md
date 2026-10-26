# Piano

[![Build](https://github.com/Yinyue93/Piano/actions/workflows/build.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/build.yaml)
[![Tests](https://github.com/Yinyue93/Piano/actions/workflows/test.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/test.yaml)
[![Performance](https://github.com/Yinyue93/Piano/actions/workflows/performance.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/performance.yaml)

A physically modeled piano synthesizer plugin using digital waveguide synthesis.

This is a fork of [FigBug/Piano](https://github.com/FigBug/Piano) (SocaLabs) with
stability, real-time safety and usability fixes. See [Changes in this fork](#changes-in-this-fork).

## Features

- Physical modeling synthesis based on digital waveguide string models
- Hammer-string interaction simulation
- Longitudinal string modes
- Soundboard resonance
- Sustain pedal support
- Available as VST, VST3, CLAP, AU (macOS) and LV2 (Linux) plugins, plus a standalone app

## Changes in this fork

**Crashes and audio-thread safety**
- Fixed a heap overflow in the string model at high sample rates or extreme settings
  (the upsampling factor could exceed what the dispersion filters support).
- The string model no longer calls `abort()` on the audio thread when it gets invalid
  settings. A note that can't be built is skipped instead of taking down the host.
- Debug checks in the string model are compiled out of release builds.
- Uninitialized engine state is now initialized. Soundboard changes are applied on the
  audio thread instead of from the parameter callback.

**Crackling with sustain pedal and big chords**
- Notes never finished: a small DC offset on the bridge, plus float rounding in the
  energy measurement, kept silent voices alive forever. With the pedal down they piled up
  until the CPU couldn't keep up, which caused crackling. Each note now goes through a
  2 Hz DC blocker and the energy is accumulated in double precision. In a dense test song,
  sounding voices dropped from up to 35 to 15 and peak CPU from ~290% to ~80% of real time.
- Notes that never made a sound (with *Hammer Spring Constant* at 0, for example) also never
  finished, because their loudest moment was silence too. A note now also finishes once its
  last second is below an absolute floor (−180 dB), after a first second's grace so its
  attack always arrives.
- A note finishes when its last second is 80 dB below its loudest second, but that second
  was a fixed 65,536-sample ring at the string's internal rate, so it shrank at high sample
  rates (to a third of a second at 96 kHz, and none at all at 65,536 Hz) and notes were cut
  early. The energy is now measured at the host rate in 32 blocks per second, so the window
  is a second at any rate. This also saves 256 KB per note.

**MIDI and parameters**
- Handles *All Notes Off* (CC 123), which damps the notes, and *All Sound Off* (CC 120),
  which silences everything at once, soundboard included. A host reset does the same as
  All Sound Off and also drops queued audio and MIDI, so nothing from before it plays after.
- MIDI handling doesn't allocate on the audio thread: its buffers are sized when the host
  starts audio, and held sustain note offs are a fixed table. SysEx is ignored without being
  copied. In a flood that fills the buffers, note offs and All Off are kept over everything
  else, and if even they don't fit, an All Notes Off takes their place.
- MIDI channels play one shared piano, as several players would: a key sounds while any
  channel holds it, so a note off on one channel no longer stops a key another channel
  still holds. Each channel has its own sustain pedal, and All Notes Off, All Sound Off and
  Reset All Controllers (which lifts the pedal) only affect their own channel.
- Host blocks larger than the size the host announced no longer lose audio: the block is
  rendered a step at a time, so the output buffer stays small whatever the block size.
- Note-ons no longer allocate memory: every note's buffers are prepared when the host
  starts audio (about 10 ms) and are only reused afterwards.
- Fixed the *Hammer Position*, *Bridge Impedance*, *Bridge Horizontal Impedance* and
  *Vertical Horizontal Impedance* faders, which were driving the wrong parameters.
- Parameter values are shown with readable precision, and the unit labels are correct
  (`x` marks a multiplier on the engine's per-note value).
- *Soundboard Size* now works. It was disabled, and the soundboard's delay lines were
  too short for the larger sizes.
- *Long Modes* now works: **Normal** (the default, same sound as before) simulates
  longitudinal string modes up to a quarter of the sample rate, **Full** up to Nyquist.
  Full roughly doubles the CPU load, so it's better suited to offline rendering.
- *String Tension* is hidden from the editor. The engine derives tension from the pitch,
  string length and density, so it can't be set on its own. It stays a plugin parameter so
  saved sessions still load.
- *Bridge Mass*, *Bridge Spring*, *Longitudinal Gamma Damped* and *Longitudinal Gamma
  Quadratic Damped* are hidden from the editor for the same reason: the bridge model only
  uses the impedances, and the damper doesn't change the longitudinal losses. They stay
  plugin parameters.
- *Longitudinal Transverse Mix* now works with *Dwgs4* on. The SIMD path, which takes over
  once the hammer leaves the string, applied *Longitudinal Mix* to both longitudinal forces.
- *Vertical Horizontal Impedance* can no longer make the bridge unstable. Its upper range
  could reach a coupling where the bridge equations divide by zero (around 0.83 at the
  default settings) and blow up. The coupling is now kept just below
  √(*Bridge Impedance* × *Bridge Horizontal Impedance*), the limit for a passive bridge, so
  the top of the fader range has no further effect.

- Changing *Downsample*, or a setting that changes how much a note is upsampled (such as
  *String Length* or *Hammer Position*), now takes effect. Each note built its resampling
  filters once, so after such a change it kept the old ones and sounded wrong until the
  plugin was reloaded. The filters are now worked out once for every factor and a note
  picks the right one on each strike.

- Plugin instances set up at the same time no longer share a writable frequency table:
  it's built once, thread-safely, and never changes.

**Editor**
- The editor window can be resized and scales uniformly.

**Tests**
- New edge-case tests (upsampling cap; extreme settings at 22.05, 44.1, 96 and 192 kHz)
  and MIDI tests (All Notes Off damps a held chord, All Sound Off silences it).
- Parameter tests: Soundboard Size and Long Modes change the sound, and preparing notes
  ahead of time doesn't.
- Processor tests run the plugin's own host-facing code, which the suite didn't build before:
  host block sizes from 0 to 5000 samples, reset at every position in the engine block, All
  Sound Off, MIDI floods, MIDI channels, and no heap allocations in `processBlock()`.
- Voice tests: a silent strike finishes, quiet notes aren't taken for silence, a released note
  finishes at the same time from 44.1 to 192 kHz, every key struck at once all finish, and
  instances set up in parallel sound the same.
- Changing *Downsample*, *Hammer Position* or *String Length* and then playing sounds
  exactly like a fresh engine with those settings.
- A missing reference render now fails instead of being written as the new baseline, and
  NaN can no longer slip through the comparisons.

## Building

### Requirements

- CMake 3.24 or higher
- A C++20 compiler
- Platform-specific dependencies:
  - **macOS**: Xcode
  - **Windows**: Visual Studio 2022 or higher
  - **Linux**: GCC/Clang, ALSA, JACK, X11, freetype, fontconfig and curl development
    libraries (see `.github/workflows/build.yaml` for the full list)

### Build Steps

Clone with submodules (JUCE, gin and the CLAP extensions):

```bash
git clone --recurse-submodules https://github.com/Yinyue93/Piano.git
cd Piano
```

If you already cloned without them, run `git submodule update --init --recursive`.

```bash
# Configure
cmake -B build

# Build everything
cmake --build build --config Release

# Or build a single format, e.g. the VST3 or the standalone app
cmake --build build --config Release --target Piano_VST3
cmake --build build --config Release --target Piano_Standalone
```

The built plugins will be in `build/Piano_artefacts/Release/`.

### Installing

Copy the plugin to your system's plugin folder and rescan in your DAW:

| Format | Windows | macOS |
|--------|---------|-------|
| VST3 | `C:\Program Files\Common Files\VST3\` | `~/Library/Audio/Plug-Ins/VST3/` |
| CLAP | `C:\Program Files\Common Files\CLAP\` | `~/Library/Audio/Plug-Ins/CLAP/` |
| AU | | `~/Library/Audio/Plug-Ins/Components/` |

On Windows the `Program Files` folders need an administrator prompt. Close the DAW first,
because it locks the plugin file while it's loaded.

## Using the standalone app

The standalone app (`Piano_artefacts/Release/Standalone/`) can play MIDI files directly:
open the sidebar, load a `.mid` file and press play. Sustain pedal events in the file are
honored.

On Windows, choose **Windows Audio (Low Latency Mode)** as the audio device type for the
lowest latency with ordinary sound cards. If you have an ASIO driver, use that instead.

## Testing

The project includes a test suite with unit tests for internal components and integration
tests for audio output.

### Running Tests

```bash
# Build and run tests
./test.sh

# Run with AddressSanitizer
./test.sh Release ON

# Run in Debug mode
./test.sh Debug
```

On Windows without a bash shell:

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target PianoTests
```

Then run it with the reference renders folder as its argument, from the repository root:

```bash
build/tests/PianoTests_artefacts/Release/PianoTests tests/reference
```

Without the argument it looks in `reference/` under the current folder. A missing
reference fails its test. To write new references after an intended change in the sound,
run it with `--update-references`, listen to the results and commit them.

`--group <name>` runs a single test group (`--list` prints them). CTest runs each group as
its own test, and `ctest -L fast` skips the three renders compared with the references:

```bash
cd build
ctest -C Release -L fast
```

### Test Suite

- **Unit Tests**: Filter, Hammer, MSD2Filter, ResampleFIR, DWGS (Digital Waveguide String) classes
- **Engine Tests**: extreme settings and sample rates, MIDI handling, parameters
- **Processor Tests**: the plugin's own `processBlock()`, driven as a host would: parameter
  IDs, host order and engine mapping, latency and output across host block sizes, the
  sustain pedal, MIDI channels, reset and All Sound Off, full MIDI buffers, and no allocations on the audio
  thread
- **Integration Tests**: Scale test, Chord test, 4-minute stress test, compared against
  reference renders in `tests/reference/`
- **Edge Case Tests**: Upsampling limits and extreme parameter settings at several sample rates
- **MIDI Tests**: All Notes Off damps, All Sound Off silences, and a note struck after
  All Sound Off doesn't bring back the old sound
- **Voice Tests**: when notes finish (silent, quiet and held notes, sample rates, the whole
  keyboard) and instances set up in parallel
- **Parameter Tests**: Soundboard Size, Long Modes, note preparation, and settings changed
  after notes were played

## Known limitations

- If a parameter change makes a note need bigger buffers than it has used so far (for
  example a longer *String Length*), the next strike of that note allocates once on the
  audio thread.
- Moving *Soundboard Size* while notes ring can click, because the soundboard's delay
  lines change length.
- The soundboard's delay lengths are fixed in samples, so its character changes a little
  with the sample rate.

## License

This project is licensed under the GNU General Public License. See [COPYING](COPYING).

## Credits

- Original physical model by Clayton Otey (see [AUTHORS](AUTHORS)).
- Plugin, UI and maintenance by Roland Rabien / [SocaLabs](https://socalabs.com/)
  ([FigBug/Piano](https://github.com/FigBug/Piano)).
- Based on physical modeling research and digital waveguide synthesis techniques.
