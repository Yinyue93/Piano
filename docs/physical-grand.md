# Physical Grand

**Experimental; rejected in listening.** The numerical checks below did not
establish piano realism. The selected continuation uses the original Refined
Treble engine: see [Refined Treble 2](refined-treble-2.md).

Physical Grand is voicing 5, bundled as a separate preset. Existing sessions
retain their selected voicing and parameter IDs. Switching voicings clears the
previous sound. Use the updated standalone or plugin to load this preset.

## Sound generation

The audio path contains no recorded excitation, soundboard impulse response,
piano samples or added noise recordings. The older warm presets still use their
existing convolution body, independently of this voicing.

| Part | Implementation |
| --- | --- |
| C4–C8 struck strings | Mass-normalized stiff-string modes; three unisons and two transverse polarizations; up to 48 partials per string and polarization, with a physical bandwidth limit |
| Hammer | Nonlinear felt compression, velocity-dependent dissipation and a contact solve; force cannot pull the string |
| Repeated strikes | Existing modal displacement and velocity survive the new hammer strike |
| Bridge | Reciprocal inertial coupling to four regional soundboard ports; the coupled system is solved together |
| Soundboard | Global orthotropic-plate modes below the transition region, with four local rib-region bases above it; up to 180 modes |
| Radiation | Four coherent radiating patches, observed by coincident angled cardioids; output follows soundboard acceleration |
| Other strings | Reduced passive sympathetic bank, enabled by lifted dampers; undamped upper strings remain open |
| Duplex | Two weak, unstruck secondary resonators per struck treble note, where the bandwidth permits |
| Bass | Existing waveguide strings below C4, feeding the same modeled soundboard |

The scale uses nominal 900 N string tension, a gradual wire-diameter curve,
note-dependent hammer mass and stiffness, and separate fundamental and partial
decay rates. A smooth stretch curve reaches 48 cents at C8. Unison offsets vary
deterministically with the key; no random phase reset or per-strike randomization
is applied. The parameter anchors in `PianoPhysical.cpp` describe this designed
scale. Reference recordings informed the initial pitch, balance and decay targets.

Bridge coupling follows from writing each string's motion as its fixed-end modal
sum plus `x/L` times bridge displacement. The kinetic-energy cross term supplies
reciprocal coupling; endpoint mass and tension stiffness remain when a damped
string is parked. This avoids adding a dissipative load for every silent string.
String loss, soundboard loss and felt hysteresis dissipate mechanical energy.

## Pedal and stereo

CC64 maps to continuous damper pressure through `(1 - pedal)^3`, with an 8 ms
approach to the requested damping. F6–C8 have no damper in this scale. CC121 resets
the transmitting channel's pedal. The maximum pedal position received across
channels drives the instrument's one physical sustain pedal. Key ownership and
All Sound Off remain channel-aware; a shared soundboard naturally contains energy
from all notes until the last sounding channel is silenced.

At zero stereo width, both outputs contain the same mono sum. Increasing width
adds the difference between the two modeled cardioids, with a 30 ms ramp. The
coincident pair avoids the inter-microphone delay used by a spaced pair. This
radiation model supplies direct instrument sound; room reverberation is an
additional production choice.

## Numerical and real-time behavior

The mechanical update runs at twice the host sample rate. Implicit midpoint
integration with frequency prewarping preserves the intended uncoupled modal
frequencies. The felt solver uses a discrete energy gradient. String state uses
SIMD float arithmetic; the small shared bridge system and hammer solve use double
precision. An AVX2 kernel is selected when both the CPU and operating system
support it; the portable SSE2/NEON path remains available. The diagnostic
configuration also supports 4× processing and forcing the portable kernel.

An explicit decimation filter adds 32 host samples to the existing 64-sample
processor FIFO. The plugin therefore reports 96 samples for this voicing and
64 for the earlier voicings. Audio callbacks use prepared storage. Very quiet
tails are parked while preserving key and pedal state.
Full string coefficients for ordinary strikes are cached during preparation;
changing physical parameters refreshes the affected note's coefficients on its
next strike.
Inaudible upper mode groups are parked below a mechanical-energy floor of
`1e-18`, while the duplex and at least four speaking-string partials remain.
New key presses restore the full groups. A comparison against the unpruned
calculation bounds the resulting audio difference.

Young's modulus, density, string-length/radius multipliers, hammer parameters,
decay, detuning, bridge impedances, maximum velocity, volume and stereo width
affect this model. Treble coefficient changes
take effect on the next strike. The legacy soundboard-size/decay/lopass controls
do not replace this fixed physical body. Legacy longitudinal, resampling and
damped-string controls retain their previous waveguide-bass behavior. String
Tension and the legacy Bridge Mass/Spring controls do not alter the modal treble's
designed scale and plate. Treble
damper behavior and oversampling are part of the new model's fixed design.

## Reproduce the checks and comparisons

Configure with tests enabled, then build the engine checks and production renderer:

```sh
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target PianoTests PhysicalPianoTests PianoAudition TrebleAudit
ctest --test-dir build -C Release --output-on-failure
```

