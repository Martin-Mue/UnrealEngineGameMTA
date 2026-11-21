"""Erzeugt die Ambience-Layer (mono, 44,1 kHz, 28 s, nahtlos) fuer MTA 6.

Layer: city, rain, wind, harbor, night_city, siren_far, industry, surf, garden, drones
plus thunder (Einzelklang, kein Loop). Rein prozedural, feste Seeds.
"""
import math
import os
import random
import sys
import time

from synth_common import (SR, OUT_DIR, noise_list, lowpass, highpass, bandpass, make_seamless, write_wav, seam_jump)

DUR = 28.0
N = int(DUR * SR)
FADE = int(2.0 * SR)  # Ueberlappung fuer den Crossfade


def pink(n, rng):
    """Pink-Noise (Paul-Kellet-Naeherung)."""
    b0 = b1 = b2 = 0.0
    out = [0.0] * n
    for i in range(n):
        w = rng.uniform(-1, 1)
        b0 = 0.99765 * b0 + w * 0.0990460
        b1 = 0.96300 * b1 + w * 0.2965164
        b2 = 0.57000 * b2 + w * 1.0526913
        out[i] = (b0 + b1 + b2 + w * 0.1848) * 0.25
    return out


def lfo(i, cycles, phase=0.0, n=N):
    return math.sin(2 * math.pi * cycles * i / n + phase)


def add_at(buf, start, smp, gain=1.0):
    n = len(buf)
    i = start % n
    for v in smp:
        buf[i] += v * gain
        i += 1
        if i == n:
            i = 0


