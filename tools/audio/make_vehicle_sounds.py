"""Erzeugt die Fahrzeugklaenge fuer MTA 6 (mono, 44,1 kHz, 16 Bit) nach Content/Audio/Generated/Vehicle/.

Aufruf (vom Projektordner aus):
    python tools/audio/make_vehicle_sounds.py
Ohne System-Python die mitgelieferte Unreal-Python verwenden:
    & "F:\\UnrealEngine\\UE_5.8\\Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe" tools/audio/make_vehicle_sounds.py

Nur Python-Standardbibliothek, feste Seeds (reproduzierbar). Rein prozedurale Synthese, keine Samples.

Dateien (Loops nahtlos):
  veh_engine_0800/2000/3500/5500  Motor-Loops je ca. 2 s; Laenge = exakt ganze Zahl von Arbeitsspielen
                                  (2 Kurbelwellenumdrehungen), alle Teiltoene/Modulationen periodisch im Loop.
  veh_tire_squeal                 Reifenquietschen (gefiltertes Rauschen + Resonanzen), 2 s Loop
  veh_wind                        Fahrtwind (rosa Rauschen, Boeen), 4 s Loop
  veh_horn                        Hupe, zwei Toene, 1 s Loop
  veh_siren                       Polizeisirene (Wail), 4 s Loop
  veh_collision                   Aufprall (Einzelklang)
  veh_door                        Tuerschlag (Einzelklang)

Nahtlosigkeit: Rauschfilter laufen zyklisch (zwei Durchlaeufe, Zustand vom Ende wird in den Anfang
uebernommen), Einzelereignisse (Zuendimpulse) werden modulo Looplaenge eingemischt, alle Sinus-/LFO-
Frequenzen haben eine ganze Zahl von Perioden pro Loop.
"""
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from synth_common import SR, OUT_DIR, write_wav, seam_jump  # noqa: E402

VEH_DIR = os.path.join(OUT_DIR, "Vehicle")
TWO_PI = 2.0 * math.pi

ENGINE_RPMS = (800, 2000, 3500, 5500)
CYLINDERS = 4


# ----------------------------------------------------------------------------
# Zyklische Filter (Zustand wird ueber das Loop-Ende in den Anfang getragen)
# ----------------------------------------------------------------------------
def cyc_lowpass(x, fc, passes=2):
    k = 1.0 - math.exp(-TWO_PI * fc / SR)
    s = 0.0
    y = [0.0] * len(x)
    for _ in range(passes):
        for i, v in enumerate(x):
            s += k * (v - s)
            y[i] = s
    return y


def cyc_highpass(x, fc):
    lp = cyc_lowpass(x, fc)
    return [a - b for a, b in zip(x, lp)]


def cyc_biquad_bp(x, f0, q, passes=3):
    """RBJ-Bandpass (0 dB Spitze), zyklisch gerechnet."""
    w0 = TWO_PI * f0 / SR
    alpha = math.sin(w0) / (2.0 * q)
    a0 = 1.0 + alpha
    b0, b2 = alpha / a0, -alpha / a0
    a1, a2 = -2.0 * math.cos(w0) / a0, (1.0 - alpha) / a0
    x1 = x2 = y1 = y2 = 0.0
    y = [0.0] * len(x)
    for _ in range(passes):
        for i, v in enumerate(x):
            o = b0 * v + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1 = x1, v
            y2, y1 = y1, o
            y[i] = o
    return y


