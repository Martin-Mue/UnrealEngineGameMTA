"""Erzeugt den Wasserfall-Loop fuer die Wildnis Verdanta (nur Python-Standardbibliothek).

Ausgabe: Content/Audio/Generated/Wilderness/amb_waterfall.wav (16 Bit, 44,1 kHz, Stereo, 12 s, nahtlos).
Klang: tiefes Grollen (braunes Rauschen), breites Rauschen des Falls (rosa, Bandpass), feine Gischt
(Hochpass) und langsam schwankende Lautstaerke je Kanal. Zufall nur ueber random.Random(Seed).

Aufruf:  python tools/audio/make_waterfall_sound.py
Danach Import in Unreal (ohne Fenster):
  "F:\\UnrealEngine\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" "E:\\Projekte\\GTA 6\\MTA6.uproject"
      -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_wilderness_sounds.py" -unattended -nosplash -stdout
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from synth_common import OUT_DIR, SR, bandpass, highpass, lowpass, make_seamless, seam_jump, write_wav  # noqa: E402

SECONDS = 12.0
OVERLAP = 1.5
SEED = 4711
OUT = os.path.join(OUT_DIR, "Wilderness", "amb_waterfall.wav")


def brown(n, rng):
    y = [0.0] * n
    s = 0.0
    for i in range(n):
        s = (s + rng.uniform(-1.0, 1.0) * 0.02) * 0.998
        y[i] = s
    return y


def white(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def channel(rng, n, phase):
    rumble = lowpass(brown(n, rng), 180.0)
    body = bandpass(white(n, rng), 250.0, 2600.0)
    spray = highpass(white(n, rng), 3500.0)
    mr = max(abs(v) for v in rumble) or 1.0
    mb = max(abs(v) for v in body) or 1.0
    ms = max(abs(v) for v in spray) or 1.0
    out = [0.0] * n
    total = n / SR
    for i in range(n):
        t = i / SR
        # Langsame Schwankungen (Periode passt nicht exakt, Naht wird per Crossfade geschlossen).
        swell = 0.85 + 0.15 * math.sin(2 * math.pi * (t / total * 3.0) + phase)
        gust = 0.8 + 0.2 * math.sin(2 * math.pi * (t / total * 7.0) + phase * 1.7)
        out[i] = 0.55 * rumble[i] / mr + 0.75 * swell * body[i] / mb + 0.35 * gust * spray[i] / ms
    return out


def main():
    rng = random.Random(SEED)
    n = int(SECONDS * SR)
    f = int(OVERLAP * SR)
    chans = []
    for phase in (0.0, 1.3):
        raw = channel(rng, n + f, phase)
        chans.append(make_seamless(raw, n))
    size = write_wav(OUT, chans, peak=0.8)
    print("amb_waterfall.wav", size, "Bytes, Naht", round(seam_jump(chans[0]), 2))


if __name__ == "__main__":
    main()
