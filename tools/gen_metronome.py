#!/usr/bin/env python3
"""Synthesizes the bundled metronome sound sets (docs/dev/audio-engine.md section 7, decision 2026-10-26).

Each set is three mono 48 kHz PCM16 WAV files: accent.wav (downbeat), beat.wav, sub.wav (subdivision).
Deterministic: the same script always produces the same bytes (seeded noise), so the files are build
outputs, not sources. Usage: gen_metronome.py --out <dir>  ->  <dir>/<set-id>/{accent,beat,sub}.wav
"""

import argparse
import math
import os
import random
import struct
import wave

SAMPLE_RATE = 48000
SETS = ["click", "woodblock", "rim", "beep"]


def envelope(n, attack, decay):
    """Linear attack over `attack` samples, exponential decay with time constant `decay` samples."""
    out = []
    for i in range(n):
        a = min(1.0, i / attack) if attack > 0 else 1.0
        out.append(a * math.exp(-max(0, i - attack) / decay))
    return out


def sine(n, freq, sweep=0.0):
    """Sine whose frequency falls by `sweep` Hz over the clip (0 = none); phase accumulated per sample."""
    out = []
    phase = 0.0
    for i in range(n):
        f = freq - sweep * i / n
        phase += 2.0 * math.pi * f / SAMPLE_RATE
        out.append(math.sin(phase))
    return out


def noise(n, seed):
    rng = random.Random(seed)
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def bandpass(samples, low, high):
    """Two one-pole filters in series: high-pass at `low`, low-pass at `high`. Enough for a noise burst."""
    out = []
    lp = 0.0
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    a_lp = math.exp(-2.0 * math.pi * high / SAMPLE_RATE)
    a_hp = math.exp(-2.0 * math.pi * low / SAMPLE_RATE)
    for x in samples:
        lp = (1.0 - a_lp) * x + a_lp * lp
        hp = a_hp * (hp_prev_out + lp - hp_prev_in)
        hp_prev_in = lp
        hp_prev_out = hp
        out.append(hp)
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] if i < len(p) else 0.0 for p in parts) for i in range(n)]


def scale(samples, gain):
    return [s * gain for s in samples]


def shaped(samples, env):
    return [s * e for s, e in zip(samples, env)]


def ms(value):
    return int(SAMPLE_RATE * value / 1000.0)


def click(role):
    freq = {"accent": 3200.0, "beat": 2400.0, "sub": 2000.0}[role]
    n = ms(25)
    tone = shaped(sine(n, freq), envelope(n, ms(0.2), ms(3)))
    burst = shaped(bandpass(noise(n, 1), 1500.0, 8000.0), envelope(n, 0, ms(1.5)))
    gain = {"accent": 0.9, "beat": 0.7, "sub": 0.45}[role]
    return scale(mix(tone, scale(burst, 0.6)), gain)


def woodblock(role):
    freq = {"accent": 1100.0, "beat": 820.0, "sub": 820.0}[role]
    n = ms(70)
    body = shaped(sine(n, freq, sweep=freq * 0.15), envelope(n, ms(0.5), ms(12)))
    knock = shaped(bandpass(noise(n, 2), 600.0, 4000.0), envelope(n, 0, ms(2)))
    gain = {"accent": 0.9, "beat": 0.75, "sub": 0.4}[role]
    return scale(mix(body, scale(knock, 0.35)), gain)


def rim(role):
    n = ms(40)
    low, high = {"accent": (1200.0, 9000.0), "beat": (1000.0, 7000.0), "sub": (1000.0, 7000.0)}[role]
    burst = shaped(bandpass(noise(n, 3), low, high), envelope(n, 0, ms(5)))
    ring = shaped(sine(n, 1800.0 if role == "accent" else 1400.0), envelope(n, ms(0.5), ms(6)))
    gain = {"accent": 0.9, "beat": 0.7, "sub": 0.4}[role]
    return scale(mix(burst, scale(ring, 0.5)), gain)


def beep(role):
    freq = {"accent": 1760.0, "beat": 1320.0, "sub": 1320.0}[role]
    length = {"accent": 60, "beat": 50, "sub": 30}[role]
    n = ms(length)
    env = [min(1.0, i / ms(2), (n - i) / ms(6)) for i in range(n)]
    gain = {"accent": 0.8, "beat": 0.65, "sub": 0.35}[role]
    return scale(shaped(sine(n, freq), env), gain)


GENERATORS = {"click": click, "woodblock": woodblock, "rim": rim, "beep": beep}


def write_wav(path, samples):
    peak = max(abs(s) for s in samples)
    norm = 0.95 / peak if peak > 0.95 else 1.0
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * norm)) * 32767)) for s in samples)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="output folder; one subfolder per set")
    args = parser.parse_args()
    for set_id in SETS:
        folder = os.path.join(args.out, set_id)
        os.makedirs(folder, exist_ok=True)
        for role in ("accent", "beat", "sub"):
            write_wav(os.path.join(folder, role + ".wav"), GENERATORS[set_id](role))
    print("metronome sets: " + ", ".join(SETS))


if __name__ == "__main__":
    main()
