# -*- coding: utf-8 -*-
"""
Vorschau der Grundrisse aus Content/Data/interiors.json.

  <python> tools/ue_interiors/preview_interiors.py core_penthouse          -> ASCII je Etage
  <python> tools/ue_interiors/preview_interiors.py core_penthouse --svg out.svg
  <python> tools/ue_interiors/preview_interiors.py --all --svg-dir Saved/InteriorPreview

ASCII: 1 Zeichen = 0,5 m. Buchstaben = Raum (Legende darunter), '#' = Wand, 'D' = Tuer,
'E' = Eingang, 'W' = Fenster, 'S' = Treppe.
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
G = 0.5
COLORS = ["#e8d8b0", "#b8d8e8", "#d8b8e8", "#c8e8b8", "#f0c8a8", "#a8e0d0", "#e8b8c8", "#d0d0a0", "#b0c0f0", "#f0e0a0"]


def load():
    with open(os.path.join(ROOT, "Content", "Data", "interiors.json"), encoding="utf-8") as fh:
        return json.load(fh)


def ascii_floor(t, f):
    fw, fd = t["footprint"]
    nx, ny = round(fw / G), round(fd / G)
    grid = [[" "] * (nx * 2 + 1) for _ in range(ny * 2 + 1)]
    owner = {}
    legend = []
    for k, r in enumerate(f["rooms"]):
        ch = chr(ord("a") + k) if k < 26 else "?"
        legend.append("%s=%s(%s)" % (ch, r["id"], r["kind"]))
        x, y, w, d = r["rect"]
        for i in range(round(x / G), round((x + w) / G)):
            for j in range(round(y / G), round((y + d) / G)):
                owner[(i, j)] = ch
                grid[2 * j + 1][2 * i + 1] = ch
    for i in range(nx + 1):
        for j in range(ny):
            if owner.get((i - 1, j)) != owner.get((i, j)):
                grid[2 * j + 1][2 * i] = "#"
    for j in range(ny + 1):
        for i in range(nx):
            if owner.get((i, j - 1)) != owner.get((i, j)):
                grid[2 * j][2 * i + 1] = "#"
    def mark(at, width, ch):
        x, y = at
        if abs(x / G - round(x / G)) < 1e-6:
            i = round(x / G)
            for j in range(ny):
                if abs((j + 0.5) * G - y) < width / 2 and grid[2 * j + 1][2 * i] == "#":
                    grid[2 * j + 1][2 * i] = ch
        if abs(y / G - round(y / G)) < 1e-6:
            j = round(y / G)
            for i in range(nx):
                if abs((i + 0.5) * G - x) < width / 2 and grid[2 * j][2 * i + 1] == "#":
                    grid[2 * j][2 * i + 1] = ch
    for dr in f["doors"]:
        mark(dr["at"], dr.get("width", 1.0), "E" if dr.get("entrance") else "D")
    for wn in f["windows"]:
        mark(wn["at"], wn.get("width", 1.0), "W")
    for st in f.get("stairs", []):
        x, y, w, d = st["rect"]
        for i in range(round(x / G), round((x + w) / G)):
            for j in range(round(y / G), round((y + d) / G)):
                grid[2 * j + 1][2 * i + 1] = "S"
    # Y nach oben (Norden) zeichnen
    lines = ["".join(row) for row in reversed(grid)]
    return "\n".join(lines) + "\n" + "  ".join(legend)


def svg_template(t, scale=20):
    fw, fd = t["footprint"]
    floors = t["floors"]
    gap = 2
    W = (fw + gap) * len(floors) * scale + gap * scale
    H = (fd + 4) * scale
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" font-family="sans-serif" font-size="10">' % (W, H),
           '<rect width="100%" height="100%" fill="#1d1f24"/>',
           '<text x="10" y="16" fill="#fff" font-size="13">%s (%s) %s</text>' % (t["id"], t["roomType"], t["label"])]
    for k, f in enumerate(floors):
        ox = gap * scale + k * (fw + gap) * scale
        oy = 2 * scale
        Y = lambda y: oy + (fd - y) * scale
        out.append('<text x="%d" y="%d" fill="#aaa">Etage %d</text>' % (ox, oy - 4, f["level"]))
        for n, r in enumerate(f["rooms"]):
            x, y, w, d = r["rect"]
            out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="#333" stroke-width="3"/>' % (ox + x * scale, Y(y + d), w * scale, d * scale, COLORS[n % len(COLORS)]))
            out.append('<text x="%.1f" y="%.1f" fill="#222">%s</text>' % (ox + (x + 0.3) * scale, Y(y + d) + 12, r["id"]))
        for st in f.get("stairs", []):
            x, y, w, d = st["rect"]
            out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="#555" stroke-dasharray="4 2"/>' % (ox + x * scale, Y(y + d), w * scale, d * scale))
        for dr in f["doors"]:
            x, y = dr["at"]
            col = "#ff5050" if dr.get("entrance") else ("#ffb000" if dr.get("lock") else "#40a040")
            out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"/>' % (ox + x * scale, Y(y), dr.get("width", 1) * scale / 2, col))
            if dr.get("entrance"):
                out.append('<text x="%.1f" y="%.1f" fill="#fff">%s</text>' % (ox + x * scale + 6, Y(y) - 6, dr["entrance"]))
        for wn in f["windows"]:
            x, y = wn["at"]
            out.append('<circle cx="%.1f" cy="%.1f" r="4" fill="#60c0ff"/>' % (ox + x * scale, Y(y)))
        for s in t.get("secrets", []):
            if s.get("floor", 0) == f["level"]:
                out.append('<text x="%.1f" y="%.1f" fill="#c000c0" font-size="14">*</text>' % (ox + s["at"][0] * scale, Y(s["at"][1])))
    out.append("</svg>")
    return "\n".join(out)


def main(argv):
    data = load()
    by_id = {t["id"]: t for t in data["templates"]}
    if "--all" in argv:
        out_dir = argv[argv.index("--svg-dir") + 1] if "--svg-dir" in argv else os.path.join(ROOT, "Saved", "InteriorPreview")
        os.makedirs(out_dir, exist_ok=True)
        for t in data["templates"]:
            with open(os.path.join(out_dir, t["id"] + ".svg"), "w", encoding="utf-8") as fh:
                fh.write(svg_template(t))
        print("%d SVGs nach %s" % (len(data["templates"]), out_dir))
        return 0
    if len(argv) < 2 or argv[1] not in by_id:
        print("Vorlagen:", ", ".join(sorted(by_id)))
        return 1
    t = by_id[argv[1]]
    if "--svg" in argv:
        path = argv[argv.index("--svg") + 1]
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(svg_template(t))
        print("SVG:", path)
    else:
        for f in t["floors"]:
            print("== %s  Etage %d ==" % (t["id"], f["level"]))
            print(ascii_floor(t, f))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
