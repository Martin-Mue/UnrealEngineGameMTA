"""Erzeugt die Radio-Musikschleifen (24 Loops, 6 Sender x 4 Varianten) fuer MTA 6.

Alle Stuecke sind frei komponiert/prozedural (feste Seeds), Stereo, 44,1 kHz, 16 Bit,
je 20-30 s und nahtlos loopbar. Keine Anlehnung an reale Titel oder Melodien.

Aufruf:  python gen_radio_music.py [station_id [variante]]
"""
import math
import os
import random
import sys
import time

from synth_common import (SR, OUT_DIR, Mix, voice, fm, kick, sub808, snare, hat, clap, tom, rim, noise_burst,
                          lowpass, bandpass, bitcrush, reverb, echo, master_music, write_wav, seam_jump, hz,
                          parse_key, bars_for, CHORDS, PENT_MINOR, PENT_MAJOR, noise_list)

# Station -> (Genre, [(Variante, BPM, Tonart, Seed)])
STATIONS = {
    "neon_tide":       ("synthwave", [("a", 100, "Am", 1101), ("b", 108, "Fm", 1102), ("c", 116, "Dm", 1103), ("d", 96, "Em", 1104)]),
    "dockline":        ("harbor_rock", [("a", 108, "Em", 2101), ("b", 116, "Am", 2102), ("c", 100, "Dm", 2103), ("d", 124, "Gm", 2104)]),
    "velvet_quay":     ("jazz", [("a", 84, "Dm", 3101), ("b", 76, "Gm", 3102), ("c", 92, "F", 3103), ("d", 68, "Cm", 3104)]),
    "lantern_pirate":  ("beat", [("a", 86, "Cm", 4101), ("b", 90, "Gm", 4102), ("c", 82, "Dm", 4103), ("d", 94, "Am", 4104)]),
    "aurelia_direct":  ("corporate", [("a", 104, "C", 5101), ("b", 100, "G", 5102), ("c", 108, "D", 5103), ("d", 96, "F", 5104)]),
    "open_line":       ("funk", [("a", 116, "Gm", 6101), ("b", 120, "C", 6102), ("c", 112, "Em", 6103), ("d", 124, "A", 6104)]),
}

MINOR_PROGS = [
    [(0, "m"), (8, "M"), (3, "M"), (10, "M")],
    [(0, "m"), (10, "M"), (8, "M"), (10, "M")],
    [(0, "m"), (5, "m"), (8, "M"), (7, "M")],
    [(0, "m"), (3, "M"), (5, "m"), (8, "M")],
]
MAJOR_PROGS = [
    [(0, "M"), (7, "M"), (9, "m"), (5, "M")],
    [(0, "M"), (9, "m"), (5, "M"), (7, "M")],
    [(0, "M"), (5, "M"), (9, "m"), (7, "M")],
    [(9, "m"), (5, "M"), (0, "M"), (7, "M")],
]
JAZZ_MINOR = [
    [(2, "m7b5"), (7, "7"), (0, "m7"), (8, "M7")],
    [(2, "m7b5"), (7, "7"), (0, "m7"), (0, "m7")],
    [(0, "m7"), (5, "m7"), (2, "m7b5"), (7, "7")],
    [(2, "m7b5"), (7, "7"), (0, "m9"), (5, "m7")],
]
JAZZ_MAJOR = [[(2, "m7"), (7, "7"), (0, "M7"), (9, "7")]] * 4

_memo = {}


def mv(kind, freq, dur, **kw):
    """Gecachte Stimme: gleiche Noten werden nur einmal gerechnet."""
    key = (kind, round(freq, 2), round(dur, 3), tuple(sorted(kw.items())))
    r = _memo.get(key)
    if r is None:
        r = (voice if kind == "v" else fm)(freq, dur, **kw)
        _memo[key] = r
    return r


class Ctx:
    def __init__(self, bpm, key, seed):
        self.rng = random.Random(seed)
        self.bpm = bpm
        self.beat = 4 * int(round(SR * 60.0 / bpm / 4.0))  # durch 4 teilbar -> ganzzahlige 16tel
        self.step = self.beat // 4
        self.bars = bars_for(bpm)
        self.n = self.bars * 4 * self.beat
        self.mix = Mix(self.n)
        self.root, self.minor = parse_key(key)

    def at(self, bar, step):
        return bar * 4 * self.beat + step * self.step

    def atb(self, bar, beat):
        return int((bar * 4 + beat) * self.beat)

    def s(self, steps):
        return steps * self.step / SR

    def jit(self, ms=4.0):
        return int(self.rng.uniform(-ms, ms) * SR / 1000.0)


