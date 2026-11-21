"""Gemeinsame Synthese-Bausteine fuer MTA 6 (nur Python-Standardbibliothek).

Alles hier ist selbst entwickelte, rein prozedurale Klangerzeugung (Wavetable-
Oszillatoren, FM, Rauschen, einfache Filter, Kamm-Hall). Es werden keine fremden
Samples, Melodien oder Aufnahmen verwendet. Zufall laeuft ausschliesslich ueber
random.Random(seed), damit die Ausgabe reproduzierbar ist.

Nahtloses Looping:
  * Musik: Noten, die ueber das Loop-Ende hinausklingen, werden zyklisch (modulo)
    in den Anfang eingemischt; Delay/Hall sind zyklisch gerechnet.
  * Ambience: Rauschen wird mit Ueberlappung erzeugt und per Equal-Power-Crossfade
    geschlossen (make_seamless).
"""
import json
import math
import os
import random
import sys
import wave
from array import array

SR = 44100
TBL = 2048
TBLM = TBL - 1

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "Content", "Audio", "Generated"))


# ----------------------------------------------------------------------------
# Wavetables
# ----------------------------------------------------------------------------
def _table(fn):
    return [fn(i / TBL) for i in range(TBL)]


def _saw(p, h):
    return (2.0 / math.pi) * sum(((-1) ** (k + 1)) * math.sin(2 * math.pi * k * p) / k for k in range(1, h + 1))


def _square(p, h):
    return (4.0 / math.pi) * sum(math.sin(2 * math.pi * k * p) / k for k in range(1, h + 1, 2))


