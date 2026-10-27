# Warm Body + Refined Treble 2

This is voicing 6, now including the approved treble-balance audition from
`audio-comparisons/chop2511-balance-02/`. The user selected that sound on
2026-10-27. The earlier **Warm Body + Refined Treble** remains voicing 4, with
its original preset values and sound. Select **Warm Body + Refined Treble 2**
in the updated standalone or plugin. Existing sessions selecting voicing 6
receive the approved update; its preset name and parameter IDs are unchanged.

## The changes

1. **Correct the transverse-force scale during oversampling.** The existing
   decimator has unit gain, but the bridge-force conversion also divided by the
   internal resampling ratio. That unnecessarily attenuated oversampled notes
   by approximately 6 dB at 2x and 12 dB at 4x. The new voicing removes that
   decimation attenuation. It retains the separate gain required when the
   interpolation stage inserts zeros for the Downsample option. Longitudinal
   force scaling is unchanged; it represents different discrete quantities.
2. **Leave F6-C8 undamped.** Releasing those keys, including release through the
   sustain pedal, leaves the existing string vibration to decay naturally.
   Lower keys retain their dampers. All Sound Off, reset and preset changes
   still silence the instrument. F6 is a chosen damper boundary, not a claim
   that every grand piano has the same boundary.
3. **Balance hammer contact and output above C5.** The effective hammer mass
   falls smoothly with pitch, while strike velocity retains the original MIDI
   mapping. A fixed per-note output gain compensates the reduced mass and
   supplies a gentle upper-register lift. These are the exact coefficients
   selected in the Chopin 0:15-0:20 audition: with
   `x = max(0, (note - 72) / 12)`, `r = x*x*(2-x)` below `x=1` and `r=x`
   above, mass is multiplied by `2^(-1.7*r)`. Output is multiplied by
   `massRatio^(-0.65) * 10^(2.5*min(1,r)/20)`. Gains are cached per voice;
   there is no compressor, automatic gain or per-sample power calculation.
   Notes at and below C5 retain their previous 48 kHz sound.
4. **Keep synthesis at the approved 48 kHz reference rate.** The waveguide's
   short-delay and dispersion filters otherwise move high partials with the
   host rate. For E-flat7, the second partial moved from approximately 5026 Hz
   at 44.1 kHz to 5009 Hz at 48 kHz and 4982 Hz at 96 kHz. A narrow resonance
   in the existing soundboard amplified those changes into a 6.9 dB level
   difference. Running the same strings, body and stereo processing at 48 kHz
   preserves the approved instrument. Other host rates receive a band-limited
   output conversion, rather than a new instrument calibration.

The hammer law, stiffness, string geometry, tuning, losses, unison
detuning, bridge impedances, per-note EQ/envelope, body response and stereo model
remain those of the selected baseline. The preset has the same control values,
including Volume and Stereo Width.

At 48 kHz the rate converter is bypassed and latency remains 64 samples. At
other rates a 64-tap Blackman-windowed sinc filter uses 1024 interpolated phases,
with its cutoff adjusted to prevent aliasing on downconversion. Total reported
latency is `ceil(96 * hostRate / 48000)` host samples: 89 at 44.1 kHz, 177 at
88.2 kHz, 192 at 96 kHz and 384 at 192 kHz. MIDI timestamps are converted to
reference frames with at most one reference-sample delay. Empty and irregular
host blocks preserve event order. Both engine rates and converter storage are
prepared in advance so changing voicing does not construct an engine in the
audio callback. Reset clears the converter; All Sound Off passes through its
short filter tail. Other voicings retain their original host-rate processing.

The source of new note audio remains the physical string/hammer model. The
baseline already uses an embedded body impulse response with undocumented
provenance; preserving that response was part of preserving the selected sound.
This version adds no recorded notes, excitation samples or new impulse response.
The whole signal path should therefore not be described as entirely physically
modeled.

## Listening and engineering checks

The comparison used identical MIDI and fixed gain to match integrated loudness,
with at least 6 dB sample-peak headroom and no compressor or limiter. In addition
to held notes, the second passage covers velocities 40, 84 and 112, short key
releases, repetitions and a mixed-register phrase. The prior baseline export was
reproduced bit for bit before choosing a candidate.

The rate-scaling diagnosis used the same C5/C6 notes with forced 1x/2x/4x internal
rates. Corrected levels varied by less than 0.6 dB instead of losing about 6 dB
per doubling. The production change does not force higher internal rates.

Processor regression checks cover baseline equality through C5 at 48 kHz,
conversion latency and MIDI boundaries, cross-rate note levels, the E6/F6
damper boundary, held versus released upper keys, repeated
strikes, sustain, channel ownership, panic/reset, irregular and empty host
blocks, stereo fold-down, state/preset persistence, and allocation-free audio
callbacks at 44.1, 48 and 96 kHz.

