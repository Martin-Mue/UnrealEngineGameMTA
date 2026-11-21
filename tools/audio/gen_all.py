"""Erzeugt alle Radio-Loops und Ambience-Layer nach Content/Audio/Generated/ (parallel).

Aufruf:  python gen_all.py [--music] [--ambience] [--jobs N]
Nur Standardbibliothek. Schreibt zusaetzlich manifest.json.
"""
import multiprocessing as mp
import os
import sys

import gen_ambience
import gen_radio_music
from synth_common import OUT_DIR, write_manifest


def _music(t):
    return gen_radio_music.render(*t)


def _amb(l):
    return gen_ambience.render(l)


def main():
    args = sys.argv[1:]
    do_music = "--ambience" not in args or "--music" in args
    do_amb = "--music" not in args or "--ambience" in args
    jobs = max(1, min(os.cpu_count() or 2, 8))
    if "--jobs" in args:
        jobs = int(args[args.index("--jobs") + 1])
    entries = []
    with mp.Pool(jobs) as pool:
        if do_amb:
            for r in pool.imap_unordered(_amb, gen_ambience.tasks()):
                print("ambience", r["name"], r["bytes"], "seam", r["seamJump"], flush=True)
                entries.append(r)
        if do_music:
            for r in pool.imap_unordered(_music, gen_radio_music.tasks()):
                print("music", r["name"], r["bytes"], "seam", r["seamJump"], "%ss" % r["renderSeconds"], flush=True)
                entries.append(r)
    entries.sort(key=lambda e: e["name"])
    write_manifest(entries)
    total = sum(e["bytes"] for e in entries)
    print("Dateien: %d, gesamt %.1f MB" % (len(entries), total / 1048576.0))


if __name__ == "__main__":
    mp.freeze_support()
    main()