def pc_midi(pc, lo):
    m = lo + ((pc - lo) % 12)
    return m


def chord_tones(c, off, q, lo):
    root = pc_midi((c.root + off) % 12, lo)
    return [root + iv for iv in CHORDS[q]]


def progression(c, vi):
    return (MINOR_PROGS if c.minor else MAJOR_PROGS)[vi % 4]


def lead_phrase(c, vi, length=8):
    """Erzeugt (seed-fest) eine Pentatonik-Phrase: Liste (Stufe, Dauer_in_16teln)."""
    r = random.Random(c.rng.random() + vi)
    scale = PENT_MINOR if c.minor else PENT_MAJOR
    notes, pos = [], 0
    deg = r.randrange(len(scale))
    while pos < 16 and len(notes) < length:
        dur = r.choice([2, 2, 3, 4])
        dur = min(dur, 16 - pos)
        notes.append((pos, scale[deg % len(scale)] + 12 * (deg // len(scale)), dur))
        pos += dur
        deg = max(0, min(len(scale) * 2 - 1, deg + r.choice([-2, -1, -1, 0, 1, 1, 2])))
        if r.random() < 0.18:
            pos += 1
    return notes


# ---------------------------------------------------------------------------
# Genres
# ---------------------------------------------------------------------------
def synthwave(c, vi):
    rng, mix = c.rng, c.mix
    prog = progression(c, vi)
    K, S, H, OH = kick(), snare(rng, amp=0.9), hat(rng, amp=0.35), hat(rng, dur=0.14, amp=0.3, decay=22)
    bass_pat = [0, 0, 12, 0, 0, 0, 12, 0, 0, 12, 0, 0, 7, 0, 12, 0] if vi % 2 == 0 else [0, 0, 12, 0, 0, 12, 0, 0, 7, 0, 12, 0, 0, 12, 7, 0]
    motif = lead_phrase(c, vi)
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        for st in (0, 4, 8, 12):
            mix.add(c.at(bar, st), K, 0, 0.9)
        for st in (4, 12):
            mix.add(c.at(bar, st), S, 0, 0.7)
        if bar % 4 == 3:
            for st in (13, 14, 15):
                mix.add(c.at(bar, st), S, 0, 0.35)
        for st in range(0, 16, 2):
            mix.add(c.at(bar, st), H, -0.2 if st % 4 else 0.2, 0.7)
        for st in (2, 6, 10, 14):
            mix.add(c.at(bar, st), OH, 0.3, 0.5)
        broot = pc_midi((c.root + off) % 12, 33)
        for st in range(16):
            semis = bass_pat[st]
            mix.add(c.at(bar, st), mv("v", hz(broot + semis), c.s(1) * 0.92, wave_="saw", amp=0.34, a=0.002, d=0.07, s=0.55,
                                     r=0.03, cut=650, cut_env=3.5), 0)
        tones = chord_tones(c, off, q, 52)
        blen = 4 * c.beat / SR
        for j, m in enumerate(tones):
            mix.add(c.at(bar, 0), mv("v", hz(m), blen + 0.25, wave_="saw", amp=0.075, a=0.45, d=0.5, s=0.8, r=0.5,
                                     cut=1500, detune=9), (-0.5, 0.0, 0.5)[j % 3])
        hi = chord_tones(c, off, q, 64)
        for k_, st in enumerate(range(0, 16, 2)):
            m = hi[(0, 1, 2, 1)[k_ % 4]] + (12 if k_ % 8 == 5 else 0)
            mix.add(c.at(bar, st), mv("v", hz(m), c.s(2) * 0.85, wave_="saw", amp=0.1, a=0.002, d=0.1, s=0.05, r=0.05,
                                     cut=3200, cut_env=1.5), -0.4 if k_ % 2 else 0.4)
        if (bar // 4) % 2 == 1:
            base = pc_midi((c.root + off) % 12, 69)
            for pos, semis, dur in motif:
                mix.add(c.at(bar, pos), mv("v", hz(base + semis), c.s(dur) * 0.95, wave_="saw", amp=0.17, a=0.012, d=0.2,
                                         s=0.75, r=0.25, cut=2600, cut_env=0.8, detune=7, vib=0.006), 0.15)
    echo(mix, 3 * c.step, fb=0.38, wet=0.32)
    reverb(mix, wet=0.24, size=1.1)
    return master_music(mix, 1.2)


def harbor_rock(c, vi):
    rng, mix = c.rng, c.mix
    prog = progression(c, vi)
    K, S, H = kick(f0=110, f1=52, dur=0.3), snare(rng, amp=1.0, hp=900), hat(rng, amp=0.4, decay=60)
    CR = hat(rng, dur=0.7, amp=0.35, decay=5)
    T1, T2 = tom(118), tom(86)
    motif = lead_phrase(c, vi + 1)
    chug = [0, 2, 4, 6, 8, 10, 12, 14] if vi % 2 == 0 else [0, 3, 6, 8, 11, 14]
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        if bar % 4 == 0:
            mix.add(c.at(bar, 0), CR, 0.3, 0.7)
        for st in (0, 8) + ((10,) if vi % 2 else (6,)):
            mix.add(c.at(bar, st) + c.jit(3), K, 0, 0.95)
        for st in (4, 12):
            mix.add(c.at(bar, st) + c.jit(3), S, 0, 0.75)
        for st in range(0, 16, 2):
            mix.add(c.at(bar, st), H, 0.25, 0.6 if st % 4 == 0 else 0.4)
        if bar % 4 == 3:
            for st, t in ((12, T1), (13, T1), (14, T2), (15, T2)):
                mix.add(c.at(bar, st), t, -0.3 if t is T1 else 0.3, 0.7)
        rmid = pc_midi((c.root + off) % 12, 40)
        # Powerchords (Verzerrung per tanh), Palm-Mute-Achtel + offene Akkorde
        for st in chug:
            mute = st not in (0, 8)
            dur = c.s(1.6) if mute else c.s(7.5)
            note = None
            parts = []
            for semis, pan in ((0, -0.5), (7, 0.5), (12, -0.2)):
                parts.append((mv("v", hz(rmid + semis), dur, wave_="saw", amp=0.22, a=0.004, d=0.06 if mute else 0.4,
                                 s=0.25 if mute else 0.7, r=0.05, cut=2400 if not mute else 1300, drive=4.0, detune=6), pan))
            for smp, pan in parts:
                mix.add(c.at(bar, st), smp, pan, 1.0)
        # Bass
        for st in range(0, 16, 2):
            semis = 12 if st == 14 else 0
            mix.add(c.at(bar, st), mv("v", hz(rmid - 12 + semis), c.s(1.8), wave_="saw", amp=0.3, a=0.003, d=0.1, s=0.6, r=0.04,
                                     cut=520, cut_env=1.5, drive=2.0), 0)
        # Concertina-Pad (Hafen-Flair)
        for j, m in enumerate(chord_tones(c, off, q, 57)):
            mix.add(c.at(bar, 0), mv("v", hz(m), 4 * c.beat / SR, wave_="square", amp=0.05, a=0.08, d=0.3, s=0.8, r=0.2,
                                     cut=2000, vib=0.004, vib_hz=5.5), (-0.3, 0.0, 0.3)[j % 3])
        if (bar // 4) % 2 == 1:
            base = pc_midi((c.root + off) % 12, 64)
            for pos, semis, dur in motif:
                mix.add(c.at(bar, pos), mv("v", hz(base + semis), c.s(dur) * 0.97, wave_="saw", amp=0.17, a=0.008, d=0.2,
                                         s=0.8, r=0.18, cut=3200, drive=3.0, vib=0.009), 0.2)
    reverb(mix, wet=0.14, size=0.9)
    return master_music(mix, 1.3)


def jazz(c, vi):
    rng, mix = c.rng, c.mix
    prog = (JAZZ_MINOR if c.minor else JAZZ_MAJOR)[vi % 4]
    SW = 0.667  # Swing-Position der Achtel-Nachschlaege (in Schlaegen)
    brush = noise_burst(0.07, rng, hp=3500, lp=9500, decay=45, amp=0.28)
    brush2 = noise_burst(0.12, rng, hp=1800, lp=7000, decay=22, amp=0.22)
    Kf = kick(f0=90, f1=50, dur=0.3, click=0.0)
    comp_rhythms = [[(0.0, 1.4), (2.667, 0.8)], [(1.0, 1.0), (2.667, 1.0)], [(0.667, 0.9), (2.0, 1.2)], [(0.0, 2.0), (3.0, 0.9)]]
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        noff, _ = prog[(bar + 1) % 4]
        tones = chord_tones(c, off, q, 53)
        # Ride/Besen: 1, 2, 2-und, 3, 4, 4-und
        for b, g in ((0, 1.0), (1, 0.8), (1 + SW, 0.5), (2, 1.0), (3, 0.8), (3 + SW, 0.5)):
            mix.add(c.atb(bar, b) + c.jit(6), brush, 0.3, g)
        for b in (1, 3):
            mix.add(c.atb(bar, b) + c.jit(6), brush2, -0.2, 0.7)
        mix.add(c.atb(bar, 0), Kf, 0, 0.35)
        # E-Piano-Comping
        for b, dur in rng.choice(comp_rhythms):
            for j, m in enumerate(tones[1:] if len(tones) > 3 else tones):
                mix.add(c.atb(bar, b) + c.jit(10), mv("f", hz(m), dur * c.beat / SR, ratio=1.0, index=1.1, idx_decay=5.0, amp=0.11,
                                                     decay=1.7), (-0.3, 0.0, 0.3, 0.1)[j % 4])
        # Walking Bass
        broot = pc_midi((c.root + off) % 12, 38)
        nroot = pc_midi((c.root + noff) % 12, 38)
        steps = [broot, broot + CHORDS[q][1] if q != "7" else broot + 4, broot + 7, nroot + rng.choice([-1, 1])]
        for b, m in enumerate(steps):
            mix.add(c.atb(bar, b) + c.jit(5), mv("v", hz(m), 0.9 * c.beat / SR, wave_="tri", amp=0.42, a=0.006, d=0.25, s=0.35,
                                                r=0.06, cut=520, cut_env=1.5), 0)
        # Saxofon-artige Linie (spielt nicht in jedem Takt)
        if bar % 4 != 0 and rng.random() < 0.8:
            pool = [t + 12 * o for t in tones for o in (0, 1)] + [tones[0] + 14 + 12]
            pool = sorted(set(p for p in pool if 64 <= p <= 82))
            pos = 0.0 if rng.random() < 0.5 else 0.667
            cur = rng.randrange(len(pool))
            for _ in range(rng.randint(3, 5)):
                dur = rng.choice([0.667, 0.667, 1.0, 1.333])
                if pos + dur > 4.0:
                    break
                mix.add(c.atb(bar, pos) + c.jit(8), mv("v", hz(pool[cur]), dur * c.beat / SR * 0.92, wave_="saw", amp=0.13, a=0.03,
                                                      d=0.2, s=0.75, r=0.12, cut=1700, cut_env=0.5, vib=0.007, vib_hz=5.0), 0.25)
                pos += dur + rng.choice([0.0, 0.0, 0.333])
                cur = max(0, min(len(pool) - 1, cur + rng.choice([-2, -1, 1, 2])))
    reverb(mix, wet=0.3, size=1.35)
    return master_music(mix, 1.0)


def beat_hiphop(c, vi):
    """Instrumentaler Boom-Bap-Beat (ohne Gesang, ohne Sample-Material)."""
    rng, mix = c.rng, c.mix
    prog = progression(c, vi)
    K, S, C = kick(f0=120, f1=48, dur=0.36), snare(rng, amp=0.95, tone=175, hp=1000), clap(rng, 0.5)
    H, HO = hat(rng, amp=0.3, decay=85), hat(rng, dur=0.1, amp=0.25, decay=30)
    kick_pats = ["x.....x...x.x...", "x.....x.x.....x.", "x...x.....x..x..", "x..x..x...x...x."]
    kp = kick_pats[vi % 4]
    stab_steps = [(0, 6), (6, 5), (10, 5)] if vi % 2 == 0 else [(0, 8), (10, 4)]
    motif = lead_phrase(c, vi + 3, 6)
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        for st, ch in enumerate(kp):
            if ch == "x":
                mix.add(c.at(bar, st) + c.jit(5), K, 0, 0.95)
        for st in (4, 12):
            mix.add(c.at(bar, st) + c.jit(7), S, 0, 0.8)
            mix.add(c.at(bar, st) + c.jit(7), C, 0.1, 0.35)
        for st in range(16):
            g = 0.75 if st % 4 == 0 else (0.45 if st % 2 == 0 else 0.25 + 0.2 * rng.random())
            mix.add(c.at(bar, st) + c.jit(4), H, 0.3 if st % 2 else -0.3, g)
        if bar % 4 == 3:
            for st in (14, 15):
                mix.add(c.at(bar, st), H, 0, 0.5)
        mix.add(c.at(bar, 14), HO, 0.2, 0.6)
        # 808-Sub
        r808 = pc_midi((c.root + off) % 12, 31)
        for st, dur in ((0, 5), (10, 3)) if vi % 2 == 0 else ((0, 8), (12, 3)):
            mix.add(c.at(bar, st), sub808(hz(r808 + (7 if st == 12 else 0)), dur=min(0.9, c.s(dur)), amp=0.85, glide=0.0), 0)
        # Keys-Staccato (gebitcrusht, "gesampelter" Look, aber selbst erzeugt)
        tones = chord_tones(c, off, q + "7" if q == "m" else q, 55)
        tones = [t for t in chord_tones(c, off, "m7" if q == "m" else "M7", 55)]
        for st, dur in stab_steps:
            stab = [0.0] * int(c.s(dur) * SR + 4000)
            for m in tones:
                f = mv("f", hz(m), c.s(dur) * 0.9, ratio=1.0, index=0.8, idx_decay=4.0, amp=0.12, decay=3.0)
                for i, v in enumerate(f[:len(stab)]):
                    stab[i] += v
            stab = lowpass(bitcrush(stab, steps=40, hold=2), 3200)
            mix.add(c.at(bar, st), stab, -0.15, 1.0)
        if bar % 2 == 1:
            base = pc_midi((c.root + off) % 12, 69)
            for pos, semis, dur in motif:
                mix.add(c.at(bar, pos), mv("f", hz(base + semis), c.s(dur), ratio=3.0, index=1.0, idx_decay=9.0, amp=0.13, decay=2.6), 0.3)
    # Vinyl-Knistern
    for _ in range(int(c.n / SR * 14)):
        mix.add(rng.randrange(c.n), noise_burst(0.004, rng, hp=1500, decay=700, amp=rng.uniform(0.04, 0.16)), rng.uniform(-1, 1), 1.0)
    reverb(mix, wet=0.12, size=0.8)
    return master_music(mix, 1.4)


def corporate(c, vi):
    """Glatt-optimistisches Konzern-Nachrichtenbett (Aurelia Direct)."""
    rng, mix = c.rng, c.mix
    prog = progression(c, vi)
    K, SH = kick(f0=95, f1=50, dur=0.3, click=0.05), hat(rng, dur=0.04, amp=0.18, decay=110)
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        for st in (0, 4, 8, 12):
            mix.add(c.at(bar, st), K, 0, 0.5)
        for st in range(16):
            mix.add(c.at(bar, st), SH, 0.4 if st % 2 else -0.4, 0.55 if st % 4 == 2 else 0.35)
        hi = chord_tones(c, off, q, 60)
        hi = hi + [hi[0] + 12]
        pattern = (0, 1, 2, 3, 2, 1, 2, 3) if vi % 2 == 0 else (0, 2, 1, 3, 0, 2, 3, 1)
        for k_, st in enumerate(range(0, 16, 2)):
            m = hi[pattern[k_]] + (12 if bar % 8 >= 4 and k_ % 4 == 3 else 0)
            mix.add(c.at(bar, st), mv("f", hz(m), c.s(3), ratio=4.0, index=1.1, idx_decay=14.0, amp=0.2 if k_ % 4 == 0 else 0.15, decay=5.5),
                    -0.3 if k_ % 2 else 0.3)
        for j, m in enumerate(chord_tones(c, off, q, 48) + [chord_tones(c, off, q, 48)[0] + 12]):
            mix.add(c.at(bar, 0), mv("v", hz(m), 4 * c.beat / SR, wave_="saw", amp=0.06, a=0.9, d=0.6, s=0.85, r=0.6, cut=1900, detune=12,
                                     vib=0.003, vib_hz=4.6), (-0.6, -0.2, 0.2, 0.6)[j % 4])
        broot = pc_midi((c.root + off) % 12, 36)
        for st in (0, 4, 8, 12):
            mix.add(c.at(bar, st), mv("v", hz(broot), c.s(3), wave_="tri", amp=0.36, a=0.01, d=0.2, s=0.5, r=0.05, cut=420), 0)
        if bar % 4 == 0:
            mix.add(c.at(bar, 0), mv("f", hz(chord_tones(c, off, q, 84)[2]), 1.6, ratio=7.0, index=1.6, idx_decay=10.0, amp=0.09, decay=2.2), 0.5)
        if bar % 8 == 7:  # leise Dissonanz: das Lächeln kippt kurz
            mix.add(c.at(bar, 8), mv("v", hz(pc_midi((c.root + 6) % 12, 79)), c.s(8), wave_="sine", amp=0.07, a=0.8, d=0.2, s=0.8, r=0.3, vib=0.01), 0.6)
    reverb(mix, wet=0.28, size=1.2)
    return master_music(mix, 1.0)


def funk(c, vi):
    """Beschwingtes Talk-Bett (Open Line): Slap-Bass, Clavinet-Stabs, trockene Drums."""
    rng, mix = c.rng, c.mix
    prog = progression(c, vi)
    K, S, H, R = kick(f0=105, f1=50, dur=0.28), snare(rng, amp=0.8, hp=1400), hat(rng, amp=0.35, decay=80), rim(rng, 0.5)
    bass_pats = [[0, None, 0, 12, None, 0, 10, None, 0, None, 12, 0, None, 7, 10, None],
                 [0, 0, None, 12, None, 0, None, 7, 0, None, 0, 12, 10, None, 7, None]]
    bp = bass_pats[vi % 2]
    stab_steps = [3, 6, 10, 13] if vi % 2 == 0 else [2, 5, 8, 11, 14]
    kick_steps = (0, 3, 10) if vi % 2 == 0 else (0, 6, 10, 11)
    for bar in range(c.bars):
        off, q = prog[bar % 4]
        for st in kick_steps:
            mix.add(c.at(bar, st), K, 0, 0.85)
        for st in (4, 12):
            mix.add(c.at(bar, st), S, 0, 0.7)
        mix.add(c.at(bar, 7), R, 0.4, 0.6)
        for st in range(16):
            mix.add(c.at(bar, st), H, 0.3 if st % 2 else -0.3, 0.8 if st % 4 == 0 else (0.5 if st % 2 == 0 else 0.3))
        broot = pc_midi((c.root + off) % 12, 36)
        for st, semis in enumerate(bp):
            if semis is None:
                continue
            mix.add(c.at(bar, st), mv("v", hz(broot + semis), c.s(1.4), wave_="square", amp=0.28, a=0.002, d=0.09, s=0.2, r=0.03,
                                     cut=950, cut_env=4.0), 0)
        tones = chord_tones(c, off, "m7" if q == "m" else "7", 57)
        for st in stab_steps:
            for j, m in enumerate(tones[1:]):
                mix.add(c.at(bar, st) + c.jit(2), mv("v", hz(m), c.s(1.2), wave_="square", amp=0.07, a=0.002, d=0.07, s=0.0, r=0.04,
                                                    cut=2600, cut_env=1.5), (-0.5, 0.0, 0.5)[j % 3])
        if bar % 4 == 3:  # kurzer "Wah"-Lauf
            base = pc_midi((c.root + off) % 12, 62)
            scale = PENT_MINOR if c.minor else PENT_MAJOR
            for k_, st in enumerate((8, 10, 11, 12, 14)):
                m = base + scale[(k_ * 2 + vi) % len(scale)]
                mix.add(c.at(bar, st), mv("v", hz(m), c.s(1.6), wave_="saw", amp=0.14, a=0.004, d=0.12, s=0.3, r=0.05, cut=700,
                                         cut_env=6.0), 0.3)
    reverb(mix, wet=0.1, size=0.7)
    return master_music(mix, 1.15)


GENRES = {"synthwave": synthwave, "harbor_rock": harbor_rock, "jazz": jazz, "beat": beat_hiphop,
          "corporate": corporate, "funk": funk}


def render(station, letter):
    genre, variants = STATIONS[station]
    for idx, (let, bpm, key, seed) in enumerate(variants):
        if let != letter:
            continue
        _memo.clear()
        t0 = time.time()
        c = Ctx(bpm, key, seed)
        chans = GENRES[genre](c, idx)
        name = "radio_%s_%s" % (station, letter)
        size = write_wav(os.path.join(OUT_DIR, name + ".wav"), chans)
        info = {"file": name + ".wav", "name": name, "kind": "music", "station": station, "genre": genre,
                "bpm": bpm, "key": key, "bars": c.bars, "channels": 2, "samples": c.n,
                "seconds": round(c.n / SR, 2), "bytes": size, "loop": True,
                "seamJump": round(seam_jump(chans[0]), 2), "renderSeconds": round(time.time() - t0, 1)}
        return info
    raise KeyError((station, letter))


def tasks():
    return [(st, v[0]) for st, (g, vs) in STATIONS.items() for v in vs]


if __name__ == "__main__":
    sel = [t for t in tasks() if (len(sys.argv) < 2 or t[0] == sys.argv[1]) and (len(sys.argv) < 3 or t[1] == sys.argv[2])]
    for st, le in sel:
        print(render(st, le), flush=True)
