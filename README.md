# Piano

[![Build](https://github.com/Yinyue93/Piano/actions/workflows/build.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/build.yaml)
[![Tests](https://github.com/Yinyue93/Piano/actions/workflows/test.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/test.yaml)
[![Performance](https://github.com/Yinyue93/Piano/actions/workflows/performance.yaml/badge.svg?branch=master)](https://github.com/Yinyue93/Piano/actions/workflows/performance.yaml)

A physically modeled piano synthesizer plugin using digital waveguide synthesis.

This is a fork of [FigBug/Piano](https://github.com/FigBug/Piano) (SocaLabs) with
stability, real-time safety and usability fixes. See [CHANGELOG.md](CHANGELOG.md).

## Features

- Physical modeling synthesis based on digital waveguide string models
- Hammer-string interaction simulation
- Longitudinal string modes
- Soundboard resonance
- Adjustable stereo width with keyboard placement and subtle early reflections
- Sustain pedal support
- Available as VST, VST3, CLAP, AU (macOS) and LV2 (Linux) plugins, plus a standalone app

## Changes in this fork

Fixes for crashes and audio-thread safety, crackling with the sustain pedal and big chords,
MIDI handling and broken parameters, plus performance work, a resizable editor and a much
larger test suite. See [CHANGELOG.md](CHANGELOG.md) for the full list.

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
honored. The *Time* readout in the editor follows the file's position.

On Windows, choose **Windows Audio (Low Latency Mode)** as the audio device type for the
lowest latency with ordinary sound cards. If you have an ASIO driver, use that instead.

## Warm piano presets

The `presets/` folder contains two voicings selected from the listening comparisons:

- **Warm Body**: the alternative convolution soundboard (comparison 1).
- **Warm Body + Detail**: that body with additional upper-harmonic detail and a
  faster initial decay for each note (comparison 4).

In the updated app, select either name from the preset menu. On Windows, user
presets are XML files in `%APPDATA%\com.socalabs\Piano\programs`; copy the two
files there when installing this build on another computer. These presets need
the updated build: older versions cannot reproduce their voicing.

Existing presets and sessions use the original model unless they explicitly
select a warm voicing. Switching voicings stops the previous notes. The three
Soundboard controls affect the original model; warm voicings use a fixed body
response. Other piano parameters remain editable, and saved presets retain the
selected voicing. Their Volume setting matches the approved audition level;
adjust it as needed for other music.

The response is the project's existing embedded IR, with undocumented recording
provenance. The audition used it at 48 kHz; other rates resample it from that
reference. The Detail mode uses fixed EQ and a per-note decay envelope, with no
sampled-piano library or automatic loudness processing.

## Stereo width

The **Stereo Width** fader blends from the original mono sound at **0%** to a
wider player-perspective image at **100%**: bass toward the left, treble toward
the right, with quiet early reflections adding space around individual notes.
Try **60%** as a starting point for a grand-piano recording feel.

Width works with Classic, Warm Body and Warm Body + Detail. Changes are smoothed
over 30 ms, and the setting is saved in presets and sessions. Existing presets
and sessions default to 0%. Averaging the two channels preserves the original
mono signal; stereo peaks can be higher, so leave some output headroom. This is
a modeled stereo image, not a sampled microphone recording or a room reverb.

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
