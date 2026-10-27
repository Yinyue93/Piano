"""Measure the isolated C notes in PianoAudition's exports (requires NumPy)."""

import argparse
import json
import math
import wave
from pathlib import Path

import numpy as np


def read_wave(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 3, 48000):
            raise ValueError(f"Expected a 48 kHz, 24-bit stereo audition: {path}")
        data = np.frombuffer(source.readframes(source.getnframes()), dtype=np.uint8).reshape(-1, 3)
    samples = data[:, 0].astype(np.int32) | (data[:, 1].astype(np.int32) << 8) | (data[:, 2].astype(np.int32) << 16)
    samples = (samples ^ 0x800000) - 0x800000
    return (samples / 8388608.0).reshape(-1, 2).mean(axis=1)


def rms_db(samples):
    return 10 * math.log10(max(1e-30, float(np.mean(samples * samples))))


def analyze(path):
    audio = read_wave(path)
    rows = []
    for octave, cents in zip(range(5, 9), (1.5, 7, 18, 48)):
        onset = .2 + 2 * (octave - 5)

        def window(begin, end):
            return audio[round((onset + begin) * 48000):round((onset + end) * 48000)]

        samples = window(.03, .37)
        spectrum = np.abs(np.fft.rfft(samples * np.hanning(len(samples)))) ** 2
        frequencies = np.fft.rfftfreq(len(samples), 1 / 48000)
        fundamental = 440 * 2 ** (((octave + 1) * 12 - 69 + cents / 100) / 12)
        total = float(spectrum.sum())
        rows.append({
            "note": f"C{octave}",
            "rms_dbfs": round(rms_db(samples), 3),
            "centroid_hz": round(float(np.dot(frequencies, spectrum)) / total, 1),
            "fundamental_band_percent": round(float(spectrum[(frequencies > .95 * fundamental) &
                                                             (frequencies < 1.05 * fundamental)].sum()) / total * 100, 2),
            "below_fundamental_percent": round(float(spectrum[frequencies < .8 * fundamental].sum()) / total * 100, 2),
            "early_late_drop_db": round(rms_db(window(.05, .2)) - rms_db(window(.8, 1.4)), 3),
        })
    return rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    result = {name: analyze(args.folder / name) for name in
              ("01-refined-treble.wav", "02-physical-grand-treble.wav")}
    result["windows_seconds_after_note_on"] = {"spectrum": [.03, .37], "early": [.05, .2], "late": [.8, 1.4]}
    print(json.dumps(result, indent=2))