On Windows, the additional console tools are under `build/tests/Release/`.
On a single-configuration generator they are under `build/tests/`.

```sh
build/tests/Release/PianoAudition new-audition-folder
build/tests/Release/PianoAudition --levels levels.csv
build/tests/Release/PianoAudition --benchmark
build/tests/Release/PianoAudition --benchmark 1024
build/tests/Release/PhysicalPianoTests --survey treble-survey.csv
build/tests/Release/PhysicalPianoTests --benchmark
build/tests/Release/TrebleAudit baseline-audit-folder
python tools/analyze-physical-grand.py new-audition-folder
```

The optional spectrum-analysis script requires NumPy.

`PianoAudition` renders the actual plugin processor. The treble comparison uses
isolated C5/C6/C7/C8, octaves and a melody. The pedal comparison repeats the same
figure with pedal up, halfway down and fully down, then adds bass and repeated
treble strikes. Each pair uses identical MIDI velocities and matched integrated
loudness. Files are 48 kHz, 24-bit stereo, with latency alignment, fixed gain,
at least 6 dB sample-peak headroom and a 10 ms final fade. There is no limiter or
compressor. The renderer refuses to overwrite an existing audition.

`TrebleAudit` measures the previous Refined Treble model's raw string output,
hammer force and final output. Its original C8 raw RMS was about 24 dB below C7
in the 50–250 ms window at velocity 84. That loss occurred before the soundboard.
The new model addresses excitation, coupling and decay together.

The standalone engine checks cover passive note and chord energy, silent reset,
undamped upper keys, sympathetic response, half pedal, increasing hammer dynamics,
retained state on repetition and bounded extreme parameters. Output energy must
agree within 0.8 dB across 44.1/48/96 kHz and 2×/4× processing for the tested treble
notes. Processor checks cover preset round trips, irregular host blocks, latency,
channel panic, stereo fold-down and allocation-free processing at default settings.

## Validation results (2026-10-26)

The full Release suite passed all 14 tests. After the final SIMD, coefficient-cache
and quiet-mode optimizations, the affected processor and physical-engine suites
passed again. The optimized engine's chord/restrike render differed from the
unpruned calculation by -97.5 dB RMS relative to the reference. Separate 32-bit
AddressSanitizer smoke renders covered the largest C4 mode bank and C8. The prior
Full and Refined Treble comparison renders were preserved within 7e-8 RMS error.

The final listening files are in `audio-comparisons/physical-grand-final/`.
Both treble exports measure -23 LUFS; both pedal exports measure -23.49 LUFS.
The analyzer uses the mono sum, a 30-370 ms window after each isolated note's
onset, and a Hann-windowed power spectrum. These measurements describe that
audition; they do not establish perceptual realism by themselves.

| Measurement | Refined Treble | Physical Grand |
| --- | ---: | ---: |
| C8 level relative to C5 | -37.46 dB | -17.03 dB |
| C8 energy within 5% of its intended fundamental | 17.38% | 98.91% |
| C8 energy below 80% of its intended fundamental | 82.58% | less than 0.01% |

The uppermost note now carries its intended pitch much more clearly, and the
level collapse toward C8 is substantially reduced. Further listening should
focus on attack character and the C7/C8 decay, as well as balance within chords.

On the development Intel Core i5-1235U, the production-processor benchmark played
16 simultaneous mixed-register notes with sustain down for eight seconds at
48 kHz. Results below are background-console timings, without an audio-device
callback or real-time scheduling guarantee.

| Buffer | Total render time | Median block | 99th percentile | Worst block | Block deadline |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 samples | 6.02 s | 1.94 ms | 2.67 ms | 4.85 ms | 2.67 ms |
| 1024 samples | 5.95 s | 14.61 ms | 21.28 ms | 21.88 ms | 21.33 ms |

This is about 75% of one core's available processing time on average. Some blocks
exceeded their deadlines at both sizes, so these results do not establish
dropout-free live performance. Live device testing and further optimization
remain necessary for dependable low-latency playing under heavy sustain.

## Limits and further voicing

- The plate, rib regions, bridge participation and radiating areas are reduced
  design parameters. They have not been fitted to measured bridge admittances
  or a finite-element model of a specific concert grand.
- Struck bass waveguides drive the body in one direction. Their reduced modal
  counterparts provide sympathetic response; a fully reciprocal bass solver
  remains an extension.
- An unstruck note uses four partials in each polarization, with one collective
  unison coordinate for sympathy. A strike expands that vibration into three
  separate strings and restores the higher partials and duplex modes.
  Damped, quiet notes keep their static endpoint load while their modes are parked.
- Treble longitudinal vibration, detailed damper contact geometry, una corda,
  sostenuto and lid/room propagation are not implemented in this voicing.
- Perceptual quality still requires listening across soft and loud playing,
  chords, repetitions and pedaling. Automated energy and spectrum checks establish
  stability and measurable changes; the comparison recordings support the next
  voicing decisions.

Further physical calibration should prioritize measured bridge mobility and
soundboard radiation, then hammer-force/contact data at several velocities.
Recorded grand-piano notes can remain analysis references while synthesis stays
entirely modeled.