Upper notes now overlap after key release. A spectrum of the C8 passage can
include the previous C7's tail; it must not be presented as an isolated C8
measurement. Spectral centroids, fundamental percentages and passing stability
tests are not substitutes for the listening comparison.

## Earlier verification (2026-10-26)

- The production treble export is PCM-bit-identical to the selected candidate A.
  The production baseline export is also PCM-bit-identical to the earlier
  baseline. Results are saved in
  `audio-comparisons/refined-treble-2/selection-verification.json`.
- All 14 test groups passed across the full regression run and the final
  processor rerun. The new held/released-key check initially required identical
  bits despite the extra MIDI event splitting a convolution call. Measured
  differences were below -152 dB relative energy. The test now uses a -120 dB
  tolerance; the audio implementation was not changed to satisfy it.
- Final listening exports are 48 kHz, 24-bit stereo at -23 LUFS, with peaks from
  -8.26 to -6.93 dBFS. The baseline preset file's SHA-256 is unchanged:
  `434DB3A3B2A8FC54F6EB2548CDAE69EA88F963A7836E2155913B27C256CF5395`.

On the development i5-1235U, an eight-second, 16-note sustained passage at 48 kHz
and 128-sample blocks took 2.76-3.52 seconds for the baseline and 2.89-3.82 seconds
for Refined Treble 2. Reversing the order showed substantial run-order effects;
both versions averaged the same 15.20 active voices. The new version's median
block time ranged from 0.94 to 1.27 ms and its 99th percentile from 1.45 to 1.95 ms,
against a 2.67 ms deadline. Both versions had occasional blocks over that deadline
in this background-console test. These results describe processing cost, not a
guarantee of dropout-free live audio.

## Balance update verification (2026-10-27)

The production 48 kHz Chopin excerpt has **zero differing PCM samples** from
the approved `chop2511-balance-02` audition. Its treble stem is approximately
2.5 dB below the lower accompaniment, compared with 16 dB before the balance
correction. The MIDI, pedal, preset controls and output gain are identical.

Across 900 isolated-note renders (C-sharp5-C8, velocities 32/56/84/112/127,
44.1/48/88.2/96/192 kHz), the largest stereo RMS difference from 48 kHz is
**0.001421 dB**, measured 50-250 ms after onset with processor latency removed.
Before reference-rate processing, the worst case was approximately 6.9 dB.
The final release build's CSV, verification report and three sample-rate
excerpts are in `audio-comparisons/refined-treble-2-release/`.

All 14 regression groups passed. Processor checks cover reference-rate
conversion, reported latency, MIDI timing across irregular and empty blocks,
reset, sustain, stereo fold-down, and allocation-free callbacks.
The final processor rerun also passed switching between native-rate and
reference-rate voicings through empty callbacks: latency updates correctly,
switching allocates no memory, and returning to Refined Treble 2 matches an
explicit reset sample for sample. The comparison uses previously played
engines on both sides so their initial filter priming is equivalent.

These comparisons establish preservation of the chosen sound and agreement
across rates; they do not establish realism against an acoustic-piano recording.

## Reproduce

```sh
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target Piano_Standalone Piano_VST3 PianoTests RefinedTrebleAudition
ctest --test-dir build -C Release --output-on-failure
build/tests/Release/RefinedTrebleAudition new-output-folder
build/tests/Release/RefinedTrebleAudition --benchmark
build/tests/Release/RefinedTrebleAudition --benchmark-reverse
build/tests/Release/RefinedTrebleAudition --release-check
```

For the approved balance and sample-rate verification, run from the repository
root with `midi/chop2511.mid` present:

```sh
cmake --build build --config Release --target BalancedTrebleAudition
build/tests/Release/BalancedTrebleAudition new-output-folder audio-comparisons/chop2511-balance-02/02-balanced-treble-15s-20s.wav
```

This checks 900 isolated notes over 44.1, 48, 88.2, 96 and 192 kHz, compares
the production 48 kHz passage with the approved WAV, and exports the same
passage at 44.1, 48 and 96 kHz using identical output gain. It writes the
measurements and WAV read-back checks alongside the files.

The console executable is under `build/tests/` on a single-configuration
generator. The renderer uses the production processor, reads the exported
24-bit WAVs back, and checks their loudness, format and headroom. It refuses to
overwrite an existing output directory.

This is a focused improvement to the selected baseline, not a complete
calibration of a concert grand. Existing limitations in velocity-dependent
excitation, sympathetic resonance and body modeling remain. Further changes
should be evaluated separately against this selected sound.