def tone_env(freq, dur, amp, decay, rng=None, harm=(1.0,), glide=0.0):
    n = int(dur * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = freq * (1 + glide * t)
        ph += 2 * math.pi * f / SR
        e = math.exp(-decay * t) * min(1.0, i / 80.0)
        s = 0.0
        for h, g in enumerate(harm, 1):
            s += g * math.sin(ph * h)
        out[i] = s * e * amp
    return out


def finish(x, name, loop=True, peak=0.8):
    y = make_seamless(x, N) if loop else x
    size = write_wav(os.path.join(OUT_DIR, name + ".wav"), [y], peak=peak)
    return {"file": name + ".wav", "name": name, "kind": "ambience", "channels": 1, "samples": len(y),
            "seconds": round(len(y) / SR, 2), "bytes": size, "loop": loop,
            "seamJump": round(seam_jump(y), 2) if loop else None}


def L():
    return N + FADE


def gen_city(rng):
    n = L()
    base = lowpass(pink(n, rng), 700)
    mid = bandpass(noise_list(n, rng), 600, 2200)
    out = [base[i] * (0.8 + 0.2 * lfo(i, 3, 0.3, N)) + mid[i] * 0.25 * (0.6 + 0.4 * lfo(i, 5, 1.1, N)) for i in range(n)]
    hum = [0.12 * math.sin(2 * math.pi * 55 * i / SR) + 0.05 * math.sin(2 * math.pi * 110.4 * i / SR) for i in range(n)]
    out = [a + b for a, b in zip(out, hum)]
    for _ in range(7):  # Vorbeifahrten: Rauschen mit Filter-Sweep und Anschwellen
        d = int(rng.uniform(2.5, 5.0) * SR)
        raw = noise_list(d, rng)
        lp = 0.0
        car = [0.0] * d
        for i in range(d):
            t = i / d
            k = 1 - math.exp(-2 * math.pi * (350 + 1500 * math.sin(math.pi * t)) / SR)
            lp += k * (raw[i] - lp)
            car[i] = lp * math.sin(math.pi * t) ** 2 * 0.9
        add_at(out, rng.randrange(n), car, rng.uniform(0.5, 1.0))
    return finish(out, "amb_city")


def gen_rain(rng):
    n = L()
    wh = noise_list(n, rng)
    body = bandpass(wh, 900, 7500)
    low = lowpass(pink(n, rng), 300)
    out = [body[i] * (0.7 + 0.3 * lfo(i, 6, 0.0, N) * lfo(i, 2, 1.0, N)) + low[i] * 0.2 for i in range(n)]
    for _ in range(int(DUR * 220)):  # Tropfen auf Metall/Asphalt
        f = rng.uniform(1800, 5200)
        add_at(out, rng.randrange(n), tone_env(f, 0.02, 0.18, 220.0), rng.uniform(0.2, 1.0))
    return finish(out, "amb_rain")


def gen_wind(rng):
    n = L()
    raw = noise_list(n, rng)
    out = [0.0] * n
    lp = 0.0
    bp = 0.0
    for i in range(n):
        c = 220 + 260 * (0.5 + 0.5 * lfo(i, 3, 0.0, N)) + 120 * lfo(i, 7, 1.7, N)
        k = 1 - math.exp(-2 * math.pi * c / SR)
        lp += k * (raw[i] - lp)
        out[i] = lp * (0.55 + 0.45 * lfo(i, 4, 0.5, N) ** 2 * 1.0)
    whistle = bandpass(noise_list(n, rng), 650, 950)
    out = [out[i] * 1.6 + whistle[i] * 1.8 * max(0.0, lfo(i, 2, 0.2, N)) ** 2 for i in range(n)]
    return finish(out, "amb_wind")


def gen_harbor(rng):
    n = L()
    wave_n = lowpass(pink(n, rng), 900)
    out = [wave_n[i] * (0.3 + 0.7 * (0.5 + 0.5 * lfo(i, 4, 0.0, N)) ** 2) for i in range(n)]
    for st, f in ((9.0, 98.0), (21.5, 73.0)):  # ferne Nebelhoerner
        d = int(3.6 * SR)
        horn = [0.0] * d
        for i in range(d):
            t = i / SR
            e = min(1.0, t / 0.25) * min(1.0, (3.6 - t) / 0.7)
            horn[i] = (math.sin(2 * math.pi * f * t) + 0.5 * math.sin(2 * math.pi * f * 2.003 * t) + 0.3 * math.sin(2 * math.pi * f * 3.01 * t)) * e * 0.22
        add_at(out, int(st * SR), lowpass(horn, 900), 1.0)
    for _ in range(9):  # Moeven
        t0 = rng.randrange(n)
        for k in range(rng.randint(2, 4)):
            f0 = rng.uniform(1500, 2300)
            d = int(0.28 * SR)
            c = [math.sin(2 * math.pi * (f0 + 500 * math.sin(math.pi * i / d)) * i / SR + 12 * math.sin(2 * math.pi * 40 * i / SR) * 0.1) *
                 math.sin(math.pi * i / d) ** 1.5 * 0.05 for i in range(d)]
            add_at(out, t0 + int(k * 0.35 * SR), c, 1.0)
    for _ in range(10):  # Metall-Klacks / Taue
        t0 = rng.randrange(n)
        f = rng.uniform(300, 900)
        add_at(out, t0, tone_env(f, 0.5, 0.16, 7.0, harm=(1.0, 0.0, 0.45, 0.0, 0.2)), 1.0)
    return finish(out, "amb_harbor")


def gen_night_city(rng):
    n = L()
    base = lowpass(pink(n, rng), 450)
    out = [v * 0.6 for v in base]
    for i in range(n):  # Neon-/Netzbrummen
        t = i / SR
        out[i] += 0.03 * math.sin(2 * math.pi * 100 * t) + 0.018 * math.sin(2 * math.pi * 300 * t + 1) * (0.7 + 0.3 * lfo(i, 9, 0, N))
    bpm_period = int(SR * 60 / 124.0)  # ferner Club-Bass
    kick = [math.sin(2 * math.pi * (50 + 60 * math.exp(-i / 700.0)) * i / SR) * math.exp(-i / 4500.0) * 0.16 for i in range(9000)]
    pos = 0
    while pos < n:
        add_at(out, pos, lowpass(kick, 160), 1.0)
        pos += bpm_period
    for _ in range(4):  # Grillen-Zirpen
        t0 = rng.randrange(n)
        for k in range(int(rng.uniform(20, 40))):
            d = int(0.02 * SR)
            add_at(out, t0 + int(k * 0.045 * SR), [math.sin(2 * math.pi * 4700 * i / SR) * math.sin(math.pi * i / d) * 0.02 for i in range(d)])
    return finish(out, "amb_night_city")


def siren_wail(dur, f_lo, f_hi, period, amp, kind="wail"):
    n = int(dur * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        if kind == "wail":
            f = f_lo + (f_hi - f_lo) * (0.5 - 0.5 * math.cos(2 * math.pi * t / period))
        else:  # Zwei-Ton
            f = f_hi if (t / period) % 1.0 < 0.5 else f_lo
        ph += 2 * math.pi * f / SR
        x = math.sin(ph) + 0.4 * math.sin(2 * ph) + 0.2 * math.sin(3 * ph)
        e = math.sin(math.pi * i / n) ** 2
        out[i] = x * e * amp
    return out


def gen_siren_far(rng):
    n = L()
    out = [v * 0.02 for v in lowpass(pink(n, rng), 300)]
    s1 = lowpass(siren_wail(9.0, 650, 1350, 3.2, 0.5), 1800)
    s2 = lowpass(siren_wail(8.0, 520, 760, 0.55, 0.5, kind="two"), 1600)
    # Distanz: zusaetzliches Echo/Hall
    for s, st in ((s1, 2.0), (s2, 16.0)):
        add_at(out, int(st * SR), s, 0.6)
        add_at(out, int(st * SR) + int(0.19 * SR), s, 0.22)
    return finish(out, "amb_siren_far", peak=0.7)


def gen_industry(rng):
    n = L()
    out = [0.0] * n
    base = lowpass(pink(n, rng), 260)
    for i in range(n):
        t = i / SR
        am = 0.6 + 0.4 * math.sin(2 * math.pi * 6 * t)
        out[i] = base[i] * 0.8 + am * (0.12 * math.sin(2 * math.pi * 50 * t) + 0.07 * math.sin(2 * math.pi * 100.5 * t) + 0.03 * math.sin(2 * math.pi * 151 * t))
    for _ in range(8):  # Pressluft
        d = int(rng.uniform(0.4, 0.9) * SR)
        h = highpass(noise_list(d, rng), 2500)
        add_at(out, rng.randrange(n), [h[i] * math.exp(-4.0 * i / d) * 0.28 for i in range(d)])
    for k in range(14):  # Hammerschlaege
        add_at(out, int((1.0 + k * 1.9) * SR), tone_env(rng.uniform(180, 420), 0.45, 0.22, 9.0, harm=(1.0, 0.3, 0.5, 0.0, 0.25)))
    for st in (19.0,):  # Zugpfeife
        d = int(2.2 * SR)
        add_at(out, int(st * SR), lowpass([(math.sin(2 * math.pi * 392 * i / SR) + math.sin(2 * math.pi * 494 * i / SR)) * 0.1 * min(1.0, i / 6000.0) *
                                           min(1.0, (d - i) / 20000.0) for i in range(d)], 1200))
    return finish(out, "amb_industry")


def gen_surf(rng):
    n = L()
    w = lowpass(pink(n, rng), 1800)
    out = [w[i] * (0.25 + 0.75 * (0.5 + 0.5 * lfo(i, 5, 0.4, N)) ** 2) for i in range(n)]
    crowd = bandpass(noise_list(n, rng), 300, 1000)
    out = [out[i] + crowd[i] * 0.22 * (0.5 + 0.5 * lfo(i, 11, 0.9, N)) for i in range(n)]
    pad = [0.0] * n  # ferne Promenaden-Synths, stark gedaempft
    for i in range(n):
        t = i / SR
        pad[i] = (math.sin(2 * math.pi * 220 * t) + math.sin(2 * math.pi * 277.2 * t) + math.sin(2 * math.pi * 329.6 * t)) * 0.03 * (0.6 + 0.4 * lfo(i, 2, 0, N))
    out = [a + b for a, b in zip(out, lowpass(pad, 1200))]
    return finish(out, "amb_surf")


def gen_garden(rng):
    n = L()
    out = [v * 0.12 for v in lowpass(pink(n, rng), 900)]
    for k in range(int(DUR * 4 * 3)):  # Sprinkler-Ticks
        pass
    t = 3.0
    while t < DUR + 2:  # Sprinkler: rhythmische Rauschimpulse
        for j in range(5):
            d = int(0.05 * SR)
            b = bandpass(noise_list(d, rng), 2500, 6500)
            add_at(out, int((t + j * 0.21) * SR), [b[i] * math.exp(-60.0 * i / SR) * 0.35 for i in range(d)])
        t += 1.6
    for _ in range(24):  # Voegel
        t0 = rng.randrange(n)
        f0 = rng.uniform(2400, 4200)
        for k in range(rng.randint(1, 4)):
            d = int(rng.uniform(0.06, 0.14) * SR)
            sweep = rng.uniform(-900, 900)
            add_at(out, t0 + int(k * 0.15 * SR), [math.sin(2 * math.pi * (f0 + sweep * i / d) * i / SR) * math.sin(math.pi * i / d) * 0.07 for i in range(d)])
    for i in range(n):  # ferner Rasenmaeher
        t = i / SR
        out[i] += (0.02 * math.sin(2 * math.pi * 92 * t) + 0.012 * math.sin(2 * math.pi * 184.7 * t)) * (0.6 + 0.4 * lfo(i, 3, 0, N))
    return finish(out, "amb_garden")


def gen_drones(rng):
    n = L()
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        a = math.sin(2 * math.pi * 182 * t) + math.sin(2 * math.pi * 185.4 * t) + 0.5 * math.sin(2 * math.pi * 364.5 * t)
        whine = math.sin(2 * math.pi * (2300 + 140 * lfo(i, 5, 0, N)) * t) * 0.07 * (0.5 + 0.5 * lfo(i, 3, 1.0, N))
        out[i] = a * 0.1 * (0.75 + 0.25 * lfo(i, 14, 0, N)) + whine
    hiss = highpass(noise_list(n, rng), 4000)
    out = [out[i] + hiss[i] * 0.02 for i in range(n)]
    return finish(out, "amb_drones")


def gen_thunder(rng):
    d = int(12.0 * SR)
    raw = lowpass(noise_list(d, rng), 1800)
    out = [0.0] * d
    lp = 0.0
    rolls = [(0.0, 1.0), (1.2, 0.6), (2.6, 0.5), (4.5, 0.35), (6.5, 0.25)]
    for i in range(d):
        t = i / SR
        k = 1 - math.exp(-2 * math.pi * max(60.0, 900 * math.exp(-t * 0.6)) / SR)
        lp += k * (raw[i] - lp)
        e = sum(g * math.exp(-1.3 * (t - s)) for s, g in rolls if t >= s) * min(1.0, t / 0.01 + 0.0)
        out[i] = lp * e
    return finish(out, "amb_thunder", loop=False, peak=0.9)


GENS = {"city": gen_city, "rain": gen_rain, "wind": gen_wind, "harbor": gen_harbor, "night_city": gen_night_city,
        "siren_far": gen_siren_far, "industry": gen_industry, "surf": gen_surf, "garden": gen_garden,
        "drones": gen_drones, "thunder": gen_thunder}


def render(layer):
    t0 = time.time()
    rng = random.Random(9000 + sorted(GENS).index(layer))
    info = GENS[layer](rng)
    info["layer"] = layer
    info["renderSeconds"] = round(time.time() - t0, 1)
    return info


def tasks():
    return list(GENS)


if __name__ == "__main__":
    for l in (sys.argv[1:] or tasks()):
        print(render(l), flush=True)
