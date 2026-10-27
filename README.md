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
- **Physical Grand** voicing with a shared modeled soundboard, modal treble,
  sympathetic strings and continuous sustain-pedal damping
- Available as VST, VST3, CLAP, AU (macOS) and LV2 (Linux) plugins, plus a standalone app

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

The export button next to the MIDI menu renders the loaded file to a 24-bit WAV with the
current settings, faster than real time and without interrupting playback. The render goes
on after the file ends until the sound has died away (30 seconds at most, then it fades out).
The WAV never clips: its true peak stays at or below -1 dBTP. With **Normalize** ticked, the
peak is brought up or down to -1 dBTP. Without it, the level is kept as rendered unless it
would go over, and then it's turned down just enough.

On Windows, choose **Windows Audio (Low Latency Mode)** as the audio device type for the
lowest latency with ordinary sound cards. If you have an ASIO driver, use that instead.

## Warm piano presets

The `presets/` folder contains five warm voicings:

- **Warm Body**: an alternative convolution soundboard.
- **Warm Body + Detail**: that body with additional upper-harmonic detail and a
  faster initial decay for each note.
- **Warm Body + Full Treble**: gentler Detail EQ and less envelope attenuation
  on upper notes, at 60% stereo width. The correction fades in from zero at C4
  to full strength at C6 and above.
- **Warm Body + Refined Treble**: Full Treble's EQ and envelope with revised
  hammer contact, string dimensions, damping and bridge coupling, at 60% stereo
  width. The physical changes fade in from C4 to C5; notes at or below C4 use the
  original model.
- **Warm Body + Refined Treble 2**: Refined Treble without the transverse-force
  attenuation caused by internal oversampling, and with F6-C8 left undamped on
  key release. Hammer contact shortens above C5 and each note's level is
  compensated, bringing out the treble without changing the MIDI dynamics.
  Synthesis runs at 48 kHz, with filtered output conversion at other host rates.
  See [the implementation and validation notes](docs/refined-treble-2.md).

Select a voicing by name from the preset menu. **Warm Body + Full Treble**,
**Warm Body + Refined Treble** and **Warm Body + Refined Treble 2** are bundled
in the app. On Windows, user presets are XML files in
`%APPDATA%\com.socalabs\Piano\programs`; copy the Warm Body and Warm Body + Detail
files there when installing on another computer.

Switching voicings stops the previous notes. The three Soundboard controls affect
the original model; warm voicings use a fixed body response. Other piano
parameters remain editable, and saved presets retain the selected voicing. Each
preset's Volume is set for a matched listening level; adjust it as needed for
other music.

The body response is the project's embedded IR, with undocumented recording
provenance, recorded at 48 kHz. Refined Treble 2 synthesizes at that rate; the
other warm voicings resample the response for the host rate. The Detail mode uses
fixed EQ and a per-note decay envelope, with no sampled-piano library or automatic
loudness processing. Full Treble's note-specific EQ and envelope are calculated at
the active sample rate, with separate filter state for each voice.

Refined Treble's treble scale targets 900 N tension and diameters from 0.96 mm at
C5 to 0.80 mm at C8. These are voicing choices, not measurements of a specific
instrument. String Length and String Radius remain adjustable multipliers.
Refined Treble 2 has 64 samples of latency at 48 kHz and approximately 2 ms at
other rates, reported to the host.

## Physical Grand experiment

**Physical Grand** is an experimental voicing; **Warm Body + Refined Treble 2**
is the recommended sound. It uses modeled strings, felt contact and soundboard
radiation throughout its signal path, including the body response, with no
recorded piano samples or impulse responses.

From C4 upward it uses a modal string model with three detuned strings, two
vibration directions, stretch tuning and frequency-dependent decay. Strings
exchange energy through one shared soundboard. Released keys above E6 continue
ringing naturally, and CC64 controls damper pressure continuously. Bass notes
use the waveguide string solver and drive the same soundboard. Stereo comes
from two modeled coincident microphone responses; **Stereo Width** controls the
blend with mono.

It reports 96 samples of DSP latency (2 ms at 48 kHz). Its physical parameters
describe a designed instrument and need listening-based voicing; the bass
feedback path and the sympathetic bank use reduced approximations. See
[the model and validation notes](docs/physical-grand.md) for details,
reproducible auditions, performance measurements and remaining limitations.

## Stereo width

The **Stereo Width** fader blends from the original mono sound at **0%** to a
wider player-perspective image at **100%**: bass toward the left, treble toward
the right, with quiet early reflections adding space around individual notes.
Try **60%** as a starting point for a grand-piano recording feel.

Width works with all seven voicings. Physical Grand uses the microphone model
described above; the other six use keyboard placement and early reflections.
Changes are smoothed over 30 ms, and the setting is saved in presets and
sessions, defaulting to 0%. Averaging the two channels preserves the original
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

Build `PhysicalPianoTests` as well to run the Physical Grand model checks with
`ctest -C Release -L physical`. `PianoAudition` exports matched-level comparisons
through the production processor; commands are in the model notes linked above.

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