def _tri(p):
    return (8.0 / math.pi ** 2) * sum(((-1) ** ((k - 1) // 2)) * math.sin(2 * math.pi * k * p) / (k * k)
                                       for k in range(1, 40, 2))


TABLES = {
    "sine": _table(lambda p: math.sin(2 * math.pi * p)),
    "saw": _table(lambda p: 0.85 * _saw(p, 24)),
    "saw_lo": _table(lambda p: 0.85 * _saw(p, 7)),
    "square": _table(lambda p: 0.8 * _square(p, 23)),
    "square_lo": _table(lambda p: 0.8 * _square(p, 7)),
    "tri": _table(_tri),
}

NOTE_PC = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6,
           "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}

CHORDS = {
    "M": [0, 4, 7], "m": [0, 3, 7], "sus": [0, 5, 7], "5": [0, 7, 12],
    "7": [0, 4, 7, 10], "m7": [0, 3, 7, 10], "M7": [0, 4, 7, 11],
    "m7b5": [0, 3, 6, 10], "m9": [0, 3, 7, 10, 14], "M9": [0, 4, 7, 11, 14],
}

PENT_MINOR = [0, 3, 5, 7, 10]
PENT_MAJOR = [0, 2, 4, 7, 9]


def hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def parse_key(key):
    """'Am' -> (9, True), 'C' -> (0, False). Gibt (Pitchclass, ist_moll) zurueck."""
    minor = key.endswith("m")
    return NOTE_PC[key[:-1] if minor else key], minor


def bars_for(bpm, min_seconds=20.0):
    """Kleinste Taktzahl (8/12/16), die mindestens min_seconds ergibt."""
    for b in (8, 12, 16):
        if b * 240.0 / bpm >= min_seconds:
            return b
    return 16


# ----------------------------------------------------------------------------
# Stereo-Puffer (zyklisch)
# ----------------------------------------------------------------------------
class Mix:
    def __init__(self, n):
        self.n = n
        self.L = [0.0] * n
        self.R = [0.0] * n

    def add(self, start, samples, pan=0.0, gain=1.0):
        """Mischt samples ab 'start' ein; laeuft ueber das Ende hinaus zyklisch weiter."""
        g_l = gain * math.cos((pan + 1.0) * math.pi / 4.0)
        g_r = gain * math.sin((pan + 1.0) * math.pi / 4.0)
        n, L, R = self.n, self.L, self.R
        i = int(start) % n
        for s in samples:
            L[i] += s * g_l
            R[i] += s * g_r
            i += 1
            if i == n:
                i = 0


# ----------------------------------------------------------------------------
# Stimmen
# ----------------------------------------------------------------------------
def voice(freq, dur, wave_="saw", amp=1.0, a=0.005, d=0.1, s=0.7, r=0.15, cut=0.0, cut_env=0.0,
          detune=0.0, vib=0.0, vib_hz=5.2, drive=0.0):
    """Subtraktive Note: Wavetable (+ optional Detune-Paar), ADSR, Tiefpass mit Huellkurve."""
    n_on = max(1, int(dur * SR))
    n_rel = max(1, int(r * SR))
    n = n_on + n_rel
    na = max(1, int(a * SR))
    nd = max(1, int(d * SR))
    name = wave_ + "_lo" if (freq > 600 and (wave_ + "_lo") in TABLES) else wave_
    tbl = TABLES[name]
    inc = freq * TBL / SR
    inc2 = inc * 2.0 ** (detune / 1200.0) if detune else 0.0
    ph = 0.0
    ph2 = 0.37 * TBL
    vinc = 2 * math.pi * vib_hz / SR
    two_pi_sr = 2 * math.pi / SR
    out = [0.0] * n
    lp = 0.0
    k = 1.0
    e_on = None
    for i in range(n):
        if i < n_on:
            if i < na:
                e = i / na
            elif i < na + nd:
                e = 1.0 - (1.0 - s) * (i - na) / nd
            else:
                e = s
            e_on = e
        else:
            e = e_on * (1.0 - (i - n_on) / n_rel)
        if cut and (i & 31) == 0:
            f = cut * (1.0 + cut_env * e)
            k = 1.0 - math.exp(-two_pi_sr * f) if f < SR * 0.4 else 1.0
        m = 1.0 + vib * math.sin(vinc * i) if vib else 1.0
        x = tbl[int(ph) & TBLM]
        ph += inc * m
        if inc2:
            x = 0.5 * (x + tbl[int(ph2) & TBLM])
            ph2 += inc2 * m
        if cut:
            lp += k * (x - lp)
            x = lp
        if drive:
            x = math.tanh(x * drive)
        out[i] = x * e * amp
    return out


def fm(freq, dur, ratio=1.0, index=2.0, idx_decay=4.0, amp=1.0, decay=2.0, r=0.08, a=0.003):
    """Einfache 2-Operator-FM (E-Piano, Marimba, Glocke)."""
    n_on = int(dur * SR)
    n_rel = max(1, int(r * SR))
    n = n_on + n_rel
    w = 2 * math.pi * freq / SR
    wm = w * ratio
    na = max(1, int(a * SR))
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        e = math.exp(-decay * t)
        if i < na:
            e *= i / na
        if i >= n_on:
            e *= 1.0 - (i - n_on) / n_rel
        idx = index * math.exp(-idx_decay * t)
        out[i] = math.sin(w * i + idx * math.sin(wm * i)) * e * amp
    return out


def noise_list(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def lowpass(x, fc):
    k = 1.0 - math.exp(-2 * math.pi * fc / SR)
    y = [0.0] * len(x)
    s = 0.0
    for i, v in enumerate(x):
        s += k * (v - s)
        y[i] = s
    return y


def highpass(x, fc):
    lp = lowpass(x, fc)
    return [a - b for a, b in zip(x, lp)]


def bandpass(x, lo, hi):
    return lowpass(highpass(x, lo), hi)


def scale(x, g):
    return [v * g for v in x]


def noise_burst(dur, rng, hp=0.0, lp=0.0, decay=30.0, amp=1.0):
    n = int(dur * SR)
    x = noise_list(n, rng)
    if hp:
        x = highpass(x, hp)
    if lp:
        x = lowpass(x, lp)
    return [v * math.exp(-decay * i / SR) * amp for i, v in enumerate(x)]


# ----------------------------------------------------------------------------
# Schlagzeug (alles aus Sinus/Rauschen)
# ----------------------------------------------------------------------------
def kick(f0=130.0, f1=46.0, dur=0.38, amp=1.0, click=0.25):
    n = int(dur * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = f1 + (f0 - f1) * math.exp(-t * 28.0)
        ph += 2 * math.pi * f / SR
        e = math.exp(-t * 9.0)
        out[i] = math.sin(ph) * e * amp
        if i < 60:
            out[i] += click * (1 - i / 60.0) * (1 if (i // 3) % 2 else -1)
    return out


def sub808(freq, dur=0.7, amp=1.0, glide=0.0, drive=1.6):
    n = int(dur * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = freq * (1.0 + glide * math.exp(-t * 25.0))
        ph += 2 * math.pi * f / SR
        e = math.exp(-t * 3.2) * (min(1.0, i / 120.0))
        out[i] = math.tanh(math.sin(ph) * drive) * e * amp
    return out


def snare(rng, dur=0.22, amp=1.0, tone=190.0, noise_amt=0.8, hp=1200.0):
    n = int(dur * SR)
    nz = highpass(noise_list(n, rng), hp)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        ph += 2 * math.pi * (tone + 70 * math.exp(-t * 40)) / SR
        out[i] = (nz[i] * noise_amt * math.exp(-t * 20.0) + math.sin(ph) * 0.55 * math.exp(-t * 26.0)) * amp
    return out


def hat(rng, dur=0.05, amp=0.5, decay=70.0):
    return noise_burst(dur, rng, hp=6500.0, decay=decay, amp=amp)


def clap(rng, amp=0.8):
    out = [0.0] * int(0.22 * SR)
    for g in (0.0, 0.011, 0.022):
        b = noise_burst(0.02, rng, hp=900.0, lp=6000.0, decay=90.0, amp=amp * 0.7)
        o = int(g * SR)
        for i, v in enumerate(b):
            out[o + i] += v
    tail = noise_burst(0.16, rng, hp=900.0, lp=5500.0, decay=22.0, amp=amp)
    o = int(0.03 * SR)
    for i, v in enumerate(tail):
        out[o + i] += v
    return out


def tom(freq, dur=0.3, amp=0.8):
    n = int(dur * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        ph += 2 * math.pi * freq * (1 + 0.4 * math.exp(-t * 30)) / SR
        out[i] = math.sin(ph) * math.exp(-t * 11.0) * amp
    return out


def rim(rng, amp=0.5):
    n = int(0.04 * SR)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        out[i] = (math.sin(2 * math.pi * 1750 * t) * 0.6 + rng.uniform(-1, 1) * 0.4) * math.exp(-t * 130) * amp
    return out


# ----------------------------------------------------------------------------
# Effekte (zyklisch, damit der Loop nahtlos bleibt)
# ----------------------------------------------------------------------------
def _comb(x, d, g):
    n = len(x)
    y = x[:]
    for i in range(n):
        y[i] = x[i] + g * y[i - d]
    for i in range(min(n, 24 * d)):  # zweiter Durchlauf: Hallfahne vom Ende in den Anfang tragen
        y[i] = x[i] + g * y[i - d]
    return y


def reverb(mix, wet=0.2, size=1.0, g=0.78):
    n = mix.n
    mono = [(a + b) * 0.5 for a, b in zip(mix.L, mix.R)]
    mono = lowpass(mono, 5500.0)
    dl = [int(v * size) for v in (1687, 2053, 2371)]
    dr = [int(v * size) for v in (1759, 2131, 2467)]
    wl = [0.0] * n
    wr = [0.0] * n
    for d in dl:
        c = _comb(mono, d, g)
        wl = [a + b for a, b in zip(wl, c)]
    for d in dr:
        c = _comb(mono, d, g)
        wr = [a + b for a, b in zip(wr, c)]
    k = wet / 3.0 * (1.0 - g) * 1.6
    mix.L = [a + b * k for a, b in zip(mix.L, wl)]
    mix.R = [a + b * k for a, b in zip(mix.R, wr)]


def echo(mix, delay_samples, fb=0.4, wet=0.25):
    mono = [(a + b) * 0.5 for a, b in zip(mix.L, mix.R)]
    e = _comb(mono, delay_samples, fb)
    h = delay_samples // 2
    mix.L = [a + wet * (b - m) for a, b, m in zip(mix.L, e, mono)]
    mix.R = [a + wet * (e[i - h] - mono[i - h]) for i, a in enumerate(mix.R)]


def bitcrush(x, steps=64, hold=3):
    out = x[:]
    last = 0.0
    for i in range(len(out)):
        if i % hold == 0:
            last = round(out[i] * steps) / steps
        out[i] = last
    return out


# ----------------------------------------------------------------------------
# Ausgabe
# ----------------------------------------------------------------------------
def write_wav(path, channels, peak=0.89):
    """channels: Liste von 1 oder 2 float-Listen. Normalisiert auf 'peak', schreibt 16-Bit/44,1 kHz."""
    mx = max(max(abs(v) for v in ch) for ch in channels) or 1.0
    g = peak / mx
    n = len(channels[0])
    if len(channels) == 1:
        data = array("h", (int(max(-1.0, min(1.0, v * g)) * 32767) for v in channels[0]))
    else:
        data = array("h", [0]) * (2 * n)
        data[0::2] = array("h", (int(max(-1.0, min(1.0, v * g)) * 32767) for v in channels[0]))
        data[1::2] = array("h", (int(max(-1.0, min(1.0, v * g)) * 32767) for v in channels[1]))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(len(channels))
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes() if sys.byteorder == "little" else _swap(data))
    return os.path.getsize(path)


def _swap(a):
    a = array("h", a)
    a.byteswap()
    return a.tobytes()


def master_music(mix, drive=1.15):
    """Weiche Saettigung, damit Summen nicht hart clippen."""
    td = math.tanh(drive)
    mix.L = [math.tanh(v * drive) / td for v in mix.L]
    mix.R = [math.tanh(v * drive) / td for v in mix.R]
    return [mix.L, mix.R]


def make_seamless(x, n):
    """x hat Laenge n+f (f = Ueberlappung). Gibt n Samples zurueck, deren Ende nahtlos in den Anfang fuehrt."""
    f = len(x) - n
    y = x[:n]
    for i in range(f):
        t = i / f
        w_in = math.sin(t * math.pi / 2)
        w_out = math.cos(t * math.pi / 2)
        y[i] = x[i] * w_in + x[n + i] * w_out
    return y


def seam_jump(ch):
    """Sprung zwischen letztem und erstem Sample relativ zum mittleren Sample-zu-Sample-Abstand."""
    n = len(ch)
    avg = sum(abs(ch[i + 1] - ch[i]) for i in range(0, n - 1, 7)) / max(1, (n - 1) // 7)
    return abs(ch[0] - ch[-1]) / (avg or 1e-9)


def write_manifest(entries, path=None):
    path = path or os.path.join(OUT_DIR, "manifest.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"sampleRate": SR, "files": entries}, f, indent=2, ensure_ascii=False)