def white(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def pink(n, rng, passes=2):
    """Pink-Noise (Paul-Kellet) aus einer festen Weissrausch-Folge, zyklisch eingeschwungen."""
    w = white(n, rng)
    b0 = b1 = b2 = 0.0
    out = [0.0] * n
    for _ in range(passes):
        for i, v in enumerate(w):
            b0 = 0.99765 * b0 + v * 0.0990460
            b1 = 0.96300 * b1 + v * 0.2965164
            b2 = 0.57000 * b2 + v * 1.0526913
            out[i] = (b0 + b1 + b2 + v * 0.1848) * 0.25
    return out


def add_cyclic(buf, start, smp):
    n = len(buf)
    i = int(start) % n
    for v in smp:
        buf[i] += v
        i += 1
        if i == n:
            i = 0


def fade_edges(x, fade_in=0.002, fade_out=0.05):
    n = len(x)
    a = max(1, int(fade_in * SR))
    b = max(1, int(fade_out * SR))
    for i in range(min(a, n)):
        x[i] *= i / a
    for i in range(min(b, n)):
        x[n - 1 - i] *= i / b
    return x


def soft_clip(x, drive):
    td = math.tanh(drive)
    return [math.tanh(v * drive) / td for v in x]


# ----------------------------------------------------------------------------
# Motor
# ----------------------------------------------------------------------------
def engine_loop(rpm, seed, target_sec=2.0):
    """Viertakt-Vierzylinder: Zuendfrequenz = rpm/60 * Zylinder/2.

    Loop-Laenge = K ganze Arbeitsspiele (2 Umdrehungen). Die effektive Drehzahl weicht dadurch
    minimal (< 0,1 %) ab und wird im Manifest vermerkt.
    """
    rng = random.Random(seed)
    f_cycle_nom = rpm / 120.0  # Arbeitsspiele pro Sekunde
    k_cycles = max(1, round(target_sec * f_cycle_nom))
    n = int(round(k_cycles * SR / f_cycle_nom))
    f_cycle = k_cycles * SR / n
    eff_rpm = f_cycle * 120.0
    f_fire = f_cycle * CYLINDERS          # Zuendfrequenz
    f_rot = f_cycle * 2.0                 # Kurbelwelle
    n_fire = k_cycles * CYLINDERS
    t_fire = n / n_fire                   # Samples pro Zuendung (nicht ganzzahlig)
    r = (rpm - 800.0) / (5500.0 - 800.0)  # 0..1 ueber das Drehzahlband

    out = [0.0] * n

    # 1) Zuendimpulse: gedaempfte Resonanz + Rauschanteil, je Zylinder leicht unterschiedlich
    cyl_gain = [1.0, 0.86, 0.95, 0.80]
    cyl_res = [1.0, 1.04, 0.97, 1.02]
    f_res = 85.0 + 0.045 * rpm            # Auspuff-/Brennraumresonanz steigt mit Drehzahl
    tau = 0.42 * t_fire / SR              # Abklingzeit in s
    plen = int(min(2.5 * t_fire, 0.045 * SR))
    att = max(8, int(0.0012 * SR))
    for j in range(n_fire):
        c = j % CYLINDERS
        start = j * t_fire
        i0 = int(math.floor(start))
        frac = start - i0
        g = cyl_gain[c] * (1.0 + 0.07 * rng.uniform(-1.0, 1.0))  # Verbrennungsschwankung
        fr = f_res * cyl_res[c]
        noise_amt = 0.35 + 0.25 * r
        pulse = [0.0] * plen
        for i in range(plen):
            t = (i - frac) / SR
            if t < 0.0:
                continue
            e = math.exp(-t / tau)
            if i < att:
                e *= i / att
            pulse[i] = g * e * (math.sin(TWO_PI * fr * t) + 0.45 * math.sin(TWO_PI * 2.03 * fr * t)
                                + noise_amt * rng.uniform(-1.0, 1.0))
        add_cyclic(out, i0, pulse)

    # Rauschanteil im Puls tiefpassen (Auspuff klingt dumpf), Grenzfrequenz steigt mit Drehzahl
    out = cyc_lowpass(out, 900.0 + 1400.0 * r)

    # 2) Tonale Ordnungen (alle ganzzahlig im Loop)
    w_fire = TWO_PI * f_fire / SR
    w_rot = TWO_PI * f_rot / SR
    w_cyc = TWO_PI * f_cycle / SR
    a_fire = 0.55 - 0.15 * r
    a_fire2 = 0.18 + 0.12 * r
    a_rot = 0.16
    a_cyc = 0.07
    for i in range(n):
        out[i] += (a_fire * math.sin(w_fire * i) + a_fire2 * math.sin(2.0 * w_fire * i + 0.6)
                   + a_rot * math.sin(w_rot * i + 1.3) + a_cyc * math.sin(w_cyc * i + 0.2))

    # 3) Mechanik (Ventiltrieb, Ansaugen): Bandrauschen, mit der Zuendfrequenz moduliert
    mech = cyc_biquad_bp(white(n, rng), 2400.0 + 1800.0 * r, 1.2)
    intake = cyc_biquad_bp(white(n, rng), 600.0 + 700.0 * r, 2.0)
    m_amt = 0.05 + 0.06 * r
    i_amt = 0.08 + 0.10 * r
    for i in range(n):
        env = 0.55 + 0.45 * math.sin(w_fire * i)
        out[i] += mech[i] * m_amt * env + intake[i] * i_amt

    # 4) Leichte, langsame Modulation (ganze Perioden pro Loop)
    lfo_a = max(1, round(1.7 * n / SR))
    lfo_b = max(1, round(3.1 * n / SR))
    for i in range(n):
        p = i / n
        out[i] *= 1.0 + 0.035 * math.sin(TWO_PI * lfo_a * p) + 0.02 * math.sin(TWO_PI * lfo_b * p + 1.0)

    # 5) Gleichanteil entfernen, leicht saettigen
    out = cyc_highpass(out, 18.0)
    out = soft_clip(out, 1.3 + 0.4 * r)
    return out, eff_rpm, k_cycles


# ----------------------------------------------------------------------------
# Weitere Loops
# ----------------------------------------------------------------------------
def tire_squeal(seed, sec=2.0):
    rng = random.Random(seed)
    n = int(sec * SR)
    base = white(n, rng)
    band = cyc_biquad_bp(base, 1500.0, 1.6)
    res1 = cyc_biquad_bp(base, 980.0, 28.0)
    res2 = cyc_biquad_bp(base, 2150.0, 22.0)
    out = [0.0] * n
    # Tonale Resonanz mit leichtem Flattern (ganze Perioden: Frequenzen Vielfache von 1/sec Hz)
    f_tone = 1010.0
    vib_cycles = round(7.0 * sec)
    jit_cycles = round(13.0 * sec)
    ph = 0.0
    incs = []
    for i in range(n):
        p = i / n
        f = f_tone * (1.0 + 0.012 * math.sin(TWO_PI * vib_cycles * p) + 0.006 * math.sin(TWO_PI * jit_cycles * p + 0.7))
        incs.append(f / SR)
    total = sum(incs)
    corr = round(total) / total  # Gesamtphase auf ganze Perioden bringen -> nahtlos
    for i in range(n):
        ph += incs[i] * corr
        p = i / n
        amp = 0.75 + 0.25 * math.sin(TWO_PI * round(3.0 * sec) * p)
        out[i] = (0.32 * math.sin(TWO_PI * ph) + 0.12 * math.sin(TWO_PI * 2.0 * ph + 0.4)) * amp \
            + band[i] * 0.45 + res1[i] * 2.2 + res2[i] * 1.6
    out = cyc_highpass(out, 300.0)
    return soft_clip(out, 1.4)


def wind(seed, sec=4.0):
    rng = random.Random(seed)
    n = int(sec * SR)
    p = pink(n, rng)
    low = cyc_lowpass(p, 500.0)
    hiss = cyc_biquad_bp(p, 1800.0, 0.8)
    whistle = cyc_biquad_bp(white(n, rng), 3200.0, 14.0)
    out = [0.0] * n
    for i in range(n):
        q = i / n
        gust = 0.75 + 0.18 * math.sin(TWO_PI * 1 * q + 0.3) + 0.07 * math.sin(TWO_PI * 3 * q + 1.9)
        out[i] = (low[i] * 1.0 + hiss[i] * 0.55 * gust + whistle[i] * 0.25 * gust * gust) * gust
    return cyc_highpass(out, 40.0)


def horn(sec=1.0):
    """Zwei-Ton-Hupe (ca. 420/525 Hz, grosse Terz), Rechteck-aehnlich, Gehaeuseresonanz."""
    n = int(sec * SR)
    out = [0.0] * n
    for f, a in ((420.0, 1.0), (525.0, 0.85)):
        w = TWO_PI * f / SR
        for i in range(n):
            s = 0.0
            for h in range(1, 14):
                # ungerade stark, gerade schwach -> blechern
                g = (1.0 / h) if h % 2 else (0.35 / h)
                s += g * math.sin(w * h * i)
            out[i] += a * s
    out = soft_clip(out, 1.8)
    res = cyc_biquad_bp(out, 1900.0, 3.0)
    out = [a * 0.8 + b * 0.9 for a, b in zip(out, res)]
    return cyc_lowpass(out, 5200.0)


def siren(sec=4.0):
    """Wail: Frequenz steigt und faellt (650 -> 1450 Hz), Phase auf ganze Perioden korrigiert."""
    n = int(sec * SR)
    lo, hi = 650.0, 1450.0
    incs = [0.0] * n
    for i in range(n):
        q = i / n
        # sanfter Anstieg (60 %), schnellerer Abfall (40 %)
        if q < 0.6:
            s = 0.5 - 0.5 * math.cos(math.pi * q / 0.6)
        else:
            s = 0.5 + 0.5 * math.cos(math.pi * (q - 0.6) / 0.4)
        incs[i] = (lo + (hi - lo) * s) / SR
    total = sum(incs)
    corr = round(total) / total
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        ph += incs[i] * corr
        x = TWO_PI * ph
        out[i] = math.sin(x) + 0.30 * math.sin(2 * x) + 0.18 * math.sin(3 * x) + 0.08 * math.sin(5 * x)
    out = soft_clip(out, 1.6)
    return cyc_lowpass(out, 6000.0)


# ----------------------------------------------------------------------------
# Einzelklaenge
# ----------------------------------------------------------------------------
def collision(seed, sec=1.4):
    rng = random.Random(seed)
    n = int(sec * SR)
    out = [0.0] * n
    # tiefer Schlag (Frequenz faellt)
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = 45.0 + 85.0 * math.exp(-t * 18.0)
        ph += TWO_PI * f / SR
        out[i] += 1.1 * math.sin(ph) * math.exp(-t * 7.0)
    # Impuls + breitbandiges Rauschen
    nz = white(n, rng)
    nz_lp = cyc_lowpass(nz, 3500.0, passes=1)
    for i in range(n):
        t = i / SR
        out[i] += nz_lp[i] * 1.4 * math.exp(-t * 22.0) + nz[i] * 0.35 * math.exp(-t * 45.0)
    for i in range(int(0.004 * SR)):
        out[i] += (1.0 - i / (0.004 * SR)) * (1.0 if (i // 6) % 2 else -1.0) * 0.9
    # Blech: unharmonische Teiltoene mit eigener Abklingzeit
    for f, a, d in ((317.0, 0.35, 6.0), (523.0, 0.30, 7.5), (811.0, 0.25, 9.0), (1307.0, 0.18, 11.0),
                    (1873.0, 0.12, 14.0), (2650.0, 0.09, 18.0), (3911.0, 0.05, 22.0)):
        w = TWO_PI * f * (1.0 + rng.uniform(-0.01, 0.01)) / SR
        p0 = rng.uniform(0.0, TWO_PI)
        for i in range(n):
            out[i] += a * math.sin(w * i + p0) * math.exp(-d * i / SR)
    # Knirschen/Splitter in den ersten 250 ms
    for _ in range(36):
        t0 = rng.uniform(0.005, 0.25)
        b_len = int(rng.uniform(0.002, 0.012) * SR)
        g = rng.uniform(0.15, 0.5) * math.exp(-t0 * 6.0)
        i0 = int(t0 * SR)
        for i in range(b_len):
            if i0 + i < n:
                out[i0 + i] += g * rng.uniform(-1.0, 1.0) * (1.0 - i / b_len)
    out = soft_clip(out, 1.5)
    return fade_edges(out, 0.0005, 0.15)


def door(seed, sec=0.6):
    rng = random.Random(seed)
    n = int(sec * SR)
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = 70.0 + 60.0 * math.exp(-t * 30.0)
        ph += TWO_PI * f / SR
        out[i] += math.sin(ph) * math.exp(-t * 14.0)
    nz = cyc_lowpass(white(n, rng), 2500.0, passes=1)
    for i in range(n):
        t = i / SR
        out[i] += nz[i] * 0.9 * math.exp(-t * 35.0)
    # Schlossklick ca. 25 ms spaeter
    c0 = int(0.025 * SR)
    for i in range(int(0.03 * SR)):
        t = i / SR
        if c0 + i < n:
            out[c0 + i] += 0.35 * (math.sin(TWO_PI * 3100.0 * t) + 0.6 * math.sin(TWO_PI * 4700.0 * t)) * math.exp(-t * 160.0)
    out = soft_clip(out, 1.3)
    return fade_edges(out, 0.0005, 0.08)


# ----------------------------------------------------------------------------
# Ausgabe
# ----------------------------------------------------------------------------
def write(name, samples, loop, peak, extra=None):
    path = os.path.join(VEH_DIR, name + ".wav")
    size = write_wav(path, [samples], peak=peak)
    entry = {"file": name + ".wav", "name": name, "kind": "vehicle", "channels": 1, "samples": len(samples),
             "seconds": round(len(samples) / SR, 4), "bytes": size, "loop": loop,
             "seamJump": round(seam_jump(samples), 2) if loop else None}
    if extra:
        entry.update(extra)
    print("%-20s %7.3f s %8d B loop=%s seam=%s" % (name, entry["seconds"], size, loop, entry["seamJump"]), flush=True)
    return entry


def main():
    t0 = time.time()
    os.makedirs(VEH_DIR, exist_ok=True)
    entries = []
    for idx, rpm in enumerate(ENGINE_RPMS):
        smp, eff, cycles = engine_loop(rpm, 7100 + idx)
        entries.append(write("veh_engine_%04d" % rpm, smp, True, 0.85,
                             {"rpm": rpm, "effectiveRpm": round(eff, 3), "cycles": cycles}))
    entries.append(write("veh_tire_squeal", tire_squeal(7201), True, 0.8))
    entries.append(write("veh_wind", wind(7301), True, 0.8))
    entries.append(write("veh_horn", horn(), True, 0.8))
    entries.append(write("veh_siren", siren(), True, 0.8))
    entries.append(write("veh_collision", collision(7401), False, 0.95))
    entries.append(write("veh_door", door(7501), False, 0.9))
    with open(os.path.join(VEH_DIR, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"sampleRate": SR, "files": entries}, f, indent=2, ensure_ascii=False)
    total = sum(e["bytes"] for e in entries)
    print("Dateien: %d, gesamt %.2f MB, %.1f s" % (len(entries), total / 1048576.0, time.time() - t0))


if __name__ == "__main__":
    main()
