"""Prueft die erzeugten WAV-Dateien: Format, Laenge (20-40 s fuer Loops), Gesamtgroesse (< 150 MB)."""
import glob
import os
import wave

from synth_common import OUT_DIR

total = 0
bad = 0
for p in sorted(glob.glob(os.path.join(OUT_DIR, "*.wav"))):
    with wave.open(p) as w:
        sec = w.getnframes() / w.getframerate()
        ok = w.getframerate() == 44100 and w.getsampwidth() == 2 and (sec >= 12 if "thunder" in p else 20 <= sec <= 40)
    size = os.path.getsize(p)
    total += size
    bad += 0 if ok else 1
    print("%-34s %5.1f s %d ch %6.2f MB %s" % (os.path.basename(p), sec, w.getnchannels(), size / 1048576.0, "ok" if ok else "FEHLER"))
print("gesamt %.1f MB, Fehler: %d" % (total / 1048576.0, bad))
raise SystemExit(1 if bad or total > 150 * 1048576 else 0)
