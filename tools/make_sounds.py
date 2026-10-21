#!/usr/bin/env python3
"""Synthesises the move sounds in Resources/sounds (written for this project and
dedicated to the public domain, CC0 1.0). Standard library only; the output is
the same on every run.

  python3 tools/make_sounds.py Resources/sounds
"""
import math, os, random, struct, sys, wave

RATE = 44100


def write(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        peak = max(1e-9, max(abs(s) for s in samples))
        w.writeframes(b"".join(struct.pack("<h", int(32767 * 0.8 * s / peak)) for s in samples))


def click(ms, tone, noise, decay, seed):
    """A short knock: a decaying low tone plus a burst of low-passed noise."""
    rng = random.Random(seed)
    n = int(RATE * ms / 1000)
    out, lp = [], 0.0
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * decay)
        lp += 0.25 * (rng.uniform(-1, 1) - lp)                 # crude low-pass
        out.append(env * (math.sin(2 * math.pi * tone * t) * 0.6 + lp * noise))
    return out


def chime(freqs, ms, decay):
    n = int(RATE * ms / 1000)
    return [math.exp(-i / RATE * decay) * sum(math.sin(2 * math.pi * f * i / RATE) for f in freqs) / len(freqs)
            for i in range(n)]


def silence(ms):
    return [0.0] * int(RATE * ms / 1000)


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
write(os.path.join(out, "move.wav"), click(70, 180, 1.2, 70, 1))
write(os.path.join(out, "capture.wav"), click(50, 240, 1.8, 90, 2) + silence(25) + click(70, 160, 1.6, 70, 3))
write(os.path.join(out, "check.wav"), chime([660, 990], 180, 18))
write(os.path.join(out, "end.wav"), chime([523, 659, 784], 600, 5))
with open(os.path.join(out, "LICENSE.txt"), "w") as f:
    f.write("move.wav, capture.wav, check.wav, end.wav: synthesised for Chess-GUI-for-UCI by\n"
            "tools/make_sounds.py and dedicated to the public domain under CC0 1.0\n"
            "(https://creativecommons.org/publicdomain/zero/1.0/).\n")
