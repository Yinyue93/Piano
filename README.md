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

**MIDI and parameters**
- Handles *All Notes Off* and *All Sound Off* (CC 123 / 120), and resets voices when the
  host resets the plugin.
- MIDI is no longer allocated per block on the audio thread.
- Fixed the *Hammer Position*, *Bridge Impedance*, *Bridge Horizontal Impedance* and
  *Vertical Horizontal Impedance* faders, which were driving the wrong parameters.
- Parameter values are shown with readable precision.

**Editor**
- The editor window can be resized and scales uniformly.

**Tests**
- New edge-case tests (upsampling cap; extreme settings at 22.05, 44.1, 96 and 192 kHz)
  and MIDI tests (All Notes Off / All Sound Off damp a held chord).

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

Then run the `PianoTests` executable from the build folder.

### Test Suite

- **Unit Tests**: Filter, Hammer, MSD2Filter, ResampleFIR, DWGS (Digital Waveguide String) classes
- **Integration Tests**: Scale test, Chord test, 4-minute stress test, compared against
  reference renders in `tests/reference/`
- **Edge Case Tests**: Upsampling limits and extreme parameter settings at several sample rates
- **MIDI Tests**: All Notes Off / All Sound Off

## Known limitations

- *String Tension*, *Long Modes* and *Soundboard Size* are shown in the editor but currently
  have no effect on the sound.
- Some parameter unit labels don't match the values they show.
- Changing string parameters while notes are playing can still allocate memory on the
  audio thread.

## License

This project is licensed under the GNU General Public License. See [COPYING](COPYING).

## Credits

- Original physical model by Clayton Otey (see [AUTHORS](AUTHORS)).
- Plugin, UI and maintenance by Roland Rabien / [SocaLabs](https://socalabs.com/)
  ([FigBug/Piano](https://github.com/FigBug/Piano)).
- Based on physical modeling research and digital waveguide synthesis techniques.
