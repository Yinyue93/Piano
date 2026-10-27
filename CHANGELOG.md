# Changes in this fork

Changes made in this fork of [FigBug/Piano](https://github.com/FigBug/Piano). The upstream
release history is in [Changelist.txt](Changelist.txt).

**Seek in the MIDI player**
- In the standalone app, clicking or dragging on the MIDI file display moves the
  playhead to that point, while playing or stopped.
- The playhead is drawn on the same tick axis as the notes, so it stays on the
  note being played in files with tempo changes, and a click lands on the note
  under the mouse.

**Export MIDI to WAV**
- The standalone app's export button, next to the MIDI menu, renders the loaded
  file to a 24-bit WAV with the current settings, faster than real time and
  without interrupting playback. The render continues after the file ends until
  the sound has died away (30 seconds at most, then it fades out).
- The WAV's true peak stays at or below -1 dBTP, measured with 4x oversampling as
  in ITU-R BS.1770. **Normalize** brings the peak to -1 dBTP; otherwise the
  rendered level is kept unless it would go over.
- The standalone sidebar scrolls, so every section stays reachable with a short
  editor window.

**gin fork**
- The gin submodule now points at [Yinyue93/Gin](https://github.com/Yinyue93/Gin),
  branch `piano`, which carries the standalone app changes above on top of the
  upstream commit this fork used. Run `git submodule sync` and
  `git submodule update --init` in an existing clone.

**Refined Treble 2 balance and sample-rate consistency**
- Adopted the approved Chopin treble-balance audition: shorter effective hammer
  contact above C5 and cached per-note level compensation, with the original
  MIDI velocity mapping, body response and stereo image.
- Kept this voicing's synthesis at 48 kHz and added filtered output conversion
  for other host rates, preventing upper partials from moving across narrow
  soundboard resonances. Other voicings retain their original processing.
- Latency remains 64 samples at 48 kHz and is approximately 2 ms at other rates,
  reported to the host. MIDI timing, reset, sustain, mono fold-down and callback
  allocations are covered by processor checks.
- The approved 48 kHz WAV is preserved sample for sample. A 900-note sweep over
  44.1, 48, 88.2, 96 and 192 kHz agrees within 0.0015 dB in the measured window.

**Treble voicings**
- Added **Warm Body + Full Treble** (voicing 3) from treble comparison 4: gentler
  Detail EQ and less envelope attenuation on upper notes, fading in from C4 to C6.
- Added **Warm Body + Refined Treble** (voicing 4) from treble-model comparison 4:
  revised hammer contact, string dimensions, damping and bridge coupling fading in
  from C4 to C5, on top of Full Treble's EQ and envelope.
- Added **Warm Body + Refined Treble 2** (voicing 6), the selected A comparison:
  corrects the transverse-force attenuation from internal oversampling (about
  6 dB at 2x and 12 dB at 4x) and leaves F6-C8 undamped on key release.
- The three presets are bundled in the app and include their audition volume and
  60% stereo width. Earlier builds can't reproduce voicings 3, 4 and 6 from the
  preset XML alone; the Warm Body and Warm Body + Detail presets also need this
  fork's build. Existing presets and sessions keep the original model unless they
  select a warm voicing.

**Physical Grand experiment**
- Added **Physical Grand** (voicing 5): modal treble strings with three detuned
  strings and two vibration directions, one shared modeled soundboard,
  sympathetic strings, continuous CC64 damping and a modeled coincident
  microphone pair, with no samples or impulse responses. It reports 96 samples
  of latency.
- Kept as an experiment: it didn't win the listening comparison, which selected
  Warm Body + Refined Treble 2. Details in [docs/physical-grand.md](docs/physical-grand.md).
- Added the `PhysicalPianoTests` suite (`ctest -L physical`) and the
  `PianoAudition` and render tools in `tools/`.

**Stereo width**
- Added a saved, automatable Stereo Width fader with bass-left/treble-right
  placement and quiet, filtered early reflections for every voicing (Physical
  Grand uses its microphone model instead).
- Width changes ramp over 30 ms. Mono fold-down preserves the original signal;
  old presets and sessions retain their sound with a 0% default.
- Stereo body and reflection buffers are prepared before playback and cleared
  on reset and All Sound Off without audio-thread allocations.

**Warm piano presets**
- Added Warm Body and Warm Body + Detail presets, preserving the approved
  convolution-body auditions. The Detail preset adds upper-harmonic EQ and
  a faster per-note initial decay. Presets and sessions retain their voicing;
  existing state without it still uses the original model.
- The convolution uses short head and larger tail partitions for live playing.
  Reset and All Sound Off clear its history without allocating or losing the IR.

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
- Handles *All Notes Off* (CC 123), which releases every key as if lifted, and *All Sound
  Off* (CC 120), which silences everything at once, soundboard included. All Notes Off
  respects the sustain pedal, so with the pedal down the notes keep ringing until it is
  lifted; All Sound Off bypasses it. A host reset does the same as
  All Sound Off and also drops queued audio and MIDI, so nothing from before it plays after.
- MIDI handling doesn't allocate on the audio thread: its buffers are sized when the host
  starts audio, and held sustain note offs are a fixed table. SysEx is ignored without being
  copied. In a flood that fills the buffers, note offs and All Off are kept over everything
  else, and if even they don't fit, an All Sound Off takes their place, so the panic also
  silences notes held by the sustain pedal.
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

**Performance**
- The engine renders 64-sample blocks instead of 32, and each voice renders the whole block
  before the next one starts, so its string and filter state stay in cache. Each output
  sample still sums the voices in the same order. The plugin's latency is now 64 samples
  (1.33 ms at 48 kHz).
- With no upsampling (factor 1) the resampling filter is a plain 32-sample delay instead of
  a 64-tap FIR, with the same output.
- On x86 builds without FMA (the default with MSVC), multiply-adds use SSE multiply and add
  instructions instead of SIMDe's scalar fallback, which MSVC doesn't vectorize, and the
  longitudinal dot product keeps native 128-bit accumulators. Rounding and summation order
  are unchanged. Targets with FMA, ARM included, still use fused multiply-adds.

**Editor**
- The editor window can be resized and scales uniformly.
- A *Time* readout under the faders shows the transport position (the host's, or the
  standalone app's MIDI file player's), so a sound heard while debugging can be found again
  in the song. It shows where the block being rendered starts, so the sound you hear lags
  it by the plugin's latency and the audio device's buffer.

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
- The allocation counter also replaces the `nothrow` forms of `operator new`, so
  AddressSanitizer no longer reports a new/free mismatch from `std::stable_sort`.
- CI builds `PhysicalPianoTests` too, which ctest otherwise reported as not run.
- A missing reference render now fails instead of being written as the new baseline, and
  NaN can no longer slip through the comparisons.
- The reference renders were made about five times louder than the plugin's default and
  clipped (peaks up to 5x full scale), so they crackled when played. They're now rendered
  at a level that stays below full scale, and the comparison thresholds were scaled with
  the level so the tests are as strict as before.
- Resampler tests: the delay, ring-buffer wrapping, `clear()` and factor changes match a
  direct convolution. The longitudinal dot product is checked against a double-precision
  sum for unaligned inputs and lengths on each side of the SIMD and tail boundaries.
- All Notes Off respects the sustain pedal, and an overflowing MIDI buffer still silences
  notes with the pedal down and drops their held note offs.
