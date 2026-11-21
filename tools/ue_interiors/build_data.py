# -*- coding: utf-8 -*-
"""
Autorenwerkzeug fuer die Innenraum-Daten von MTA 6.

Erzeugt Content/Data/furnishing.json (Moebel/Deko als Primitive) und
Content/Data/interiors.json (Raumtypen, Laeden, Gebaeudevorlagen mit Grundrissen).

Die JSON-Dateien sind das ausgelieferte Datenformat (UMTAInteriorSubsystem liest sie).
Dieses Skript ist die bequeme Quelle fuer die Erstbefuellung; Handanpassungen an den
JSON-Dateien sind erlaubt, werden aber beim naechsten Lauf ueberschrieben.

Aufruf (ohne Systempython geht der Interpreter der Engine):
  F:/UnrealEngine/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe tools/ue_interiors/build_data.py
Danach immer: tools/ue_interiors/validate_interiors.py
"""
import json
import os
import random

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(ROOT, "Content", "Data")
G = 0.5  # Rasterweite der Grundrisse in Metern

# ---------------------------------------------------------------------------
# Primitive-Helfer (Masse in Metern, pos = Mittelpunkt des Teils, z ueber Boden)
# ---------------------------------------------------------------------------

def _p(shape, x, y, z, sx, sy, sz, c, e=0.0, rot=None):
    d = {"shape": shape, "pos": [round(x, 3), round(y, 3), round(z, 3)],
         "size": [round(sx, 3), round(sy, 3), round(sz, 3)], "color": c}
    if e:
        d["emissive"] = e
    if rot:
        d["rot"] = rot  # [pitch, yaw, roll] in Grad
    return d

def box(x, y, z, sx, sy, sz, c, e=0.0, rot=None): return _p("cube", x, y, z, sx, sy, sz, c, e, rot)
def cyl(x, y, z, dx, dy, h, c, e=0.0, rot=None): return _p("cylinder", x, y, z, dx, dy, h, c, e, rot)
def sph(x, y, z, d, c, e=0.0): return _p("sphere", x, y, z, d, d, d, c, e)
def cone(x, y, z, d, h, c, e=0.0, rot=None): return _p("cone", x, y, z, d, d, h, c, e, rot)

def legs4(w, d, h, c, t=0.05):
    ox, oy = w / 2 - t, d / 2 - t
    return [box(sx * ox, sy * oy, h / 2, t, t, h, c) for sx in (-1, 1) for sy in (-1, 1)]

def table_parts(w, d, h, top, leg):
    return [box(0, 0, h - 0.025, w, d, 0.05, top)] + legs4(w, d, h - 0.05, leg)

def shelf_parts(w, d, h, n, c, goods=None):
    parts = [box(0, -w / 2 + 0.02, h / 2, d, 0.04, h, c), box(0, w / 2 - 0.02, h / 2, d, 0.04, h, c),
             box(-d / 2 + 0.01, 0, h / 2, 0.02, w, h, c)]
    for i in range(n):
        z = 0.05 + i * (h - 0.1) / max(1, n - 1)
        parts.append(box(0, 0, z, d, w - 0.08, 0.03, c))
        if goods and i < n - 1:
            for k, g in enumerate(goods):
                parts.append(box(0.0, -w / 2 + 0.2 + k * (w - 0.4) / max(1, len(goods) - 1),
                                 z + 0.12, d * 0.7, 0.16, 0.2, g))
    return parts

# Farbpalette
WOOD, DWOOD, LWOOD = "#7a5535", "#4a3322", "#a8835a"
METAL, DMETAL, STEEL = "#8a8f94", "#3c4044", "#b8c0c8"
WHITE, OFFW, BLACK = "#e8e8e4", "#cfc9bc", "#151517"
FABR, FABB, FABG, FABO = "#8c2f2f", "#2f4a7a", "#4f6b44", "#b07a2f"
GLASS, SCREEN = "#9fc4d8", "#3fb6ff"
NPINK, NCYAN, NGREEN, NAMBER, NVIOLET, NRED = "#ff3fa8", "#2ff0ff", "#4dff7a", "#ffb02e", "#a35bff", "#ff3030"
PLANT, DPLANT, POT = "#3f7a33", "#6b5a2a", "#8c4a2a"
CARD, TRASH = "#b08a5a", "#2a2a2a"

ITEMS = []

def item(iid, label, cat, fp, parts, styles=None, mount="floor", collision=True, light=None,
         activity=None, interactive=None, tags=None):
    d = {"id": iid, "label": label, "category": cat, "mount": mount, "footprint": fp,
         "collision": collision, "parts": parts}
    if styles: d["styles"] = styles
    if light: d["light"] = light
    if activity: d["activity"] = activity
    if interactive: d["interactive"] = interactive
    if tags: d["tags"] = tags
    ITEMS.append(d)

def lamp(z, color, intensity=800, radius=450, x=0.0, y=0.0):
    return {"pos": [x, y, z], "color": color, "intensity": intensity, "radius": radius}

# ---------------------------------------------------------------------------
# Moebel und Deko (Front = lokale +X-Achse, Rueckseite an der Wand)
# ---------------------------------------------------------------------------
# Betten
item("bed_single", "Einzelbett", "bed", [2.0, 0.9], [box(0, 0, 0.2, 0.9, 2.0, 0.3, DWOOD), box(0, 0, 0.42, 0.85, 1.95, 0.15, OFFW), box(0, -0.8, 0.55, 0.6, 0.3, 0.12, WHITE), box(-0.43, 0, 0.55, 0.04, 2.0, 0.7, DWOOD)], activity="sleep")
item("bed_double", "Doppelbett", "bed", [2.1, 1.6], [box(0, 0, 0.2, 1.6, 2.1, 0.3, WOOD), box(0, 0, 0.42, 1.55, 2.05, 0.15, OFFW), box(-0.6, -0.4, 0.55, 0.3, 0.55, 0.12, WHITE), box(-0.6, 0.4, 0.55, 0.3, 0.55, 0.12, WHITE), box(-0.78, 0, 0.6, 0.05, 1.6, 0.9, WOOD)], styles=["worker", "warm", "rich"], activity="sleep")
item("bed_luxury", "Designerbett", "bed", [2.2, 2.0], [box(0, 0, 0.18, 2.0, 2.2, 0.25, BLACK), box(0, 0, 0.38, 1.95, 2.15, 0.18, WHITE), box(-0.95, 0, 0.75, 0.1, 2.3, 1.1, "#c8b89a"), box(-0.6, -0.5, 0.52, 0.35, 0.6, 0.14, OFFW), box(-0.6, 0.5, 0.52, 0.35, 0.6, 0.14, OFFW)], styles=["rich", "corporate"], activity="sleep")
item("mattress_floor", "Matratze am Boden", "bed", [1.9, 0.9], [box(0, 0, 0.08, 0.9, 1.9, 0.16, "#9a8f78"), box(0, -0.75, 0.2, 0.5, 0.3, 0.1, "#bdb5a0")], styles=["poor", "dirty"], collision=False, activity="sleep")
item("bunk_bed", "Etagenbett", "bed", [2.0, 0.9], legs4(0.9, 2.0, 1.7, METAL) + [box(0, 0, 0.35, 0.85, 1.95, 0.12, "#8f8a7a"), box(0, 0, 1.3, 0.85, 1.95, 0.12, "#8f8a7a")], styles=["poor", "worker", "industrial"], activity="sleep")
item("cot_army", "Feldbett", "bed", [1.9, 0.7], [box(0, 0, 0.4, 0.7, 1.9, 0.06, "#5a6a3a")] + legs4(0.7, 1.9, 0.38, DMETAL), activity="sleep")
item("hospital_bed", "Krankenbett", "bed", [2.0, 0.95], [box(0, 0, 0.55, 0.95, 2.0, 0.15, WHITE), box(0, -0.95, 0.8, 0.95, 0.05, 0.6, STEEL), box(0, 0.95, 0.7, 0.95, 0.05, 0.4, STEEL)] + legs4(0.9, 1.9, 0.5, STEEL), activity="patient")
# Sitzen
item("sofa_worn", "Abgewetztes Sofa", "seat", [0.9, 2.0], [box(0, 0, 0.22, 0.9, 2.0, 0.35, "#6a5a3f"), box(-0.35, 0, 0.6, 0.2, 2.0, 0.6, "#6a5a3f"), box(0.05, -0.92, 0.5, 0.8, 0.16, 0.35, "#5f5038"), box(0.05, 0.92, 0.5, 0.8, 0.16, 0.35, "#5f5038")], styles=["poor", "worker", "dirty"], activity="watch_tv")
item("sofa_fabric", "Stoffsofa", "seat", [0.9, 2.1], [box(0, 0, 0.22, 0.9, 2.1, 0.35, FABB), box(-0.35, 0, 0.6, 0.2, 2.1, 0.6, FABB), box(0.05, -0.97, 0.5, 0.8, 0.16, 0.35, FABB), box(0.05, 0.97, 0.5, 0.8, 0.16, 0.35, FABB)], styles=["worker", "warm", "clean"], activity="watch_tv")
item("sofa_leather", "Ledersofa", "seat", [0.95, 2.4], [box(0, 0, 0.22, 0.95, 2.4, 0.3, "#2a1d16"), box(-0.37, 0, 0.55, 0.2, 2.4, 0.5, "#2a1d16"), box(0.05, -1.12, 0.45, 0.85, 0.16, 0.3, "#2a1d16"), box(0.05, 1.12, 0.45, 0.85, 0.16, 0.3, "#2a1d16")], styles=["rich", "corporate"], activity="watch_tv")
item("armchair", "Sessel", "seat", [0.85, 0.85], [box(0, 0, 0.22, 0.85, 0.85, 0.35, FABR), box(-0.33, 0, 0.6, 0.18, 0.85, 0.6, FABR), box(0.05, -0.36, 0.5, 0.75, 0.14, 0.3, FABR), box(0.05, 0.36, 0.5, 0.75, 0.14, 0.3, FABR)], styles=["warm", "worker", "rich"], activity="read")
item("chair_wood", "Holzstuhl", "seat", [0.45, 0.45], [box(0, 0, 0.45, 0.45, 0.45, 0.04, WOOD), box(-0.2, 0, 0.7, 0.04, 0.45, 0.5, WOOD)] + legs4(0.45, 0.45, 0.43, WOOD, 0.04))
item("chair_plastic", "Plastikstuhl", "seat", [0.45, 0.45], [box(0, 0, 0.44, 0.45, 0.45, 0.04, "#d0d0c8"), box(-0.2, 0, 0.68, 0.04, 0.45, 0.45, "#d0d0c8")] + legs4(0.45, 0.45, 0.42, "#d0d0c8", 0.03), styles=["poor", "worker", "industrial"])
item("chair_office", "Buerostuhl", "seat", [0.6, 0.6], [cyl(0, 0, 0.05, 0.6, 0.6, 0.06, BLACK), cyl(0, 0, 0.28, 0.06, 0.06, 0.4, METAL), box(0, 0, 0.5, 0.5, 0.5, 0.08, BLACK), box(-0.23, 0, 0.82, 0.06, 0.45, 0.55, BLACK)], activity="work")
item("stool_bar", "Barhocker", "seat", [0.4, 0.4], [cyl(0, 0, 0.04, 0.4, 0.4, 0.05, DMETAL), cyl(0, 0, 0.38, 0.05, 0.05, 0.7, METAL), cyl(0, 0, 0.76, 0.38, 0.38, 0.07, FABR)], activity="drink")
item("bench_wood", "Holzbank", "seat", [0.4, 1.6], [box(0, 0, 0.44, 0.4, 1.6, 0.05, WOOD)] + legs4(0.4, 1.6, 0.42, DWOOD))
item("pew", "Kirchenbank", "seat", [0.6, 3.0], [box(0, 0, 0.45, 0.45, 3.0, 0.05, DWOOD), box(-0.25, 0, 0.75, 0.06, 3.0, 0.65, DWOOD), box(0, -1.48, 0.45, 0.6, 0.05, 0.9, DWOOD), box(0, 1.48, 0.45, 0.6, 0.05, 0.9, DWOOD)], activity="pray")
item("booth_seat", "Sitznische", "seat", [0.7, 1.8], [box(0, 0, 0.22, 0.6, 1.8, 0.42, FABR), box(-0.3, 0, 0.75, 0.15, 1.8, 0.7, FABR)], styles=["neon", "worker", "warm"], activity="eat")
item("folding_chair", "Klappstuhl", "seat", [0.45, 0.45], [box(0, 0, 0.45, 0.42, 0.42, 0.03, DMETAL), box(-0.2, 0, 0.7, 0.03, 0.42, 0.35, DMETAL)] + legs4(0.42, 0.42, 0.44, DMETAL, 0.025))
item("lobby_sofa", "Lobby-Sitzbank", "seat", [0.9, 2.6], [box(0, 0, 0.22, 0.9, 2.6, 0.38, "#d8d8d8"), box(-0.35, 0, 0.55, 0.2, 2.6, 0.4, "#d8d8d8")], styles=["corporate", "rich", "clean"])
item("beanbag", "Sitzsack", "seat", [0.8, 0.8], [sph(0, 0, 0.3, 0.8, FABO)], styles=["warm", "neon", "worker"], activity="relax")
# Tische
item("table_kitchen", "Kuechentisch", "table", [0.8, 1.2], table_parts(0.8, 1.2, 0.75, LWOOD, WOOD), activity="eat")
item("table_dining", "Esstisch", "table", [1.0, 2.0], table_parts(1.0, 2.0, 0.75, DWOOD, DWOOD), styles=["warm", "rich", "worker"], activity="eat")
item("table_glass", "Glastisch", "table", [1.0, 2.2], [box(0, 0, 0.74, 1.0, 2.2, 0.02, GLASS)] + legs4(1.0, 2.2, 0.72, STEEL, 0.04), styles=["rich", "corporate", "clean"], activity="eat")
item("table_coffee", "Couchtisch", "table", [0.6, 1.1], table_parts(0.6, 1.1, 0.42, WOOD, DWOOD))
item("table_crate", "Kistentisch", "table", [0.8, 0.8], [box(0, 0, 0.25, 0.8, 0.8, 0.5, CARD)], styles=["poor", "dirty", "industrial"])
item("table_cafe", "Bistrotisch", "table", [0.7, 0.7], [cyl(0, 0, 0.74, 0.7, 0.7, 0.03, WHITE), cyl(0, 0, 0.37, 0.06, 0.06, 0.72, METAL), cyl(0, 0, 0.02, 0.45, 0.45, 0.04, METAL)], activity="eat")
item("table_folding", "Klapptisch", "table", [0.75, 1.8], table_parts(0.75, 1.8, 0.74, "#d8d4c8", DMETAL))
item("table_pool", "Billardtisch", "table", [1.4, 2.5], [box(0, 0, 0.72, 1.4, 2.5, 0.12, DWOOD), box(0, 0, 0.79, 1.25, 2.35, 0.02, "#1f6b3a"), box(0, 0, 0.35, 1.1, 2.1, 0.6, DWOOD)], activity="play")
item("table_interview", "Verhoertisch", "table", [0.9, 1.4], table_parts(0.9, 1.4, 0.75, STEEL, STEEL) + [cyl(0, 0, 0.78, 0.05, 0.05, 0.05, DMETAL)])
item("table_operating", "OP-Tisch", "table", [0.7, 2.0], [box(0, 0, 0.9, 0.7, 2.0, 0.08, STEEL), cyl(0, 0, 0.45, 0.25, 0.25, 0.85, STEEL), box(0, 0, 0.04, 0.6, 0.8, 0.08, DMETAL)], activity="surgery")
# Arbeit
item("desk_wood", "Schreibtisch", "desk", [0.7, 1.4], table_parts(0.7, 1.4, 0.75, WOOD, DWOOD) + [box(0, 0.45, 0.4, 0.6, 0.4, 0.65, DWOOD)], activity="work")
item("desk_office", "Buerotisch", "desk", [0.8, 1.6], table_parts(0.8, 1.6, 0.74, "#e2e2de", METAL) + [box(-0.15, 0, 1.0, 0.04, 0.55, 0.35, BLACK), box(-0.13, 0, 1.0, 0.02, 0.5, 0.3, SCREEN, 1.2)], styles=["corporate", "clean", "worker"], activity="work")
item("desk_executive", "Chefschreibtisch", "desk", [1.0, 2.2], [box(0, 0, 0.38, 0.9, 2.2, 0.76, DWOOD), box(0, 0, 0.77, 1.0, 2.2, 0.04, BLACK), box(-0.2, 0.5, 1.0, 0.04, 0.6, 0.38, BLACK), box(-0.18, 0.5, 1.0, 0.02, 0.55, 0.32, SCREEN, 1.0)], styles=["rich", "corporate"], activity="work")
item("reception_desk", "Empfangstresen", "desk", [1.0, 3.0], [box(0, 0, 0.55, 0.9, 3.0, 1.1, WHITE), box(0.1, 0, 1.12, 0.8, 3.0, 0.04, "#2a3a4a"), box(0.46, 0, 0.6, 0.02, 2.6, 0.12, NCYAN, 4.0)], styles=["corporate", "clean", "rich"], activity="reception")
item("workbench", "Werkbank", "work", [0.8, 2.0], table_parts(0.8, 2.0, 0.9, DWOOD, DMETAL) + [box(-0.35, 0, 1.4, 0.05, 2.0, 0.9, "#5a5a50"), box(0.1, 0.6, 0.97, 0.3, 0.2, 0.12, "#a03020")], styles=["industrial", "worker", "poor", "warm"], activity="repair")
item("soldering_desk", "Loetarbeitsplatz", "work", [0.8, 1.6], table_parts(0.8, 1.6, 0.8, "#3a3a3a", DMETAL) + [box(0.1, -0.3, 0.86, 0.3, 0.4, 0.08, "#1f5f2f"), cyl(0.1, 0.4, 0.95, 0.12, 0.12, 0.25, NAMBER, 0.5), box(-0.25, 0.2, 1.1, 0.04, 0.5, 0.35, BLACK), box(-0.23, 0.2, 1.1, 0.02, 0.45, 0.3, NGREEN, 1.5)], activity="hack")
item("filing_cabinet", "Aktenschrank", "storage", [0.6, 0.5], [box(0, 0, 0.66, 0.6, 0.5, 1.32, "#7d8288")] + [box(0.31, 0, 0.3 + i * 0.32, 0.02, 0.45, 0.28, "#6a6f74") for i in range(4)])
item("cubicle_wall", "Trennwand Grossraum", "office", [0.08, 1.6], [box(0, 0, 0.7, 0.06, 1.6, 1.4, "#8a92a0")], styles=["corporate", "clean"])
item("copier", "Kopierer", "office", [0.7, 0.7], [box(0, 0, 0.5, 0.7, 0.7, 1.0, OFFW), box(0, 0, 1.03, 0.6, 0.6, 0.06, DMETAL)])
item("water_cooler", "Wasserspender", "office", [0.35, 0.35], [box(0, 0, 0.5, 0.35, 0.35, 1.0, WHITE), cyl(0, 0, 1.2, 0.3, 0.3, 0.4, "#6fb7e8")], activity="idle")
item("whiteboard", "Whiteboard", "office", [0.1, 1.6], [box(0, 0, 1.5, 0.04, 1.6, 1.0, WHITE)], mount="wall", collision=False)
# Lagerung
item("shelf_wood", "Holzregal", "storage", [0.4, 1.0], shelf_parts(1.0, 0.4, 1.8, 5, WOOD, [FABO, "#3f5f8f", "#8f3f3f"]))
item("bookshelf", "Buecherregal", "storage", [0.35, 1.2], shelf_parts(1.2, 0.35, 2.0, 6, DWOOD, ["#6a2f2f", "#2f4f6a", "#c8a050", "#3a5a3a"]), styles=["warm", "rich", "worker"], activity="read")
item("shelf_metal", "Metallregal", "storage", [0.5, 1.2], shelf_parts(1.2, 0.5, 2.0, 5, DMETAL, [CARD, CARD, "#4a4a4a"]))
item("pallet_rack", "Palettenregal", "storage", [1.1, 2.7], shelf_parts(2.7, 1.1, 4.0, 3, "#e07a1f", [CARD, CARD, "#6b5a3a"]), styles=["industrial", "worker"])
item("wardrobe", "Kleiderschrank", "storage", [0.6, 1.2], [box(0, 0, 1.0, 0.6, 1.2, 2.0, WOOD), box(0.31, -0.05, 1.0, 0.02, 0.02, 0.2, METAL), box(0.31, 0.05, 1.0, 0.02, 0.02, 0.2, METAL)], interactive="hiding")
item("locker_row", "Spindreihe", "storage", [0.5, 1.5], [box(0, -0.5 + i * 0.5, 0.95, 0.5, 0.48, 1.9, "#4f6a7a") for i in range(3)], styles=["industrial", "worker", "clean"], interactive="hiding")
item("evidence_locker", "Asservatenschrank", "storage", [0.6, 1.8], [box(0, 0, 1.0, 0.6, 1.8, 2.0, "#3a4a3a"), box(0.31, 0, 1.0, 0.02, 1.7, 1.9, "#2f3f2f")])
item("medical_cabinet", "Medizinschrank", "storage", [0.4, 0.9], [box(0, 0, 1.0, 0.4, 0.9, 1.8, WHITE), box(0.21, 0, 1.4, 0.02, 0.8, 0.7, GLASS), box(0.22, 0, 1.6, 0.01, 0.2, 0.2, NRED, 0.8)])
item("crate_wood", "Holzkiste", "clutter", [0.8, 0.8], [box(0, 0, 0.35, 0.8, 0.8, 0.7, "#8a6a3a"), box(0.41, 0, 0.35, 0.02, 0.6, 0.1, "#6a4a2a")])
item("crate_stack", "Kistenstapel", "clutter", [1.0, 1.0], [box(0, 0, 0.35, 1.0, 1.0, 0.7, "#8a6a3a"), box(0.05, 0.05, 1.0, 0.8, 0.8, 0.6, "#7a5a32"), box(0, 0, 1.5, 0.5, 0.5, 0.4, CARD)], styles=["industrial", "worker", "poor"])
item("supply_crate", "Versorgungskiste", "clutter", [0.6, 1.0], [box(0, 0, 0.25, 0.6, 1.0, 0.5, "#3f4f2f"), box(0.31, 0, 0.35, 0.02, 0.3, 0.1, NAMBER, 0.6)])
item("cardboard_boxes", "Kartons", "clutter", [0.6, 0.6], [box(0, 0, 0.2, 0.6, 0.6, 0.4, CARD), box(0.05, -0.05, 0.55, 0.45, 0.4, 0.3, "#a07a4a")], collision=False)
item("pallet", "Palette", "clutter", [1.2, 0.8], [box(0, 0, 0.07, 0.8, 1.2, 0.14, "#a08a5a")], collision=False)
item("barrel_oil", "Oelfass", "clutter", [0.6, 0.6], [cyl(0, 0, 0.45, 0.6, 0.6, 0.9, "#2f5a8a"), cyl(0, 0, 0.91, 0.55, 0.55, 0.02, DMETAL)], styles=["industrial", "poor", "worker"])
item("barrel_fire", "Feuertonne", "clutter", [0.6, 0.6], [cyl(0, 0, 0.45, 0.6, 0.6, 0.9, "#5a3a2a"), cone(0, 0, 1.0, 0.4, 0.35, "#ff7a1f", 6.0)], styles=["poor", "dirty", "industrial"], light=lamp(1.1, "#ff8a3a", 1500, 600), activity="warm_hands")
# Kueche, Bad
item("stove", "Herd", "kitchen", [0.6, 0.6], [box(0, 0, 0.45, 0.6, 0.6, 0.9, WHITE), box(0, 0, 0.91, 0.55, 0.55, 0.02, BLACK), box(0.31, 0, 0.45, 0.02, 0.45, 0.35, "#222")], activity="cook")
item("fridge", "Kuehlschrank", "kitchen", [0.65, 0.65], [box(0, 0, 0.9, 0.65, 0.65, 1.8, OFFW), box(0.33, 0.25, 1.1, 0.03, 0.03, 0.4, METAL)])
item("kitchen_counter", "Kuechenzeile", "kitchen", [0.6, 2.4], [box(0, 0, 0.45, 0.6, 2.4, 0.9, WOOD), box(0, 0, 0.92, 0.62, 2.4, 0.04, "#d0ccc0"), box(0, 0.6, 0.9, 0.4, 0.5, 0.05, STEEL), box(-0.2, 0, 1.8, 0.35, 2.4, 0.7, WOOD)], activity="cook")
item("kitchenette", "Kochnische", "kitchen", [0.6, 1.6], [box(0, 0, 0.45, 0.6, 1.6, 0.9, "#9a9a92"), box(0, -0.4, 0.92, 0.5, 0.5, 0.03, BLACK), box(0, 0.4, 0.9, 0.4, 0.45, 0.05, STEEL)], styles=["poor", "worker", "neon"], activity="cook")
item("griddle", "Grillplatte", "kitchen", [0.8, 1.6], [box(0, 0, 0.45, 0.8, 1.6, 0.9, STEEL), box(0, 0, 0.93, 0.7, 1.5, 0.04, "#2a2a2a"), box(0, 0, 0.96, 0.6, 1.3, 0.01, "#ff6a2a", 0.6)], activity="cook")
item("fryer", "Fritteuse", "kitchen", [0.6, 0.6], [box(0, 0, 0.45, 0.6, 0.6, 0.9, STEEL), box(0, 0, 0.92, 0.5, 0.5, 0.05, "#b08a2a")], activity="cook")
item("sink_kitchen", "Spuele", "kitchen", [0.6, 1.0], [box(0, 0, 0.45, 0.6, 1.0, 0.9, STEEL), box(0, 0, 0.88, 0.45, 0.6, 0.06, DMETAL), cyl(-0.2, 0, 1.05, 0.04, 0.04, 0.3, METAL)], activity="wash")
item("microwave", "Mikrowelle", "kitchen", [0.4, 0.5], [box(0, 0, 0.15, 0.4, 0.5, 0.3, BLACK), box(0.21, -0.05, 0.15, 0.01, 0.3, 0.2, "#2a4a3a", 0.3)], mount="surface", collision=False)
item("toilet", "Toilette", "bath", [0.7, 0.45], [box(0.05, 0, 0.2, 0.55, 0.4, 0.4, WHITE), box(-0.25, 0, 0.6, 0.18, 0.42, 0.4, WHITE)])
item("toilet_dirty", "Verdreckte Toilette", "bath", [0.7, 0.45], [box(0.05, 0, 0.2, 0.55, 0.4, 0.4, "#b0a888"), box(-0.25, 0, 0.6, 0.18, 0.42, 0.4, "#a09878")], styles=["poor", "dirty"])
item("shower", "Dusche", "bath", [0.9, 0.9], [box(0, 0, 0.03, 0.9, 0.9, 0.06, WHITE), box(0.44, 0, 1.0, 0.02, 0.9, 1.9, GLASS), cyl(-0.4, 0, 1.9, 0.15, 0.15, 0.05, METAL)])
item("bathtub", "Badewanne", "bath", [0.8, 1.7], [box(0, 0, 0.28, 0.8, 1.7, 0.56, WHITE), box(0, 0, 0.5, 0.65, 1.55, 0.1, "#9ac0d0")], styles=["warm", "rich", "worker", "clean"], activity="bathe")
item("sink_bath", "Waschbecken", "bath", [0.45, 0.6], [box(0, 0, 0.82, 0.45, 0.6, 0.15, WHITE), cyl(0, 0, 0.42, 0.12, 0.12, 0.7, WHITE), box(-0.21, 0, 1.4, 0.02, 0.5, 0.6, "#c8d8e0", 0.15)])
item("washing_machine", "Waschmaschine", "bath", [0.6, 0.6], [box(0, 0, 0.42, 0.6, 0.6, 0.85, WHITE), cyl(0.31, 0, 0.42, 0.4, 0.4, 0.02, "#3a5a7a", rot=[0, 0, 90])])
# Technik
item("tv_crt", "Roehrenfernseher", "tech", [0.5, 0.6], [box(0, 0, 0.25, 0.5, 0.6, 0.5, "#2a2620"), box(0.26, 0, 0.27, 0.02, 0.45, 0.35, "#4a7a8a", 1.5)], styles=["poor", "worker", "dirty"])
item("tv_flat", "Flachbildschirm", "tech", [0.1, 1.4], [box(0, 0, 1.4, 0.05, 1.4, 0.8, BLACK), box(0.03, 0, 1.4, 0.01, 1.32, 0.72, SCREEN, 2.0)], mount="wall", collision=False)
item("tv_stand", "TV-Mobel", "tech", [0.45, 1.6], [box(0, 0, 0.25, 0.45, 1.6, 0.5, DWOOD), box(-0.1, 0, 0.82, 0.08, 1.3, 0.62, BLACK), box(-0.05, 0, 0.82, 0.01, 1.22, 0.55, SCREEN, 1.8)], activity="watch_tv")
item("terminal_public", "Oeffentliches Terminal", "tech", [0.5, 0.6], [box(0, 0, 0.6, 0.5, 0.6, 1.2, "#2a3440"), box(0.15, 0, 1.35, 0.1, 0.55, 0.45, "#1a2028", rot=[-20, 0, 0]), box(0.21, 0, 1.36, 0.01, 0.48, 0.38, NCYAN, 2.5, rot=[-20, 0, 0])], interactive="terminal", activity="hack")
item("terminal_desk", "Arbeitsterminal", "tech", [0.6, 0.8], [box(0, 0, 0.37, 0.6, 0.8, 0.74, DMETAL), box(-0.1, 0, 1.0, 0.05, 0.7, 0.45, BLACK), box(-0.07, 0, 1.0, 0.01, 0.65, 0.4, NGREEN, 2.0)], interactive="terminal", activity="hack")
item("multi_monitor", "Monitorwand", "tech", [0.8, 1.8], table_parts(0.8, 1.8, 0.75, BLACK, DMETAL) + [box(-0.25, y, z, 0.04, 0.55, 0.33, BLACK) for y in (-0.6, 0, 0.6) for z in (1.0, 1.36)] + [box(-0.22, y, z, 0.01, 0.5, 0.3, c, 1.8) for (y, c) in ((-0.6, NGREEN), (0, SCREEN), (0.6, NVIOLET)) for z in (1.0, 1.36)], styles=["neon", "corporate", "industrial", "worker"], interactive="terminal", activity="hack")
item("server_rack", "Serverschrank", "tech", [0.9, 0.6], [box(0, 0, 1.0, 0.9, 0.6, 2.0, BLACK)] + [box(0.46, 0, 0.3 + i * 0.22, 0.01, 0.45, 0.04, NGREEN if i % 2 else SCREEN, 3.0) for i in range(8)], light=lamp(1.0, "#3fb6ff", 200, 250, 0.6), activity="hack")
item("cctv_monitors", "Ueberwachungsmonitore", "tech", [0.6, 1.8], [box(0, 0, 0.4, 0.6, 1.8, 0.8, DMETAL)] + [box(-0.1, y, z, 0.05, 0.55, 0.35, "#1a1a1a") for y in (-0.6, 0, 0.6) for z in (1.05, 1.45)] + [box(-0.07, y, z, 0.01, 0.5, 0.3, "#8ab0a0", 1.5) for y in (-0.6, 0, 0.6) for z in (1.05, 1.45)], interactive="terminal", activity="guard")
item("arcade_cabinet", "Spielautomat", "tech", [0.7, 0.7], [box(0, 0, 0.9, 0.7, 0.7, 1.8, "#3a1a5a"), box(0.3, 0, 1.3, 0.12, 0.6, 0.45, BLACK, rot=[-15, 0, 0]), box(0.37, 0, 1.31, 0.01, 0.55, 0.4, NPINK, 3.0, rot=[-15, 0, 0]), box(0.2, 0, 1.75, 0.3, 0.7, 0.12, NCYAN, 4.0)], styles=["neon", "worker", "industrial"], light=lamp(1.4, "#ff3fa8", 300, 300, 0.6), activity="play")
item("vending_machine", "Verkaufsautomat", "tech", [0.8, 1.0], [box(0, 0, 0.95, 0.8, 1.0, 1.9, "#b02a2a"), box(0.41, -0.15, 1.1, 0.02, 0.6, 1.3, "#c8e8f0", 1.5)], light=lamp(1.2, "#d8f0ff", 300, 300, 0.7))
item("credit_terminal", "Kreditautomat", "tech", [0.4, 0.6], [box(0, 0, 0.75, 0.4, 0.6, 1.5, "#2a3a4a"), box(0.21, 0, 1.2, 0.01, 0.4, 0.3, NAMBER, 2.0)], interactive="terminal")
item("cash_register", "Kasse", "tech", [0.4, 0.4], [box(0, 0, 0.08, 0.4, 0.4, 0.16, BLACK), box(-0.1, 0, 0.3, 0.05, 0.3, 0.2, NGREEN, 1.0)], mount="surface", collision=False)
item("radio", "Radio", "tech", [0.2, 0.4], [box(0, 0, 0.1, 0.2, 0.4, 0.2, "#5a4a3a"), cyl(0.1, -0.1, 0.1, 0.12, 0.12, 0.02, BLACK, rot=[0, 0, 90])], mount="surface", collision=False)
item("laptop", "Laptop", "tech", [0.3, 0.35], [box(0, 0, 0.01, 0.3, 0.35, 0.02, DMETAL), box(-0.14, 0, 0.13, 0.02, 0.35, 0.24, "#3fb6ff", 1.5, rot=[0, 0, 0])], mount="surface", collision=False)
item("printer_3d", "3D-Drucker", "tech", [0.6, 0.6], [box(0, 0, 0.4, 0.6, 0.6, 0.8, "#2a2a30"), box(0.31, 0, 0.45, 0.01, 0.5, 0.6, GLASS), box(0, 0, 0.3, 0.1, 0.1, 0.1, NAMBER, 1.0)], mount="floor", styles=["neon", "worker", "industrial", "clean"])
item("drone_parts", "Drohnenteile", "tech", [0.6, 0.6], [box(0, 0, 0.05, 0.3, 0.3, 0.08, DMETAL)] + [cyl(sx * 0.22, sy * 0.22, 0.09, 0.2, 0.2, 0.02, BLACK) for sx in (-1, 1) for sy in (-1, 1)], mount="surface", collision=False)
item("cable_bundle", "Kabelstraenge", "tech", [0.3, 2.0], [cyl(0, y, 0.04, 0.06, 0.06, 2.0, c, rot=[90, 0, 0]) for (y, c) in ((-0.08, BLACK), (0, "#c02020"), (0.08, "#2040c0"))], collision=False, mount="floor")
item("jukebox", "Musikbox", "tech", [0.6, 0.9], [box(0, 0, 0.7, 0.6, 0.9, 1.4, "#6a2a1a"), cyl(0.25, 0, 1.4, 0.2, 0.9, 0.5, NAMBER, 3.0, rot=[0, 0, 0])], styles=["warm", "worker", "neon"], light=lamp(1.4, "#ffb02e", 300, 300, 0.5), activity="dance")
item("speaker_stack", "Boxenturm", "tech", [0.7, 0.8], [box(0, 0, 0.5, 0.7, 0.8, 1.0, BLACK), box(0, 0, 1.4, 0.6, 0.7, 0.8, BLACK), cyl(0.36, 0, 0.5, 0.5, 0.5, 0.02, "#333", rot=[0, 90, 0]), cyl(0.31, 0, 1.4, 0.4, 0.4, 0.02, "#333", rot=[0, 90, 0])])
item("dj_booth", "DJ-Pult", "tech", [0.8, 2.0], [box(0, 0, 0.5, 0.8, 2.0, 1.0, "#1a1a20"), box(0.41, 0, 0.5, 0.02, 1.8, 0.1, NVIOLET, 5.0), box(-0.1, -0.5, 1.03, 0.4, 0.4, 0.05, "#333"), box(-0.1, 0.5, 1.03, 0.4, 0.4, 0.05, "#333")], styles=["neon", "worker", "warm"], light=lamp(1.6, "#a35bff", 1200, 700, 1.0), activity="dj")
item("police_terminal", "Polizeiterminal", "tech", [0.7, 1.2], table_parts(0.7, 1.2, 0.75, "#2a3a5a", DMETAL) + [box(-0.15, 0, 1.0, 0.04, 0.6, 0.38, BLACK), box(-0.13, 0, 1.0, 0.01, 0.55, 0.33, "#3f8fff", 2.0)], interactive="terminal", activity="work")
item("aegis_camera", "Aegis-Kamera", "tech", [0.3, 0.3], [box(0, 0, -0.25, 0.08, 0.08, 0.3, DMETAL), box(0.1, 0, -0.45, 0.3, 0.15, 0.15, WHITE), sph(0.26, 0, -0.45, 0.06, NRED, 6.0)], mount="ceiling", collision=False, styles=["corporate", "clean", "rich"])
# Beleuchtung
item("lamp_floor", "Stehlampe", "light", [0.4, 0.4], [cyl(0, 0, 0.02, 0.35, 0.35, 0.04, DMETAL), cyl(0, 0, 0.8, 0.04, 0.04, 1.6, DMETAL), cone(0, 0, 1.62, 0.45, 0.35, "#f0e0c0", 1.5, rot=[180, 0, 0])], collision=False, light=lamp(1.5, "#ffd8a0", 600, 450))
item("lamp_desk", "Schreibtischlampe", "light", [0.25, 0.25], [cyl(0, 0, 0.02, 0.15, 0.15, 0.03, BLACK), cyl(0, 0, 0.2, 0.03, 0.03, 0.35, BLACK), cone(0.05, 0, 0.4, 0.18, 0.15, "#e0e0d0", 1.2, rot=[150, 0, 0])], mount="surface", collision=False, light=lamp(0.35, "#ffe0b0", 250, 250))
item("bulb_bare", "Nackte Gluehbirne", "light", [0.2, 0.2], [cyl(0, 0, -0.3, 0.01, 0.01, 0.6, BLACK), sph(0, 0, -0.65, 0.1, "#fff0c0", 8.0)], mount="ceiling", collision=False, styles=["poor", "dirty", "industrial", "worker"], light=lamp(-0.7, "#ffcf88", 700, 500))
item("lamp_pendant", "Haengelampe", "light", [0.5, 0.5], [cyl(0, 0, -0.3, 0.01, 0.01, 0.6, BLACK), cone(0, 0, -0.7, 0.5, 0.3, "#c8a050", 0.0), sph(0, 0, -0.78, 0.12, "#fff0d0", 6.0)], mount="ceiling", collision=False, styles=["warm", "rich", "worker"], light=lamp(-0.85, "#ffd8a0", 900, 600))
item("lamp_fluorescent", "Leuchtstoffroehre", "light", [0.3, 1.5], [box(0, 0, -0.05, 0.25, 1.5, 0.08, "#d8d8d8"), box(0, 0, -0.1, 0.12, 1.4, 0.03, "#f0fff8", 6.0)], mount="ceiling", collision=False, styles=["industrial", "worker", "clean", "corporate", "poor"], light=lamp(-0.2, "#e8fff0", 1100, 650))
item("lamp_led_panel", "LED-Deckenpanel", "light", [1.2, 1.2], [box(0, 0, -0.02, 1.2, 1.2, 0.03, "#f8fbff", 5.0)], mount="ceiling", collision=False, styles=["corporate", "clean", "rich"], light=lamp(-0.1, "#f0f6ff", 1400, 700))
item("string_lights", "Lichterkette", "light", [0.1, 3.0], [sph(0, -1.4 + i * 0.35, -0.1 - 0.06 * (i % 2), 0.06, [NAMBER, NPINK, NCYAN][i % 3], 8.0) for i in range(9)], mount="ceiling", collision=False, styles=["warm", "neon", "worker"], light=lamp(-0.15, "#ffc070", 300, 350))
item("candle_stand", "Kerzenstaender", "light", [0.4, 0.4], [cyl(0, 0, 0.6, 0.06, 0.06, 1.2, "#b08a3a"), cyl(0, 0, 1.25, 0.05, 0.05, 0.12, OFFW), cone(0, 0, 1.36, 0.03, 0.06, "#ffb040", 10.0)], collision=False, styles=["warm", "rich", "worker", "poor"], light=lamp(1.4, "#ffa040", 150, 250))
item("emergency_light", "Notleuchte", "light", [0.15, 0.4], [box(0, 0, 2.2, 0.08, 0.4, 0.15, WHITE), box(0.05, 0, 2.2, 0.01, 0.35, 0.1, NGREEN, 5.0)], mount="wall", collision=False)
item("med_lamp", "OP-Leuchte", "light", [0.8, 0.8], [cyl(0, 0, -0.4, 0.04, 0.04, 0.8, STEEL), cyl(0, 0, -0.85, 0.7, 0.7, 0.12, WHITE), cyl(0, 0, -0.92, 0.55, 0.55, 0.02, "#ffffff", 8.0)], mount="ceiling", collision=False, light=lamp(-1.0, "#ffffff", 2500, 400))
# Neon
for nid, label, col, w in (("neon_open", "Neon: OFFEN", NRED, 1.0), ("neon_bar", "Neon: BAR", NPINK, 1.2), ("neon_noodles", "Neon: Nudeln", NAMBER, 1.4),
                           ("neon_arrow", "Neon-Pfeil", NCYAN, 1.0), ("neon_heart", "Neon-Herz", NPINK, 0.6), ("neon_cocktail", "Neon-Cocktail", NVIOLET, 0.7),
                           ("neon_kestrel", "Neon: Kestrel-Vogel", NCYAN, 1.6), ("neon_cross", "Neon-Kreuz", NGREEN, 0.6), ("neon_24h", "Neon: 24H", NGREEN, 0.9),
                           ("neon_strip", "Neonleiste", NVIOLET, 2.4)):
    item(nid, label, "neon", [0.1, w], [box(0, 0, 1.9, 0.03, w, 0.04, col, 12.0), box(0, 0, 1.9 + (0.25 if w < 2 else 0), 0.03, w * 0.8, 0.04, col, 12.0), box(-0.02, 0, 1.95, 0.01, w + 0.1, 0.5, "#111", 0.0)],
         mount="wall", collision=False, styles=["neon", "worker", "industrial", "poor", "warm"], light=lamp(1.95, col, 400, 380, 0.25))
# Deko an der Wand
for pid, label, cols, st in (("poster_band", "Bandposter", ("#c8323a", "#202020"), ["warm", "worker", "poor", "neon"]),
                             ("poster_propaganda", "Aurelia-Propagandaplakat", ("#2a5aa8", WHITE), ["corporate", "clean", "worker"]),
                             ("poster_resistance", "Widerstandsplakat", ("#e8d020", BLACK), ["warm", "poor", "dirty"]),
                             ("poster_movie", "Filmplakat", ("#f08a20", "#301040"), ["neon", "worker", "warm"]),
                             ("poster_pinup_car", "Autoposter", ("#d02020", "#e0e0e0"), ["industrial", "worker"]),
                             ("poster_union", "Gewerkschaftsplakat", ("#b03020", "#f0e8d0"), ["worker", "industrial"]),
                             ("poster_wanted", "Fahndungsplakat", (OFFW, "#303030"), ["clean", "corporate", "worker"]),
                             ("poster_beach", "Strandposter", ("#2fd0d0", "#ff8ac0"), ["neon", "rich", "clean"])):
    item(pid, label, "decor", [0.05, 0.7], [box(0, 0, 1.6, 0.01, 0.6, 0.85, cols[0]), box(0.006, 0, 1.75, 0.005, 0.45, 0.3, cols[1])], mount="wall", collision=False, styles=st)
item("painting_oil", "Oelgemaelde", "decor", [0.08, 1.2], [box(0, 0, 1.6, 0.05, 1.2, 0.9, "#b08a3a"), box(0.03, 0, 1.6, 0.01, 1.05, 0.75, "#3a5a6a")], mount="wall", collision=False, styles=["rich", "warm", "corporate"])
item("art_abstract", "Abstrakte Kunst", "decor", [0.05, 1.6], [box(0, 0, 1.6, 0.04, 1.6, 1.1, WHITE), box(0.03, -0.3, 1.7, 0.01, 0.6, 0.5, NPINK), box(0.03, 0.35, 1.45, 0.01, 0.5, 0.6, "#2a6aa8")], mount="wall", collision=False, styles=["rich", "corporate", "clean"])
item("graffiti_panel", "Graffiti", "decor", [0.03, 2.0], [box(0, 0, 1.2, 0.01, 2.0, 1.2, "#3a8a3a", 0.3), box(0.006, -0.4, 1.3, 0.005, 0.9, 0.5, NPINK, 0.4), box(0.006, 0.5, 1.0, 0.005, 0.8, 0.4, "#f0d020", 0.3)], mount="wall", collision=False, styles=["poor", "dirty", "industrial", "warm"])
item("clock_wall", "Wanduhr", "decor", [0.05, 0.35], [cyl(0, 0, 2.1, 0.35, 0.35, 0.04, WHITE, rot=[0, 0, 90]), box(0.03, 0, 2.15, 0.01, 0.02, 0.12, BLACK)], mount="wall", collision=False)
item("mirror", "Spiegel", "decor", [0.03, 0.6], [box(0, 0, 1.5, 0.02, 0.6, 0.9, "#d0e0e8", 0.1)], mount="wall", collision=False)
item("corkboard", "Pinnwand mit Faeden", "decor", [0.05, 1.5], [box(0, 0, 1.5, 0.03, 1.5, 1.0, "#a07848")] + [box(0.02, -0.5 + i * 0.25, 1.4 + 0.2 * (i % 2), 0.005, 0.18, 0.24, OFFW) for i in range(5)] + [box(0.025, 0, 1.55, 0.003, 1.2, 0.01, NRED)], mount="wall", collision=False, activity="plan")
item("menu_board", "Menuetafel", "decor", [0.05, 1.6], [box(0, 0, 2.1, 0.04, 1.6, 0.7, BLACK), box(0.025, 0, 2.1, 0.005, 1.4, 0.55, NAMBER, 1.5)], mount="wall", collision=False)
item("corporate_logo", "Aurelia-Logowand", "decor", [0.1, 3.0], [box(0, 0, 1.8, 0.08, 3.0, 1.6, "#1c2a3a"), cyl(0.05, 0, 1.9, 0.9, 0.9, 0.02, "#d8e8ff", 4.0, rot=[0, 0, 90])], mount="wall", collision=False, styles=["corporate", "clean", "rich"])
item("banner_union", "Gewerkschaftsbanner", "decor", [0.03, 2.4], [box(0, 0, 2.0, 0.01, 2.4, 1.0, "#a82a20"), box(0.006, 0, 2.0, 0.005, 1.6, 0.3, "#f0e0c0")], mount="wall", collision=False, styles=["worker", "industrial", "warm"])
item("tool_wall", "Werkzeugwand", "decor", [0.05, 2.0], [box(0, 0, 1.5, 0.03, 2.0, 1.2, "#7a7a6a")] + [box(0.03, -0.8 + i * 0.2, 1.5 + 0.2 * (i % 3) - 0.2, 0.02, 0.05, 0.3, ["#a03020", METAL, "#e0a020"][i % 3]) for i in range(9)], mount="wall", collision=False)
item("ac_unit", "Klimageraet", "decor", [0.3, 0.9], [box(0, 0, 2.4, 0.25, 0.9, 0.3, OFFW), box(0.13, 0, 2.3, 0.01, 0.8, 0.05, "#333")], mount="wall", collision=False)
item("radiator", "Heizkoerper", "decor", [0.12, 1.0], [box(0, 0, 0.45, 0.1, 1.0, 0.6, OFFW)], mount="wall", collision=False)
item("pipes_wall", "Rohrleitungen", "decor", [0.2, 3.0], [cyl(0, 0, 2.5, 0.12, 0.12, 3.0, "#6a5a4a", rot=[90, 0, 0]), cyl(0, 0, 2.7, 0.08, 0.08, 3.0, "#5a6a5a", rot=[90, 0, 0])], mount="wall", collision=False, styles=["industrial", "poor", "dirty", "worker"])
item("fire_extinguisher", "Feuerloescher", "decor", [0.15, 0.15], [cyl(0, 0, 0.9, 0.15, 0.15, 0.5, NRED), box(0, 0, 1.2, 0.05, 0.08, 0.08, BLACK)], mount="wall", collision=False)
item("altar_cross", "Altarkreuz", "decor", [0.05, 0.9], [box(0, 0, 2.4, 0.06, 0.12, 1.4, "#c8a050"), box(0, 0, 2.7, 0.06, 0.9, 0.12, "#c8a050")], mount="wall", collision=False)
item("curtain", "Vorhang", "decor", [0.05, 1.6], [box(0, 0, 1.4, 0.04, 1.6, 2.6, FABR)], mount="wall", collision=False, styles=["warm", "rich", "worker", "neon"])
item("bars_window", "Fenstergitter", "decor", [0.05, 1.2], [box(0, -0.5 + i * 0.2, 1.5, 0.03, 0.03, 1.4, DMETAL) for i in range(6)], mount="wall", collision=False, styles=["poor", "industrial", "dirty", "worker"])
# Pflanzen
item("plant_potted", "Topfpflanze", "plant", [0.5, 0.5], [cyl(0, 0, 0.2, 0.4, 0.4, 0.4, POT), sph(0, 0, 0.7, 0.6, PLANT)], collision=False)
item("plant_palm", "Zimmerpalme", "plant", [0.7, 0.7], [cyl(0, 0, 0.25, 0.5, 0.5, 0.5, WHITE), cyl(0, 0, 0.9, 0.08, 0.08, 0.9, "#6a5a3a")] + [cone(0.3 * dx, 0.3 * dy, 1.4, 0.25, 0.8, PLANT, rot=[60, 45 + 90 * k, 0]) for k, (dx, dy) in enumerate(((1, 0), (0, 1), (-1, 0), (0, -1)))], collision=False, styles=["neon", "rich", "clean", "corporate"])
item("plant_dead", "Vertrocknete Pflanze", "plant", [0.4, 0.4], [cyl(0, 0, 0.18, 0.35, 0.35, 0.35, POT), cone(0, 0, 0.55, 0.3, 0.45, DPLANT)], collision=False, styles=["poor", "dirty", "industrial"])
item("plant_hanging", "Haengepflanze", "plant", [0.4, 0.4], [cyl(0, 0, -0.4, 0.01, 0.01, 0.8, BLACK), cyl(0, 0, -0.85, 0.3, 0.3, 0.2, POT), sph(0, 0, -0.95, 0.45, PLANT)], mount="ceiling", collision=False, styles=["warm", "rich", "clean", "worker"])
item("plant_ficus", "Ficus", "plant", [0.6, 0.6], [box(0, 0, 0.25, 0.5, 0.5, 0.5, "#3a3a3a"), cyl(0, 0, 0.9, 0.06, 0.06, 1.0, "#6a5030"), sph(0, 0, 1.5, 0.9, PLANT)], collision=False, styles=["corporate", "clean", "rich"])
item("herb_box", "Kraeuterkasten", "plant", [0.25, 0.8], [box(0, 0, 0.1, 0.25, 0.8, 0.2, "#7a5a3a"), box(0, 0, 0.25, 0.2, 0.75, 0.15, PLANT)], mount="surface", collision=False)
# Muell und Kleinkram
item("trash_bag", "Muellsack", "trash", [0.6, 0.6], [sph(0, 0, 0.3, 0.6, TRASH), sph(0.1, 0.1, 0.62, 0.15, TRASH)], collision=False, tags=["trash"])
item("trash_pile", "Muellhaufen", "trash", [1.0, 1.0], [sph(0, 0, 0.15, 0.9, "#3a3528"), box(0.2, -0.1, 0.3, 0.3, 0.4, 0.06, "#c8c0a0"), cyl(-0.2, 0.2, 0.3, 0.07, 0.07, 0.13, "#a02020", rot=[90, 0, 0])], collision=False, styles=["poor", "dirty", "industrial"], tags=["trash"])
item("cans", "Getraenkedosen", "trash", [0.4, 0.4], [cyl(0.1, 0, 0.06, 0.07, 0.07, 0.12, NRED), cyl(-0.1, 0.1, 0.035, 0.07, 0.07, 0.12, "#20a050", rot=[90, 0, 0]), cyl(0, -0.15, 0.06, 0.07, 0.07, 0.12, METAL)], collision=False, tags=["trash"])
item("bottles", "Flaschen", "trash", [0.3, 0.3], [cyl(0, 0, 0.13, 0.08, 0.08, 0.26, "#2a6a2a"), cyl(0.1, 0.05, 0.13, 0.08, 0.08, 0.26, "#6a4a1a"), cyl(-0.1, 0, 0.04, 0.08, 0.08, 0.26, "#2a6a2a", rot=[90, 0, 0])], collision=False, tags=["trash"])
item("pizza_boxes", "Pizzakartons", "trash", [0.4, 0.4], [box(0, 0, 0.025 + 0.05 * i, 0.4, 0.4, 0.045, CARD, rot=[0, 10 * i, 0]) for i in range(3)], collision=False, tags=["trash"])
item("newspapers", "Zeitungsstapel", "trash", [0.4, 0.3], [box(0, 0, 0.1, 0.4, 0.3, 0.2, "#d8d4c4")], collision=False, tags=["trash"])
item("cigarette_butts", "Kippen und Aschenbecher", "trash", [0.2, 0.2], [cyl(0, 0, 0.02, 0.15, 0.15, 0.04, "#606060"), box(0.03, 0, 0.05, 0.05, 0.01, 0.01, OFFW)], mount="surface", collision=False, tags=["trash"])
item("syringes", "Spritzenabfall", "trash", [0.3, 0.3], [cyl(0, 0, 0.01, 0.015, 0.015, 0.12, "#e0e8f0", rot=[90, 30, 0]), cyl(0.05, 0.05, 0.01, 0.015, 0.015, 0.12, "#e0e8f0", rot=[90, -40, 0])], collision=False, styles=["poor", "dirty"], tags=["trash"])
item("puddle", "Pfuetze", "trash", [1.0, 1.4], [cyl(0, 0, 0.003, 1.0, 1.4, 0.005, "#202830", 0.05)], collision=False, styles=["poor", "dirty", "industrial"], tags=["trash"])
item("tire_stack", "Reifenstapel", "clutter", [0.7, 0.7], [cyl(0, 0, 0.12 + 0.24 * i, 0.68, 0.68, 0.22, "#1a1a1a") for i in range(3)], styles=["industrial", "poor", "worker"])
item("engine_block", "Motorblock", "clutter", [0.8, 0.6], [box(0, 0, 0.3, 0.8, 0.6, 0.6, "#5a5a55"), cyl(0, 0, 0.65, 0.3, 0.3, 0.12, DMETAL)], styles=["industrial", "worker"])
item("car_lift", "Hebebuehne", "work", [2.4, 4.4], [box(0, -1.15, 1.0, 0.3, 0.3, 2.0, "#c83020"), box(0, 1.15, 1.0, 0.3, 0.3, 2.0, "#c83020"), box(-0.6, 0, 0.1, 0.4, 2.2, 0.15, DMETAL), box(0.6, 0, 0.1, 0.4, 2.2, 0.15, DMETAL)], styles=["industrial", "worker"], activity="repair")
item("car_wreck", "Autowrack", "clutter", [2.0, 4.2], [box(0, 0, 0.55, 1.8, 4.2, 0.7, "#6a3a2a"), box(0, -0.3, 1.15, 1.6, 2.0, 0.5, "#5a3020"), box(0, -0.3, 1.15, 1.62, 1.8, 0.35, "#2a3a40")] + [cyl(sx * 0.85, sy * 1.35, 0.33, 0.66, 0.66, 0.25, "#151515", rot=[0, 0, 90]) for sx in (-1, 1) for sy in (-1, 1)], styles=["industrial", "poor", "worker"])
item("compressor", "Kompressor", "work", [0.6, 1.0], [cyl(0, 0, 0.35, 0.5, 0.5, 0.9, "#c83020", rot=[90, 0, 0]), box(0, 0, 0.75, 0.3, 0.4, 0.3, DMETAL)])
item("welding_rig", "Schweissgeraet", "work", [0.6, 0.6], [box(0, 0, 0.4, 0.5, 0.5, 0.8, "#2a5a2a"), cyl(0.3, 0.2, 0.9, 0.05, 0.05, 0.4, BLACK)], light=lamp(0.9, "#a0c8ff", 200, 200, 0.4), activity="repair")
item("toolbox_chest", "Werkzeugwagen", "work", [0.5, 0.9], [box(0, 0, 0.5, 0.5, 0.9, 1.0, NRED)] + [box(0.26, 0, 0.2 + i * 0.18, 0.01, 0.8, 0.02, METAL) for i in range(5)])
item("forklift", "Gabelstapler", "clutter", [1.2, 2.4], [box(0, 0.2, 0.7, 1.1, 1.6, 0.9, "#e0b020"), box(0, -0.9, 1.5, 0.1, 0.1, 2.6, DMETAL), box(-0.4, -1.1, 0.1, 0.1, 0.9, 0.05, DMETAL), box(0.4, -1.1, 0.1, 0.1, 0.9, 0.05, DMETAL), box(0, 0.3, 2.1, 1.1, 1.2, 0.05, DMETAL)], styles=["industrial", "worker"])
item("container_small", "Kleincontainer", "clutter", [2.4, 3.0], [box(0, 0, 1.2, 2.4, 3.0, 2.4, "#2a6a8a"), box(1.21, 0, 1.2, 0.02, 2.8, 2.2, "#24607e")], styles=["industrial", "worker"])
item("iv_stand", "Infusionsstaender", "medical", [0.4, 0.4], [cyl(0, 0, 0.9, 0.03, 0.03, 1.8, STEEL), box(0, 0, 1.7, 0.1, 0.05, 0.2, "#c8e0f0", 0.3), cyl(0, 0, 0.03, 0.4, 0.4, 0.05, STEEL)], collision=False)
item("screen_divider", "Paravent", "medical", [0.1, 1.8], [box(0, -0.6, 0.9, 0.04, 0.58, 1.7, "#d8e8d8"), box(0, 0, 0.9, 0.04, 0.58, 1.7, "#d8e8d8"), box(0, 0.6, 0.9, 0.04, 0.58, 1.7, "#d8e8d8")])
item("cooler_box", "Kuehlbox (Organe?)", "medical", [0.4, 0.6], [box(0, 0, 0.2, 0.4, 0.6, 0.4, "#2a6ac8"), box(0, 0, 0.41, 0.42, 0.62, 0.04, WHITE)], collision=False, styles=["poor", "dirty", "industrial", "worker"])
item("dental_chair", "Behandlungsstuhl", "medical", [0.8, 1.8], [box(0, 0, 0.55, 0.6, 1.2, 0.15, "#5aa0a0"), box(0, -0.7, 0.9, 0.6, 0.3, 0.7, "#5aa0a0", rot=[20, 0, 0]), cyl(0, 0, 0.25, 0.25, 0.25, 0.5, STEEL)], activity="patient")
# Laden, Bar, Imbiss
item("shop_gondola", "Warenregal (Laden)", "shop", [0.9, 2.0], shelf_parts(2.0, 0.9, 1.6, 4, WHITE, ["#e03030", "#30a0e0", "#f0c020", "#30b050"]), activity="browse")
item("display_fridge", "Kuehlregal", "shop", [0.8, 1.8], [box(0, 0, 1.0, 0.8, 1.8, 2.0, "#dfe7ea"), box(0.41, 0, 1.0, 0.02, 1.7, 1.8, "#bfeaff", 1.8)] + [box(0.1, 0, 0.4 + i * 0.4, 0.5, 1.6, 0.25, ["#e03030", "#f0f0f0", "#30a0e0", "#f0c020"][i]) for i in range(4)], light=lamp(1.0, "#d8f4ff", 300, 300, 0.6))
item("shop_counter", "Ladentheke", "shop", [0.8, 2.2], [box(0, 0, 0.5, 0.7, 2.2, 1.0, LWOOD), box(0, 0, 1.02, 0.8, 2.3, 0.04, "#3a3a3a"), box(0.36, 0, 0.5, 0.01, 2.0, 0.6, GLASS, 0.2)], activity="shopkeeper")
item("magazine_rack", "Zeitschriftenstaender", "shop", [0.4, 1.0], [box(0, 0, 0.75, 0.3, 1.0, 1.5, DMETAL)] + [box(0.16, -0.3 + 0.3 * k, 0.4 + 0.35 * j, 0.02, 0.25, 0.3, ["#e04040", "#4080e0", "#e0c040"][(k + j) % 3]) for k in range(3) for j in range(3)])
item("clothes_rack", "Kleiderstange", "shop", [0.6, 1.6], [cyl(0, -0.75, 0.75, 0.04, 0.04, 1.5, METAL), cyl(0, 0.75, 0.75, 0.04, 0.04, 1.5, METAL), cyl(0, 0, 1.5, 0.04, 0.04, 1.5, METAL, rot=[90, 0, 0])] + [box(0, -0.6 + 0.2 * i, 1.05, 0.45, 0.06, 0.8, [FABR, FABB, FABG, FABO][i % 4]) for i in range(7)], styles=["rich", "neon", "clean", "warm"], activity="browse")
item("souvenir_stand", "Souvenirstaender", "shop", [0.6, 0.6], [cyl(0, 0, 0.8, 0.08, 0.08, 1.6, METAL)] + [box(0.12 * (i % 2 * 2 - 1), 0, 0.5 + 0.25 * i, 0.15, 0.25, 0.2, [NPINK, NCYAN, "#f0e020"][i % 3]) for i in range(4)], styles=["neon", "clean"], activity="browse")
item("bar_counter", "Bartresen", "bar", [0.8, 3.0], [box(0, 0, 0.55, 0.7, 3.0, 1.1, DWOOD), box(0.05, 0, 1.12, 0.8, 3.1, 0.05, "#2a1a10"), box(0.36, 0, 0.15, 0.02, 2.9, 0.06, NAMBER, 3.0), cyl(-0.2, -0.8, 1.3, 0.06, 0.06, 0.35, METAL), cyl(-0.2, -0.6, 1.3, 0.06, 0.06, 0.35, METAL)], activity="bartender")
item("bottle_shelf", "Flaschenregal", "bar", [0.3, 2.0], [box(0, 0, 1.4 + 0.45 * j, 0.3, 2.0, 0.03, DWOOD) for j in range(3)] + [cyl(0, -0.9 + 0.2 * i, 1.55 + 0.45 * j, 0.07, 0.07, 0.28, ["#2a6a2a", "#8a5a1a", "#d0d0e0", "#6a1a2a"][(i + j) % 4], 0.4) for i in range(10) for j in range(3)], mount="wall", collision=False)
item("beer_taps", "Zapfanlage", "bar", [0.3, 0.6], [box(0, 0, 0.15, 0.2, 0.6, 0.3, METAL)] + [cyl(0.05, -0.2 + 0.2 * i, 0.38, 0.03, 0.03, 0.18, [NAMBER, WHITE, BLACK][i]) for i in range(3)], mount="surface", collision=False)
item("dancefloor_tile", "Leuchtboden", "bar", [2.0, 2.0], [box(sx * 0.5, sy * 0.5, 0.01, 0.96, 0.96, 0.02, c, 4.0) for (sx, sy, c) in ((-1, -1, NPINK), (1, -1, NCYAN), (-1, 1, NVIOLET), (1, 1, NAMBER))], collision=False, styles=["neon", "worker", "warm"], light=lamp(0.3, "#a35bff", 600, 500), activity="dance")
item("napkin_dispenser", "Serviettenspender", "diner", [0.15, 0.15], [box(0, 0, 0.07, 0.12, 0.15, 0.14, METAL)], mount="surface", collision=False)
item("condiments", "Ketchup und Senf", "diner", [0.15, 0.15], [cyl(-0.04, 0, 0.08, 0.06, 0.06, 0.16, NRED), cyl(0.04, 0, 0.08, 0.06, 0.06, 0.16, "#e0c020")], mount="surface", collision=False)
item("food_plate", "Teller mit Essen", "diner", [0.3, 0.3], [cyl(0, 0, 0.01, 0.28, 0.28, 0.02, WHITE), sph(0, 0, 0.04, 0.14, "#b0702a")], mount="surface", collision=False)
item("noodle_bowl", "Nudelschale", "diner", [0.2, 0.2], [cyl(0, 0, 0.04, 0.18, 0.18, 0.08, NRED), cyl(0, 0, 0.08, 0.15, 0.15, 0.01, "#e8d08a")], mount="surface", collision=False)
# Kirche/Halle, Polizei
item("altar", "Altar", "hall", [1.0, 2.2], [box(0, 0, 0.5, 1.0, 2.2, 1.0, OFFW), box(0, 0, 1.02, 1.1, 2.3, 0.05, "#c8a050")], activity="preach")
item("lectern", "Rednerpult", "hall", [0.5, 0.6], [box(0, 0, 0.55, 0.4, 0.5, 1.1, DWOOD), box(0.05, 0, 1.15, 0.5, 0.6, 0.06, DWOOD, rot=[-15, 0, 0])], activity="preach")
item("long_table", "Lange Tafel", "hall", [1.0, 4.0], table_parts(1.0, 4.0, 0.75, LWOOD, DWOOD), activity="eat")
item("soup_pot", "Suppenkessel", "hall", [0.5, 0.5], [cyl(0, 0, 0.2, 0.5, 0.5, 0.4, STEEL)], mount="surface", collision=False)
item("cell_bars", "Zellengitter", "police", [0.1, 3.0], [box(0, -1.45 + i * 0.15, 1.2, 0.04, 0.04, 2.4, DMETAL) for i in range(20)] + [box(0, 0, 2.38, 0.08, 3.0, 0.06, DMETAL)], mount="floor")
item("booking_desk", "Aufnahmetresen", "police", [0.9, 2.4], [box(0, 0, 0.6, 0.8, 2.4, 1.2, "#2a3a5a"), box(0.05, 0, 1.22, 0.9, 2.5, 0.05, "#1a2a40"), box(-0.1, 0, 1.8, 0.04, 2.4, 1.0, GLASS)], activity="reception")
item("weapon_rack_empty", "Leerer Waffenschrank", "police", [0.4, 1.2], [box(0, 0, 1.0, 0.4, 1.2, 2.0, "#1a1a1a"), box(0.21, 0, 1.0, 0.02, 1.1, 1.9, GLASS)], styles=["clean", "corporate", "worker", "industrial"])
item("metal_detector", "Sicherheitsschleuse", "police", [0.8, 1.0], [box(0, -0.48, 1.05, 0.6, 0.12, 2.1, "#3a3f48"), box(0, 0.48, 1.05, 0.6, 0.12, 2.1, "#3a3f48"), box(0, 0, 2.15, 0.6, 1.08, 0.12, "#3a3f48"), box(0.31, 0.48, 1.8, 0.01, 0.06, 0.06, NGREEN, 5.0)], collision=False, styles=["corporate", "clean", "rich"])
# Wohnen Kleinkram
item("rug_round", "Runder Teppich", "decor", [1.8, 1.8], [cyl(0, 0, 0.005, 1.8, 1.8, 0.01, "#8a3a3a")], collision=False, styles=["warm", "rich", "worker"])
item("rug_rect", "Teppich", "decor", [1.6, 2.4], [box(0, 0, 0.005, 1.6, 2.4, 0.01, "#3a4a6a")], collision=False)
item("rug_worn", "Abgetretener Laeufer", "decor", [0.8, 2.0], [box(0, 0, 0.005, 0.8, 2.0, 0.01, "#5a4a3a")], collision=False, styles=["poor", "worker", "dirty"])
item("coat_rack", "Garderobe", "decor", [0.4, 0.4], [cyl(0, 0, 0.9, 0.05, 0.05, 1.8, DWOOD), cyl(0, 0, 0.02, 0.4, 0.4, 0.04, DWOOD), box(0.1, 0, 1.4, 0.15, 0.3, 0.6, FABB)], collision=False)
item("laundry_rack", "Waeschestaender", "decor", [0.6, 1.4], legs4(0.6, 1.4, 1.0, METAL, 0.02) + [box(0, -0.3, 0.85, 0.02, 0.4, 0.5, OFFW), box(0, 0.25, 0.85, 0.02, 0.3, 0.4, FABB)], collision=False, styles=["poor", "worker", "warm"])
item("punching_bag", "Boxsack", "decor", [0.6, 0.6], [cyl(0, 0, -0.3, 0.02, 0.02, 0.6, METAL), cyl(0, 0, -1.1, 0.4, 0.4, 1.0, "#5a1a1a")], mount="ceiling", collision=False, activity="train")
item("weight_bench", "Hantelbank", "decor", [0.6, 1.4], [box(0, 0, 0.45, 0.35, 1.2, 0.1, BLACK), cyl(0, -0.5, 1.0, 0.03, 0.03, 1.6, METAL, rot=[0, 0, 90]), cyl(-0.75, -0.5, 1.0, 0.4, 0.4, 0.06, "#222", rot=[0, 0, 90]), cyl(0.75, -0.5, 1.0, 0.4, 0.4, 0.06, "#222", rot=[0, 0, 90])] + legs4(0.35, 1.2, 0.42, DMETAL, 0.04), activity="train")
item("guitar", "Gitarre", "decor", [0.3, 0.4], [box(0, 0, 0.35, 0.08, 0.35, 0.45, "#a0602a"), box(0, 0, 0.85, 0.04, 0.06, 0.6, DWOOD)], collision=False, styles=["warm", "worker", "poor", "neon"], activity="play_music")
item("record_player", "Plattenspieler", "decor", [0.4, 0.45], [box(0, 0, 0.05, 0.4, 0.45, 0.1, DWOOD), cyl(0, 0, 0.11, 0.3, 0.3, 0.01, BLACK)], mount="surface", collision=False, styles=["warm", "rich", "neon"])
item("aquarium", "Aquarium", "decor", [0.45, 1.2], [box(0, 0, 0.4, 0.45, 1.2, 0.8, DWOOD), box(0, 0, 1.05, 0.45, 1.2, 0.5, "#2a8aa8", 1.2)], styles=["rich", "neon", "corporate", "warm"], light=lamp(1.1, "#3ac8e8", 200, 250))
item("cat_bowl", "Katzennapf", "decor", [0.2, 0.2], [cyl(0, 0, 0.03, 0.18, 0.18, 0.06, "#c03030")], collision=False)
item("candles_table", "Kerzen", "decor", [0.2, 0.2], [cyl(0, 0, 0.06, 0.05, 0.05, 0.12, OFFW), cone(0, 0, 0.15, 0.03, 0.05, "#ffb040", 10.0)], mount="surface", collision=False, styles=["warm", "rich", "poor"])
item("ashtray_full", "Voller Aschenbecher", "decor", [0.15, 0.15], [cyl(0, 0, 0.02, 0.14, 0.14, 0.04, "#505050"), sph(0, 0, 0.04, 0.08, "#808080")], mount="surface", collision=False, styles=["poor", "worker", "dirty", "industrial"])
item("phone_old", "Altes Telefon", "decor", [0.2, 0.25], [box(0, 0, 0.05, 0.2, 0.25, 0.1, "#2a2a2a"), box(0, 0, 0.12, 0.06, 0.22, 0.04, "#2a2a2a")], mount="surface", collision=False)
item("map_wall", "Stadtplan mit Markierungen", "decor", [0.03, 1.8], [box(0, 0, 1.6, 0.02, 1.8, 1.2, "#d8d0b0"), box(0.012, -0.3, 1.7, 0.005, 0.08, 0.08, NRED), box(0.012, 0.4, 1.45, 0.005, 0.08, 0.08, NRED)], mount="wall", collision=False, activity="plan")
item("go_bag", "Fluchtrucksack", "decor", [0.4, 0.3], [box(0, 0, 0.25, 0.3, 0.4, 0.5, "#3a4a2a")], collision=False)
item("sleeping_bag", "Schlafsack", "bed", [1.9, 0.8], [box(0, 0, 0.06, 0.8, 1.9, 0.12, "#3a5a7a")], collision=False, styles=["poor", "dirty", "industrial"], activity="sleep")
item("ceiling_fan", "Deckenventilator", "decor", [1.2, 1.2], [cyl(0, 0, -0.2, 0.04, 0.04, 0.4, METAL), cyl(0, 0, -0.42, 0.2, 0.2, 0.08, METAL), box(0, 0, -0.45, 1.2, 0.12, 0.01, DWOOD), box(0, 0, -0.45, 0.12, 1.2, 0.01, DWOOD)], mount="ceiling", collision=False, styles=["warm", "neon", "worker", "poor"])
item("light_switch", "Lichtschalter", "tech", [0.05, 0.1], [box(0, 0, 1.2, 0.02, 0.08, 0.12, OFFW)], mount="wall", collision=False, interactive="switch")
item("fuse_box", "Sicherungskasten", "tech", [0.15, 0.5], [box(0, 0, 1.5, 0.12, 0.5, 0.6, "#7a7a70"), box(0.065, 0.15, 1.6, 0.01, 0.05, 0.05, NAMBER, 3.0)], mount="wall", collision=False, interactive="switch")
item("vent_grate", "Lueftungsgitter", "decor", [0.05, 0.6], [box(0, 0, 0.3, 0.03, 0.6, 0.4, "#5a5a5a")], mount="wall", collision=False)

# ---------------------------------------------------------------------------
# Wohnstile (je Bezirk ueber districtStyles in interiors.json gewaehlt)
# ---------------------------------------------------------------------------
STYLES = [
    {"id": "corporate", "label": "Konzernglanz (Kestrel Core)", "tags": ["corporate", "clean", "rich"], "wear": 0.05, "clutter": 0.6,
     "trash": [0, 1], "plants": 0.6, "neon": 0.1, "flicker": 0.0, "wall": "#dfe3e8", "floor": "#9aa3ad", "ceiling": "#f2f4f6",
     "lightColor": "#eef4ff", "lightIntensity": 1600, "lightsPer10m2": 1.2},
    {"id": "worker", "label": "Arbeiterwohnung (Saltmarket)", "tags": ["worker", "warm"], "wear": 0.45, "clutter": 1.1,
     "trash": [1, 4], "plants": 0.3, "neon": 0.15, "flicker": 0.08, "wall": "#b8a888", "floor": "#6a5440", "ceiling": "#d0c8b8",
     "lightColor": "#ffd9a8", "lightIntensity": 1000, "lightsPer10m2": 0.7},
    {"id": "bohemian", "label": "Altstadt, warm und politisch (Lantern Hill)", "tags": ["warm", "worker", "neon"], "wear": 0.35, "clutter": 1.3,
     "trash": [0, 3], "plants": 0.7, "neon": 0.35, "flicker": 0.05, "wall": "#c89a62", "floor": "#7a4a2a", "ceiling": "#e0c8a0",
     "lightColor": "#ffc890", "lightIntensity": 900, "lightsPer10m2": 0.8},
    {"id": "suburban", "label": "Gated Villa (Greenmoor)", "tags": ["rich", "clean", "warm"], "wear": 0.05, "clutter": 0.8,
     "trash": [0, 0], "plants": 0.8, "neon": 0.0, "flicker": 0.0, "wall": "#e8e4d8", "floor": "#b8946a", "ceiling": "#f6f4ee",
     "lightColor": "#fff0dc", "lightIntensity": 1300, "lightsPer10m2": 1.0},
    {"id": "industrial", "label": "Schrott und Natriumlicht (Rustline)", "tags": ["industrial", "poor", "dirty", "worker"], "wear": 0.75, "clutter": 1.4,
     "trash": [3, 9], "plants": 0.05, "neon": 0.2, "flicker": 0.25, "wall": "#7a7468", "floor": "#4a4640", "ceiling": "#6a665e",
     "lightColor": "#ffb070", "lightIntensity": 1100, "lightsPer10m2": 0.6},
    {"id": "slum", "label": "Elendsquartier", "tags": ["poor", "dirty"], "wear": 0.9, "clutter": 1.6,
     "trash": [5, 12], "plants": 0.05, "neon": 0.05, "flicker": 0.35, "wall": "#6e675a", "floor": "#463c32", "ceiling": "#5e584e",
     "lightColor": "#ffbf80", "lightIntensity": 700, "lightsPer10m2": 0.4},
    {"id": "neon_tourist", "label": "Neon-Fassade (Sunstrip)", "tags": ["neon", "clean", "rich"], "wear": 0.2, "clutter": 1.0,
     "trash": [0, 3], "plants": 0.6, "neon": 0.8, "flicker": 0.05, "wall": "#f0d8e0", "floor": "#3a8a90", "ceiling": "#f4ecf0",
     "lightColor": "#ffe0f0", "lightIntensity": 1100, "lightsPer10m2": 0.9},
]

# ---------------------------------------------------------------------------
# Fuellregeln je Raumart (place: wall|corner|center|scatter|wallDecor|ceiling|surface)
# ---------------------------------------------------------------------------
def R(pick, place, n=(1, 1), chance=1.0, clutter=False):
    d = {"pick": pick if isinstance(pick, list) else [pick], "place": place, "count": list(n)}
    if chance < 1.0: d["chance"] = chance
    if clutter: d["clutter"] = True
    return d

TRASH_ALL = ["trash_bag", "trash_pile", "cans", "bottles", "pizza_boxes", "newspapers", "syringes", "puddle"]
POSTERS = ["poster_band", "poster_propaganda", "poster_resistance", "poster_movie", "poster_pinup_car", "poster_union", "poster_beach", "painting_oil", "art_abstract", "graffiti_panel"]
PLANTS = ["plant_potted", "plant_palm", "plant_dead", "plant_ficus"]
NEONS = ["neon_open", "neon_bar", "neon_arrow", "neon_heart", "neon_cocktail", "neon_kestrel", "neon_strip"]
CEIL = ["lamp_pendant", "bulb_bare", "lamp_fluorescent", "lamp_led_panel"]

ROOM_KINDS = {
    "living": [R(["sofa_leather", "sofa_fabric", "sofa_worn"], "wall"), R(["tv_stand", "tv_crt"], "wall"), R(["table_coffee", "table_crate"], "center"),
               R(["armchair", "beanbag"], "corner", (0, 1)), R(["bookshelf", "shelf_wood"], "wall", (0, 1)), R(["rug_round", "rug_rect", "rug_worn"], "center", (0, 1), 0.7),
               R(["lamp_floor"], "corner", (0, 1), 0.7), R(POSTERS, "wallDecor", (1, 3)), R(PLANTS, "corner", (0, 2), clutter=True), R(["ceiling_fan", "string_lights", "plant_hanging"], "ceiling", (0, 1), 0.5),
               R(["light_switch"], "wallDecor", (1, 1)), R(["radiator", "ac_unit", "clock_wall"], "wallDecor", (0, 2)), R(["guitar", "laundry_rack", "aquarium", "record_player"], "corner", (0, 1), 0.6, True)],
    "bedroom": [R(["bed_luxury", "bed_double", "bed_single", "mattress_floor"], "wall"), R(["wardrobe"], "wall"), R(["desk_wood", "shelf_wood"], "wall", (0, 1), 0.6),
                R(["chair_wood"], "scatter", (0, 1), 0.5), R(["rug_round", "rug_worn"], "center", (0, 1), 0.5), R(POSTERS, "wallDecor", (0, 2)), R(["mirror", "curtain"], "wallDecor", (0, 1)),
                R(["lamp_floor", "plant_potted"], "corner", (0, 1), 0.6, True), R(["light_switch"], "wallDecor")],
    "kitchen": [R(["kitchen_counter", "kitchenette"], "wall"), R(["fridge"], "wall"), R(["stove"], "wall", (0, 1), 0.6), R(["table_kitchen", "table_cafe"], "center"),
                R(["chair_wood", "chair_plastic"], "scatter", (1, 3)), R(["clock_wall", "herb_box", "radiator"], "wallDecor", (0, 1)), R(["microwave", "radio", "herb_box"], "surface", (1, 2)),
                R(["light_switch"], "wallDecor")],
    "bath": [R(["toilet", "toilet_dirty"], "wall"), R(["sink_bath"], "wall"), R(["shower", "bathtub"], "corner"), R(["washing_machine"], "wall", (0, 1), 0.4), R(["mirror"], "wallDecor", (0, 1))],
    "hall": [R(["coat_rack"], "corner", (0, 1), 0.7), R(["shelf_wood", "filing_cabinet"], "wall", (0, 1), 0.4), R(POSTERS, "wallDecor", (0, 2)), R(["light_switch"], "wallDecor"), R(["plant_potted", "plant_dead"], "corner", (0, 1), 0.5, True)],
    "stair": [R(["emergency_light", "fire_extinguisher"], "wallDecor", (1, 2)), R(["graffiti_panel", "poster_wanted"], "wallDecor", (0, 1), 0.5)],
    "slum": [R(["mattress_floor", "bunk_bed", "sleeping_bag", "cot_army"], "wall", (1, 2)), R(["table_crate", "chair_plastic"], "scatter", (1, 2)), R(["kitchenette", "barrel_fire"], "wall", (0, 1), 0.6),
             R(["tv_crt", "radio"], "corner", (0, 1), 0.6), R(["graffiti_panel", "poster_resistance", "poster_band"], "wallDecor", (1, 2)), R(["bulb_bare"], "ceiling", (1, 1)),
             R(["laundry_rack", "cardboard_boxes", "plant_dead"], "corner", (1, 2), clutter=True), R(["bars_window", "pipes_wall"], "wallDecor", (0, 1))],
    "shopfloor": [R(["shop_gondola", "clothes_rack", "souvenir_stand"], "center", (2, 4)), R(["display_fridge", "shelf_metal"], "wall", (1, 3)), R(["magazine_rack", "vending_machine"], "wall", (0, 1)),
                  R(["aegis_camera"], "ceiling", (0, 1), 0.6), R(NEONS + ["neon_24h"], "wallDecor", (0, 2)), R(["poster_propaganda", "poster_movie", "poster_beach"], "wallDecor", (0, 1))],
    "counter": [R(["shop_counter"], "wall"), R(["cash_register"], "surface"), R(["shelf_metal"], "wall", (0, 1)), R(["neon_open", "neon_24h"], "wallDecor", (0, 1))],
    "storage": [R(["shelf_metal", "pallet_rack", "shelf_wood"], "wall", (1, 3)), R(["crate_wood", "crate_stack", "cardboard_boxes", "barrel_oil"], "scatter", (2, 5), clutter=True), R(["lamp_fluorescent", "bulb_bare"], "ceiling", (1, 1)), R(["fuse_box"], "wallDecor", (0, 1))],
    "bar": [R(["bar_counter"], "wall"), R(["bottle_shelf"], "wallDecor"), R(["stool_bar"], "scatter", (3, 6)), R(["table_cafe", "booth_seat"], "scatter", (1, 3)), R(["beer_taps"], "surface"),
            R(["jukebox", "table_pool", "arcade_cabinet"], "wall", (0, 2)), R(NEONS, "wallDecor", (1, 3)), R(["poster_band", "poster_movie"], "wallDecor", (0, 2)), R(["ceiling_fan", "string_lights"], "ceiling", (0, 2))],
    "dancefloor": [R(["dancefloor_tile"], "center", (1, 2)), R(["dj_booth"], "wall"), R(["speaker_stack"], "corner", (2, 2)), R(["neon_strip", "neon_heart", "neon_kestrel"], "wallDecor", (1, 3)), R(["string_lights"], "ceiling", (0, 2))],
    "lounge": [R(["sofa_leather", "sofa_fabric", "booth_seat"], "wall", (1, 2)), R(["table_coffee"], "center"), R(["armchair", "beanbag"], "scatter", (1, 2)), R(PLANTS, "corner", (0, 2)), R(NEONS + ["painting_oil", "art_abstract"], "wallDecor", (1, 2))],
    "office": [R(["desk_office", "desk_wood", "desk_executive"], "wall", (1, 2)), R(["chair_office"], "scatter", (1, 2)), R(["filing_cabinet"], "wall", (0, 2)), R(["whiteboard", "corkboard", "poster_propaganda"], "wallDecor", (0, 1)),
               R(["plant_ficus", "plant_potted", "water_cooler"], "corner", (0, 1)), R(["laptop", "phone_old", "lamp_desk"], "surface", (0, 2)), R(["light_switch"], "wallDecor")],
    "openoffice": [R(["desk_office"], "center", (2, 6)), R(["chair_office"], "scatter", (2, 5)), R(["cubicle_wall"], "center", (0, 3)), R(["copier", "water_cooler", "filing_cabinet"], "wall", (1, 3)), R(["aegis_camera"], "ceiling", (0, 1))],
    "lobby": [R(["reception_desk"], "wall"), R(["lobby_sofa"], "wall", (1, 2)), R(["plant_ficus", "plant_palm"], "corner", (2, 4)), R(["corporate_logo", "art_abstract"], "wallDecor", (1, 1)), R(["metal_detector"], "center", (0, 1), 0.5),
              R(["aegis_camera"], "ceiling", (1, 2)), R(["terminal_public"], "wall", (0, 1)), R(["water_cooler"], "corner", (0, 1))],
    "meeting": [R(["table_glass", "table_dining"], "center"), R(["chair_office"], "scatter", (4, 8)), R(["tv_flat", "whiteboard"], "wallDecor"), R(["plant_ficus"], "corner", (0, 1))],
    "waiting": [R(["bench_wood", "chair_plastic", "folding_chair"], "wall", (2, 4)), R(["magazine_rack", "water_cooler", "vending_machine"], "wall", (0, 1)), R(["poster_wanted", "poster_propaganda", "clock_wall"], "wallDecor", (1, 2)), R(PLANTS, "corner", (0, 1))],
    "surgery": [R(["table_operating", "dental_chair"], "center"), R(["med_lamp"], "ceiling"), R(["medical_cabinet"], "wall", (1, 2)), R(["iv_stand", "cooler_box"], "scatter", (1, 2)), R(["sink_kitchen"], "wall", (0, 1)), R(["screen_divider"], "wall", (0, 1))],
    "ward": [R(["hospital_bed", "cot_army"], "wall", (2, 4)), R(["iv_stand"], "scatter", (1, 3)), R(["screen_divider"], "scatter", (0, 2)), R(["medical_cabinet"], "wall", (0, 1)), R(["lamp_fluorescent"], "ceiling")],
    "workshop": [R(["workbench"], "wall", (1, 2)), R(["tool_wall"], "wallDecor"), R(["toolbox_chest", "compressor", "welding_rig"], "wall", (1, 3)), R(["engine_block", "tire_stack", "barrel_oil"], "scatter", (1, 4), clutter=True),
                 R(["poster_pinup_car", "poster_union"], "wallDecor", (0, 1)), R(["lamp_fluorescent", "bulb_bare"], "ceiling", (1, 2)), R(["radio"], "surface", (0, 1))],
    "garage": [R(["car_lift"], "center", (0, 1), 0.7), R(["car_wreck"], "center", (0, 1), 0.5), R(["tool_wall"], "wallDecor", (1, 2)), R(["toolbox_chest", "workbench", "tire_stack"], "wall", (2, 4)),
               R(["barrel_oil", "engine_block", "puddle"], "scatter", (1, 4), clutter=True), R(["lamp_fluorescent"], "ceiling", (2, 3)), R(["poster_pinup_car"], "wallDecor", (0, 1))],
    "warehouse": [R(["pallet_rack"], "wall", (2, 5)), R(["crate_stack", "pallet", "container_small", "barrel_oil"], "scatter", (3, 7), clutter=True), R(["forklift"], "scatter", (0, 1)),
                  R(["lamp_fluorescent", "bulb_bare"], "ceiling", (2, 4)), R(["fire_extinguisher", "emergency_light"], "wallDecor", (1, 2))],
    "server": [R(["server_rack"], "wall", (2, 5)), R(["cable_bundle"], "scatter", (1, 3)), R(["terminal_desk"], "wall", (0, 1)), R(["ac_unit"], "wallDecor")],
    "lab": [R(["multi_monitor", "soldering_desk"], "wall", (1, 3)), R(["chair_office"], "scatter", (1, 3)), R(["printer_3d", "shelf_metal"], "wall", (1, 2)), R(["drone_parts", "laptop"], "surface", (1, 2)),
            R(["cable_bundle"], "scatter", (0, 2)), R(["beanbag", "sofa_worn"], "corner", (0, 1)), R(["neon_strip", "poster_resistance", "graffiti_panel"], "wallDecor", (1, 3)), R(["string_lights", "lamp_fluorescent"], "ceiling", (1, 2))],
    "sanctuary": [R(["pew"], "center", (4, 8)), R(["altar"], "wall"), R(["lectern"], "scatter"), R(["candle_stand"], "corner", (2, 4)), R(["altar_cross", "neon_cross"], "wallDecor"), R(["lamp_pendant"], "ceiling", (2, 4))],
    "community": [R(["long_table"], "center", (1, 2)), R(["folding_chair"], "scatter", (4, 10)), R(["banner_union", "poster_union", "poster_resistance"], "wallDecor", (1, 3)), R(["soup_pot"], "surface", (0, 1)), R(["lectern"], "wall", (0, 1))],
    "dining": [R(["booth_seat"], "wall", (1, 3)), R(["table_cafe", "table_kitchen"], "scatter", (2, 4)), R(["chair_wood", "chair_plastic", "stool_bar"], "scatter", (2, 6)), R(["napkin_dispenser", "condiments", "food_plate", "noodle_bowl"], "surface", (1, 3)),
               R(["menu_board"], "wallDecor"), R(NEONS + ["neon_noodles"], "wallDecor", (0, 2)), R(["ceiling_fan", "lamp_pendant"], "ceiling", (0, 2))],
    "kitchen_pro": [R(["griddle", "fryer", "stove"], "wall", (2, 3)), R(["sink_kitchen"], "wall"), R(["fridge", "shelf_metal"], "wall", (1, 2)), R(["lamp_fluorescent"], "ceiling"), R(["trash_bag"], "corner", (0, 2), clutter=True)],
    "cell": [R(["cot_army"], "wall"), R(["toilet_dirty", "toilet"], "corner")],
    "booking": [R(["booking_desk"], "wall"), R(["police_terminal"], "wall", (1, 2)), R(["chair_office"], "scatter", (1, 2)), R(["poster_wanted"], "wallDecor", (1, 3)), R(["aegis_camera"], "ceiling", (1, 1))],
    "briefing": [R(["table_interview", "long_table"], "center"), R(["folding_chair", "chair_office"], "scatter", (3, 8)), R(["map_wall", "whiteboard", "corkboard"], "wallDecor", (1, 2)), R(["cctv_monitors"], "wall", (0, 1))],
    "evidence": [R(["evidence_locker", "weapon_rack_empty"], "wall", (2, 4)), R(["crate_wood", "cardboard_boxes"], "scatter", (1, 3)), R(["lamp_fluorescent"], "ceiling")],
    "safe": [R(["cot_army", "mattress_floor", "bed_single"], "wall"), R(["supply_crate", "go_bag"], "scatter", (1, 3)), R(["map_wall", "corkboard"], "wallDecor"), R(["terminal_desk", "multi_monitor", "cctv_monitors"], "wall", (0, 1)),
             R(["table_folding", "table_crate"], "center", (0, 1)), R(["bulb_bare", "lamp_fluorescent"], "ceiling"), R(["weight_bench", "punching_bag"], "corner", (0, 1), 0.5)],
    "locker": [R(["locker_row"], "wall", (1, 3)), R(["bench_wood"], "center", (0, 1)), R(["emergency_light"], "wallDecor")],
}

# ---------------------------------------------------------------------------
# Grundriss-Werkzeug
# ---------------------------------------------------------------------------
def snap(v):
    return round(v / G) * G

class Floor:
    def __init__(self, level, height=3.0):
        self.level, self.height = level, height
        self.rooms, self.doors, self.windows, self.stairs = [], [], [], []

    def room(self, rid, kind, x, y, w, d, **kw):
        r = {"id": rid, "kind": kind, "rect": [snap(x), snap(y), snap(w), snap(d)]}
        r.update(kw)
        self.rooms.append(r)
        return self

    def R(self, rid):
        return next(r for r in self.rooms if r["id"] == rid)["rect"]

    def door(self, a, b, t=0.5, width=1.0, **kw):
        """Tuer auf der gemeinsamen Wand zweier Raeume, t = Lage entlang der Ueberlappung."""
        ra, rb = self.R(a), self.R(b)
        ax0, ay0, ax1, ay1 = ra[0], ra[1], ra[0] + ra[2], ra[1] + ra[3]
        bx0, by0, bx1, by1 = rb[0], rb[1], rb[0] + rb[2], rb[1] + rb[3]
        if abs(ax1 - bx0) < 1e-6 or abs(bx1 - ax0) < 1e-6:
            x = ax1 if abs(ax1 - bx0) < 1e-6 else ax0
            lo, hi = max(ay0, by0), min(ay1, by1)
            c = max(lo + width / 2, min(hi - width / 2, snap(lo + t * (hi - lo))))
            at = [x, c]
        elif abs(ay1 - by0) < 1e-6 or abs(by1 - ay0) < 1e-6:
            y = ay1 if abs(ay1 - by0) < 1e-6 else ay0
            lo, hi = max(ax0, bx0), min(ax1, bx1)
            c = max(lo + width / 2, min(hi - width / 2, snap(lo + t * (hi - lo))))
            at = [c, y]
        else:
            raise ValueError("Raeume %s/%s grenzen nicht aneinander" % (a, b))
        if hi - lo < width:
            raise ValueError("Wand %s/%s zu kurz fuer Tuer" % (a, b))
        d = {"rooms": [a, b], "at": at, "width": width}
        d.update(kw)
        self.doors.append(d)
        return self

    def _side_point(self, a, side, t, width):
        r = self.R(a)
        x0, y0, x1, y1 = r[0], r[1], r[0] + r[2], r[1] + r[3]
        if side in ("S", "N"):
            c = max(x0 + width / 2, min(x1 - width / 2, snap(x0 + t * (x1 - x0))))
            return [c, y0 if side == "S" else y1]
        c = max(y0 + width / 2, min(y1 - width / 2, snap(y0 + t * (y1 - y0))))
        return [x0 if side == "W" else x1, c]

    def out(self, a, side, t=0.5, width=1.0, entrance=None, **kw):
        d = {"rooms": [a, "outside"], "at": self._side_point(a, side, t, width), "width": width}
        if entrance: d["entrance"] = entrance
        d.update(kw)
        self.doors.append(d)
        return self

    def win(self, a, side, t=0.5, width=1.0):
        self.windows.append({"room": a, "at": self._side_point(a, side, t, width), "width": width})
        return self

    def stair(self, room, x, y, w, d, axis="x"):
        self.stairs.append({"room": room, "rect": [snap(x), snap(y), snap(w), snap(d)], "toLevel": self.level + 1, "axis": axis})
        return self

    def to_json(self):
        d = {"level": self.level, "height": self.height, "rooms": self.rooms, "doors": self.doors, "windows": self.windows}
        if self.stairs: d["stairs"] = self.stairs
        return d

TEMPLATES = []

def template(tid, rtype, label, districts, w, d, floors, style=None, shop=None, access=None, props=None,
             secrets=None, npcs=None, interactives=None):
    t = {"id": tid, "roomType": rtype, "label": label, "districts": districts, "footprint": [w, d],
         "floors": [f.to_json() for f in floors]}
    if style: t["style"] = style
    if shop: t["shop"] = shop
    if access: t["access"] = access
    if props: t["props"] = props
    if interactives: t["interactives"] = interactives
    t["secrets"] = secrets or []
    t["npcSlots"] = npcs or []
    TEMPLATES.append(t)
    return t

def sec(sid, room, x, y, kind="collectible", floor=0, z=None, hidden=False, hint=None):
    s = {"id": sid, "floor": floor, "room": room, "at": [x, y] if z is None else [x, y, z], "kind": kind}
    if hidden: s["hidden"] = True
    if hint: s["hint"] = hint
    return s

def npc(room, x, y, activity, role="resident", hours=None, chance=0.8, floor=0, rot=0):
    n = {"floor": floor, "room": room, "at": [x, y], "rot": rot, "activity": activity, "role": role, "chance": chance}
    if hours: n["hours"] = hours
    return n

def mid(rect, fx=0.5, fy=0.5):
    return snap(rect[0] + rect[2] * fx), snap(rect[1] + rect[3] * fy)

# --- Layout-Funktionen je Raumtyp -------------------------------------------

def L_apartment_small(w, d, studio=False):
    f = Floor(0, 2.8)
    d1 = snap(d * 0.55)
    if studio:
        f.room("living", "living", 0, 0, w, d1).room("bed", "bedroom", 0, d1, snap(w * 0.6), d - d1).room("bath", "bath", snap(w * 0.6), d1, w - snap(w * 0.6), d - d1)
        f.door("living", "bed", 0.3).door("living", "bath", 0.5).out("living", "S", 0.3, entrance="front")
        f.win("living", "W").win("bed", "N", 0.4).win("living", "E", 0.5)
        return [f]
    xa = snap(w * 0.6)
    f.room("living", "living", 0, 0, xa, d1).room("kitchen", "kitchen", xa, 0, w - xa, d1)
    f.room("bed", "bedroom", 0, d1, xa, d - d1).room("bath", "bath", xa, d1, w - xa, d - d1)
    f.door("living", "kitchen").door("living", "bed", 0.4).door("kitchen", "bath", 0.5)
    f.out("living", "S", 0.4, entrance="front").out("kitchen", "E", 0.5, entrance="back")
    f.win("living", "W", 0.5).win("bed", "W", 0.5).win("bed", "N", 0.5).win("kitchen", "S", 0.5)
    return [f]

def L_apartment_large(w, d, two_floors=True, garage=False):
    g = Floor(0, 3.0)
    hx = 3.0
    hall_x = snap(w / 2 - hx / 2)
    d1 = snap(d * 0.5)
    g.room("hall", "hall", hall_x, 0, hx, d)
    g.room("living", "living", 0, 0, hall_x, d1).room("dining", "kitchen", 0, d1, hall_x, d - d1)
    g.room("study", "office", hall_x + hx, 0, w - hall_x - hx, d1)
    g.room("bath0", "bath", hall_x + hx, d1, w - hall_x - hx, snap((d - d1) * 0.45))
    g.room("utility", "garage" if garage else "storage", hall_x + hx, d1 + snap((d - d1) * 0.45), w - hall_x - hx, d - d1 - snap((d - d1) * 0.45))
    g.door("hall", "living", 0.5, 2.0).door("hall", "dining", 0.5).door("hall", "study").door("hall", "bath0").door("hall", "utility")
    g.door("living", "dining", 0.5, 2.0)
    g.out("hall", "S", 0.5, 2.0, entrance="front").out("dining", "N", 0.5, entrance="back")
    if garage:
        g.out("utility", "E", 0.5, 3.0, entrance="garage", vehicles=True)
    g.win("living", "W", 0.3, 2.0).win("living", "S", 0.5, 2.0).win("dining", "W", 0.5, 2.0).win("study", "E", 0.5, 2.0).win("study", "S", 0.5)
    floors = [g]
    if two_floors:
        g.stair("hall", hall_x + 0.5, snap(d * 0.55), 2.0, 4.0, "y")
        u = Floor(1, 2.9)
        u.room("upper_hall", "hall", hall_x, 0, hx, d)
        u.room("master", "bedroom", 0, 0, hall_x, d1).room("child", "bedroom", 0, d1, hall_x, d - d1)
        u.room("bath1", "bath", hall_x + hx, 0, w - hall_x - hx, snap(d * 0.35)).room("guest", "bedroom", hall_x + hx, snap(d * 0.35), w - hall_x - hx, d - snap(d * 0.35))
        u.door("upper_hall", "master").door("upper_hall", "child").door("upper_hall", "bath1").door("upper_hall", "guest")
        u.out("upper_hall", "N", 0.5, entrance="roof")
        u.win("master", "W", 0.5, 2.0).win("child", "W", 0.5).win("guest", "E", 0.5, 2.0).win("bath1", "S", 0.5)
        floors.append(u)
    return floors

def L_slum_single(w, d):
    f = Floor(0, 2.6)
    xa = snap(w - 2.0)
    f.room("room", "slum", 0, 0, xa, d).room("wc", "bath", xa, 0, w - xa, snap(d * 0.5)).room("nook", "storage", xa, snap(d * 0.5), w - xa, d - snap(d * 0.5))
    f.door("room", "wc").door("room", "nook").out("room", "S", 0.3, entrance="front").out("nook", "N", 0.5, entrance="emergency")
    f.win("room", "W", 0.5)
    return [f]

def L_flophouse(w, d, cells=4):
    f = Floor(0, 2.6)
    cw = snap(w / cells)
    cd = snap(d * 0.4)
    f.room("corridor", "hall", 0, cd, w, 2.0)
    for i in range(cells):
        x = i * cw
        ww = cw if i < cells - 1 else w - x
        f.room("cell%d" % i, "slum", x, 0, ww, cd)
        f.door("corridor", "cell%d" % i, 0.5)
        f.win("cell%d" % i, "S", 0.5)
    f.room("common", "kitchen", 0, cd + 2.0, snap(w * 0.6), d - cd - 2.0).room("wash", "bath", snap(w * 0.6), cd + 2.0, w - snap(w * 0.6), d - cd - 2.0)
    f.door("corridor", "common").door("corridor", "wash")
    f.out("corridor", "W", 0.5, entrance="front").out("corridor", "E", 0.5, entrance="back").win("common", "N", 0.5)
    return [f]

def L_shop(w, d):
    f = Floor(0, 3.2)
    d1 = snap(d * 0.65)
    xc = snap(w * 0.35)
    f.room("floor", "shopfloor", xc, 0, w - xc, d1).room("till", "counter", 0, 0, xc, d1)
    f.room("store", "storage", 0, d1, snap(w * 0.6), d - d1).room("wc", "bath", snap(w * 0.6), d1, w - snap(w * 0.6), d - d1)
    f.door("floor", "till", 0.5, 2.0).door("till", "store").door("store", "wc")
    f.out("floor", "S", 0.5, 2.0, entrance="front").out("store", "N", 0.4, entrance="back")
    f.win("floor", "S", 0.15, 2.0).win("floor", "S", 0.85, 2.0)
    return [f]

def L_bar(w, d, club=False):
    f = Floor(0, 3.6 if club else 3.2)
    d1 = snap(d * 0.55)
    xb = snap(w * 0.55)
    f.room("bar", "bar", 0, 0, w, d1)
    f.room("floor2", "dancefloor" if club else "lounge", 0, d1, xb, d - d1)
    f.room("backhall", "hall", xb, d1, 2.0, d - d1)
    rest = w - xb - 2.0
    f.room("wc", "bath", xb + 2.0, d1, rest, snap((d - d1) * 0.4)).room("office", "office", xb + 2.0, d1 + snap((d - d1) * 0.4), rest, d - d1 - snap((d - d1) * 0.4))
    f.door("bar", "floor2", 0.5, 2.0).door("bar", "backhall").door("backhall", "wc").door("backhall", "office")
    f.out("bar", "S", 0.3, 2.0, entrance="front").out("backhall", "N", 0.5, entrance="back").out("floor2", "W", 0.5, entrance="emergency")
    f.win("bar", "S", 0.75, 2.0).win("bar", "W", 0.5)
    return [f]

def L_workshop(w, d):
    f = Floor(0, 4.5)
    xg = snap(w * 0.65)
    f.room("garage", "garage", 0, 0, xg, d)
    y1 = snap(d * 0.5)
    y2 = y1 + snap(d * 0.3)
    f.room("bench", "workshop", xg, 0, w - xg, y1).room("office", "office", xg, y1, w - xg, y2 - y1).room("wc", "bath", xg, y2, w - xg, d - y2)
    f.door("garage", "bench", 0.5, 2.0).door("garage", "office").door("office", "wc")
    f.out("garage", "S", 0.5, 4.0, entrance="garage", vehicles=True).out("bench", "S", 0.5, entrance="front").out("garage", "N", 0.3, entrance="back")
    f.win("office", "E", 0.5).win("bench", "E", 0.5, 2.0)
    return [f]

def L_office(w, d, floors=2):
    g = Floor(0, 4.0)
    xr = snap(w * 0.6)
    g.room("lobby", "lobby", 0, 0, xr, snap(d * 0.6)).room("security", "booking", xr, 0, w - xr, snap(d * 0.4))
    g.room("core", "hall", xr, snap(d * 0.4), w - xr, snap(d * 0.6) - snap(d * 0.4) + d - snap(d * 0.6))
    g.room("meeting", "meeting", 0, snap(d * 0.6), snap(xr * 0.5), d - snap(d * 0.6)).room("lounge", "lounge", snap(xr * 0.5), snap(d * 0.6), xr - snap(xr * 0.5), d - snap(d * 0.6))
    g.door("lobby", "security").door("lobby", "core", 0.5, 2.0).door("lobby", "meeting", 0.5, 2.0).door("lobby", "lounge", 0.5, 2.0).door("core", "lounge")
    g.out("lobby", "S", 0.4, 3.0, entrance="front").out("core", "E", 0.8, entrance="back")
    g.win("lobby", "S", 0.85, 2.0).win("lobby", "W", 0.5, 3.0).win("meeting", "W", 0.5, 2.0)
    res = [g]
    if floors > 1:
        cr = g.R("core")
        g.stair("core", cr[0] + 0.5, cr[1] + 0.5, 2.0, 4.5, "y")
        u = Floor(1, 3.4)
        u.room("upper_core", "hall", xr, 0, w - xr, d)
        u.room("open", "openoffice", 0, 0, xr, snap(d * 0.6)).room("exec", "office", 0, snap(d * 0.6), snap(xr * 0.6), d - snap(d * 0.6)).room("server", "server", snap(xr * 0.6), snap(d * 0.6), xr - snap(xr * 0.6), d - snap(d * 0.6))
        u.door("upper_core", "open", 0.3, 2.0).door("open", "exec").door("upper_core", "server", 0.8, 1.0, lock={"key": "key_server", "hack": 3})
        u.out("upper_core", "N", 0.5, entrance="roof")
        u.win("open", "W", 0.5, 3.0).win("open", "S", 0.5, 3.0).win("exec", "W", 0.5, 2.0)
        res.append(u)
    return res

def L_clinic(w, d):
    f = Floor(0, 3.0)
    d1 = snap(d * 0.4)
    f.room("waiting", "waiting", 0, 0, snap(w * 0.6), d1).room("reception", "counter", snap(w * 0.6), 0, w - snap(w * 0.6), d1)
    f.room("corr", "hall", 0, d1, w, 2.0)
    d2 = d1 + 2.0
    f.room("surgery", "surgery", 0, d2, snap(w * 0.4), d - d2).room("ward", "ward", snap(w * 0.4), d2, snap(w * 0.35), d - d2).room("meds", "storage", snap(w * 0.75), d2, w - snap(w * 0.75), d - d2)
    f.door("waiting", "reception", 0.5).door("waiting", "corr").door("corr", "surgery").door("corr", "ward").door("corr", "meds", lock={"key": "key_meds", "hack": 2})
    f.out("waiting", "S", 0.4, entrance="front").out("corr", "E", 0.5, entrance="back")
    f.win("waiting", "W", 0.5).win("ward", "N", 0.5)
    return [f]

def L_warehouse(w, d):
    g = Floor(0, 6.0)
    xo = snap(w * 0.75)
    g.room("hall", "warehouse", 0, 0, xo, d)
    g.room("dock", "garage", xo, 0, w - xo, snap(d * 0.55)).room("stairs", "stair", xo, snap(d * 0.55), w - xo, d - snap(d * 0.55))
    g.door("hall", "dock", 0.5, 3.0).door("hall", "stairs")
    g.out("dock", "S", 0.5, 4.0, entrance="garage", vehicles=True).out("hall", "S", 0.2, entrance="front").out("hall", "N", 0.5, entrance="emergency")
    sr = g.R("stairs")
    g.stair("stairs", sr[0] + 0.5, sr[1] + 0.5, 2.0, 5.0, "y")
    u = Floor(1, 3.0)
    u.room("landing", "stair", xo, snap(d * 0.55), w - xo, d - snap(d * 0.55)).room("office", "office", xo, 0, w - xo, snap(d * 0.55))
    u.door("landing", "office").out("landing", "E", 0.5, entrance="roof").win("office", "E", 0.5, 2.0).win("office", "W", 0.5, 2.0)
    return [g, u]

def L_safehouse(w, d):
    f = Floor(0, 2.7)
    xa = snap(w * 0.55)
    f.room("entry", "hall", 0, 0, xa, 2.5).room("main", "safe", 0, 2.5, xa, d - 2.5)
    y1 = snap(d * 0.4)
    y2 = y1 + snap(d * 0.25)
    f.room("kitchen", "kitchen", xa, 0, w - xa, y1).room("bath", "bath", xa, y1, w - xa, y2 - y1).room("vault", "evidence", xa, y2, w - xa, d - y2)
    f.door("entry", "main").door("entry", "kitchen").door("main", "bath").door("main", "vault", lock={"key": "key_safehouse"}, panel=True)
    f.out("entry", "S", 0.5, entrance="front").out("vault", "E", 0.5, entrance="emergency")
    f.win("main", "W", 0.5)
    return [f]

def L_diner(w, d):
    f = Floor(0, 3.2)
    d1 = snap(d * 0.6)
    f.room("dining", "dining", 0, 0, w, d1)
    f.room("kitchen", "kitchen_pro", 0, d1, snap(w * 0.55), d - d1).room("wc", "bath", snap(w * 0.55), d1, snap(w * 0.2), d - d1).room("store", "storage", snap(w * 0.75), d1, w - snap(w * 0.75), d - d1)
    f.door("dining", "kitchen").door("dining", "wc").door("dining", "store")
    f.out("dining", "S", 0.5, 2.0, entrance="front").out("kitchen", "N", 0.5, entrance="back")
    f.win("dining", "S", 0.15, 2.0).win("dining", "S", 0.85, 2.0).win("dining", "W", 0.5, 2.0)
    return [f]

def L_hackerspace(w, d):
    f = Floor(0, 3.0)
    xa = snap(w * 0.6)
    f.room("lab", "lab", 0, 0, xa, d)
    y1 = snap(d * 0.35)
    y2 = y1 + snap(d * 0.35)
    f.room("server", "server", xa, 0, w - xa, y1).room("bench", "workshop", xa, y1, w - xa, y2 - y1).room("lounge", "lounge", xa, y2, w - xa, d - y2)
    f.door("lab", "server", 0.5, 1.0, lock={"hack": 2}).door("lab", "bench").door("lab", "lounge").door("bench", "lounge")
    f.out("lab", "S", 0.3, entrance="front").out("lounge", "N", 0.5, entrance="back")
    f.win("lab", "W", 0.5, 2.0)
    return [f]

def L_community(w, d, church=True):
    f = Floor(0, 6.0 if church else 4.0)
    xa = snap(w * 0.72)
    f.room("nave", "sanctuary" if church else "community", 0, 0, xa, d)
    y1 = snap(d * 0.35)
    y2 = y1 + snap(d * 0.35)
    f.room("vestry", "office", xa, 0, w - xa, y1).room("kitchen", "kitchen", xa, y1, w - xa, y2 - y1).room("wc", "bath", xa, y2, w - xa, d - y2)
    f.door("nave", "vestry").door("nave", "kitchen").door("kitchen", "wc")
    f.out("nave", "S", 0.5, 2.0, entrance="front").out("vestry", "E", 0.5, entrance="back")
    f.win("nave", "W", 0.25, 2.0).win("nave", "W", 0.75, 2.0).win("nave", "N", 0.5, 2.0)
    return [f]

def L_police(w, d):
    f = Floor(0, 3.2)
    d1 = snap(d * 0.35)
    f.room("front", "booking", 0, 0, w, d1)
    f.room("corr", "hall", 0, d1, w, 2.0)
    d2 = d1 + 2.0
    cw = 3.0
    f.room("brief", "briefing", 0, d2, snap(w * 0.4), d - d2).room("evidence", "evidence", snap(w * 0.4), d2, snap(w * 0.2), d - d2)
    x = snap(w * 0.4) + snap(w * 0.2)
    i = 0
    while x + cw <= w + 1e-6:
        f.room("cell%d" % i, "cell", x, d2, cw, d - d2)
        f.door("corr", "cell%d" % i, 0.5, 1.0, lock={"key": "key_cells", "hack": 3}, panel=True)
        x += cw
        i += 1
    f.door("front", "corr", 0.3).door("corr", "brief").door("corr", "evidence", 0.5, 1.0, lock={"key": "key_evidence", "hack": 4}, panel=True)
    f.out("front", "S", 0.5, 2.0, entrance="front").out("corr", "W", 0.5, entrance="back")
    f.win("front", "S", 0.15, 2.0).win("brief", "W", 0.5)
    return [f]

# --- Zugriffsregeln je Raumtyp ----------------------------------------------
ACCESS = {
    "apartment_small": {"open": None, "key": "key_home", "hack": 1},
    "apartment_large": {"open": None, "key": "key_home", "hack": 2},
    "slum_room": {"open": [0, 24]},
    "shop_kiosk": {"open": [7, 23], "hack": 1},
    "bar_club": {"open": [18, 4], "hack": 1},
    "workshop_garage": {"open": [8, 20], "hack": 1},
    "office_lobby": {"open": [7, 20], "faction": "aurelia", "minStanding": -20, "hack": 3},
    "clinic": {"open": [0, 24], "faction": "collective", "minStanding": -40},
    "warehouse": {"open": [6, 22], "hack": 2},
    "safehouse": {"open": None, "key": "key_safehouse"},
    "diner": {"open": [6, 2]},
    "hackerspace": {"open": [0, 24], "faction": "collective", "minStanding": 10, "hack": 2},
    "community_hall": {"open": [7, 22]},
    "police_station": {"open": [0, 24], "faction": "kcg", "minStanding": -10, "hack": 4},
}

ROOM_TYPES = [
    {"id": "apartment_small", "label": "Wohnung (klein)", "role": "resident", "activities": ["sleep", "cook", "watch_tv", "wash"]},
    {"id": "apartment_large", "label": "Wohnung (gross) / Haus", "role": "resident", "activities": ["sleep", "cook", "watch_tv", "read", "work"]},
    {"id": "slum_room", "label": "Slum-Zimmer / Absteige", "role": "resident", "activities": ["sleep", "warm_hands", "smoke", "drink"]},
    {"id": "shop_kiosk", "label": "Laden / Kiosk", "role": "shopkeeper", "activities": ["shopkeeper", "browse"]},
    {"id": "bar_club", "label": "Bar / Club", "role": "patron", "activities": ["bartender", "drink", "dance", "dj", "play"]},
    {"id": "workshop_garage", "label": "Werkstatt / Garage", "role": "worker", "activities": ["repair", "smoke"]},
    {"id": "office_lobby", "label": "Buero / Konzernlobby", "role": "worker", "activities": ["reception", "work", "guard", "idle"]},
    {"id": "clinic", "label": "Klinik ohne Lizenz", "role": "medic", "activities": ["surgery", "patient", "reception"]},
    {"id": "warehouse", "label": "Lagerhalle", "role": "worker", "activities": ["carry", "guard", "work"]},
    {"id": "safehouse", "label": "Versteck / Safehouse", "role": "ally", "activities": ["plan", "sleep", "train", "hack"]},
    {"id": "diner", "label": "Imbiss", "role": "cook", "activities": ["cook", "eat", "shopkeeper"]},
    {"id": "hackerspace", "label": "Hackerspace", "role": "hacker", "activities": ["hack", "repair", "relax"]},
    {"id": "community_hall", "label": "Kirche / Gemeinschaftshalle", "role": "member", "activities": ["pray", "preach", "eat"]},
    {"id": "police_station", "label": "Polizeiwache (KCG)", "role": "officer", "activities": ["reception", "work", "guard", "prisoner"]},
]

DISTRICT_STYLES = {"Core": "corporate", "Saltmarket": "worker", "LanternHill": "bohemian", "Greenmoor": "suburban", "Rustline": "industrial", "Sunstrip": "neon_tourist"}

SHOPS = [
    {"id": "kiosk_basic", "label": "Eckkiosk", "owner": "npc_kiosk_owner", "open": [6, 24], "category": "food",
     "wares": [["water", "Wasser", 3, 30], ["soda", "Limo", 4, 30], ["snack_bar", "Riegel", 3, 40], ["cigarettes", "Zigaretten", 9, 25], ["newspaper", "Zeitung", 2, 10], ["phone_card", "Prepaid-Karte", 20, 8]]},
    {"id": "store_247", "label": "24/7-Markt", "owner": "npc_store_clerk", "open": [0, 24], "category": "food",
     "wares": [["water", "Wasser", 2, 50], ["canned_food", "Konserve", 5, 40], ["medkit_small", "Verbandszeug", 25, 10], ["energy_drink", "Energydrink", 5, 30], ["batteries", "Batterien", 6, 20], ["lighter", "Feuerzeug", 2, 20]]},
    {"id": "boutique_greenmoor", "label": "Boutique Greenmoor", "owner": "npc_boutique_owner", "open": [10, 19], "category": "clothing",
     "wares": [["jacket_designer", "Designerjacke", 850, 4], ["sunglasses", "Sonnenbrille", 220, 6], ["dress_shoes", "Lederschuhe", 380, 5], ["scarf_silk", "Seidenschal", 140, 6]]},
    {"id": "records_lantern", "label": "Plattenladen Lantern Hill", "owner": "npc_record_dealer", "open": [11, 21], "category": "media",
     "wares": [["vinyl_local", "Platte lokaler Bands", 25, 12], ["zine", "Untergrund-Zine", 3, 30], ["radio_mod", "Radio-Umbausatz", 60, 5], ["poster_rolled", "Plakatrolle", 8, 20]]},
    {"id": "souvenir_sunstrip", "label": "Souvenirladen am Pier", "owner": "npc_souvenir_seller", "open": [9, 23], "category": "clothing",
     "wares": [["cap_wheel", "Kappe Kestrel Wheel", 18, 20], ["shirt_beach", "Strandhemd", 35, 15], ["sunscreen", "Sonnencreme", 12, 25], ["keychain", "Schluesselanhaenger", 5, 50]]},
    {"id": "bar_drinks", "label": "Getraenkekarte", "owner": "npc_bartender", "open": [18, 4], "category": "drink",
     "wares": [["beer", "Bier", 5, 99], ["whisky", "Whisky", 12, 40], ["cocktail_neon", "Neon-Cocktail", 14, 40], ["water", "Wasser", 2, 99]]},
    {"id": "diner_menu", "label": "Imbisskarte", "owner": "npc_diner_cook", "open": [6, 2], "category": "food",
     "wares": [["burger", "Burger", 8, 99], ["fries", "Pommes", 4, 99], ["noodles", "Nudelsuppe", 7, 99], ["fish_roll", "Fischbroetchen", 6, 60], ["coffee", "Kaffee", 3, 99]]},
    {"id": "garage_parts", "label": "Teiletheke", "owner": "npc_mechanic", "open": [8, 20], "category": "vehicle",
     "wares": [["repair_kit", "Reparaturset", 120, 10], ["tires_sport", "Sportreifen", 480, 4], ["nitro_can", "Lachgasflasche", 300, 3], ["plate_fake", "Gefaelschtes Kennzeichen", 900, 2]]},
    {"id": "clinic_meds", "label": "Hinterzimmer-Apotheke", "owner": "npc_street_doc", "open": [0, 24], "category": "medical",
     "wares": [["medkit", "Medkit", 60, 10], ["painkillers", "Schmerzmittel", 20, 20], ["stim", "Stimulans", 75, 6], ["implant_cleanup", "Implantat-Reinigung", 400, 2]]},
    {"id": "hacker_gear", "label": "Hardware-Tausch", "owner": "npc_hacker_trader", "open": [14, 4], "category": "electronics",
     "wares": [["jammer", "Kamera-Stoersender", 650, 3], ["burner_phone", "Wegwerfhandy", 40, 10], ["deck_upgrade", "Hackdeck-Upgrade", 1200, 2], ["cable_kit", "Kabelset", 15, 20]]},
]

# ---------------------------------------------------------------------------
# Vorlagen (je Bezirk Varianten)
# ---------------------------------------------------------------------------
def add(tid, rtype, label, districts, w, d, floors, extra_secrets=None, npcs=None, **kw):
    fl = floors
    first = fl[0]
    rooms = first.rooms
    main = rooms[0]
    mx, my = mid(main["rect"], 0.8, 0.8)
    secrets = [sec(tid + "_cache", main["id"], mx, my, "collectible")]
    if extra_secrets:
        secrets += extra_secrets
    # Handgesetzte Punkte, die nach Rasterrundung ausserhalb ihres Raums liegen, in den Raum ziehen
    for e in (extra_secrets or []) + (kw.get("interactives") or []):
        lvl = e.get("floor", 0)
        rr = next(r["rect"] for r in fl[lvl].rooms if r["id"] == e["room"])
        x0, y0, x1, y1 = rr[0] + 0.5, rr[1] + 0.5, rr[0] + rr[2] - 0.5, rr[1] + rr[3] - 0.5
        e["at"][0] = min(max(e["at"][0], x0), x1)
        e["at"][1] = min(max(e["at"][1], y0), y1)
    acc = dict(ACCESS[rtype])
    acc.update(kw.pop("access", {}) or {})
    nslots = npcs if npcs is not None else []
    if not nslots:
        rt = next(r for r in ROOM_TYPES if r["id"] == rtype)
        ax, ay = mid(main["rect"], 0.5, 0.5)
        nslots = [npc(main["id"], ax, ay, rt["activities"][0], rt["role"], chance=0.9)]
    return template(tid, rtype, label, districts, w, d, fl, secrets=secrets, npcs=nslots, access=acc, **kw)

def build_templates():
    # Kestrel Core
    add("core_microflat", "apartment_small", "Mikroappartement im Wohnturm", ["Core"], 7, 9, L_apartment_small(7, 9, studio=True))
    add("core_penthouse", "apartment_large", "Penthouse mit Dachterrassen-Zugang", ["Core"], 16, 18, L_apartment_large(16, 18, True),
        extra_secrets=[sec("core_penthouse_safe", "study", 13.0, 2.0, "stash", hidden=True, hint="terminal")],
        interactives=[{"kind": "terminal", "floor": 0, "room": "study", "at": [12.0, 4.0], "rot": 180, "id": "penthouse_pc", "hack": 3, "links": "core_penthouse_safe"}])
    add("core_aurelia_lobby", "office_lobby", "Aurelia-Holdings-Lobby", ["Core"], 26, 22, L_office(26, 22, 2),
        extra_secrets=[sec("core_aurelia_memo", "server", 18.0, 18.0, "note", floor=1)])
    add("core_office_annex", "office_lobby", "Verwaltungsnebengebaeude", ["Core", "Saltmarket"], 20, 18, L_office(20, 18, 2), style="corporate")
    add("core_backroom_clinic", "clinic", "Kellerklinik unter dem Finanzviertel", ["Core"], 14, 14, L_clinic(14, 14), shop="clinic_meds")
    add("core_kcg_precinct", "police_station", "KCG-Revier Core", ["Core"], 22, 16, L_police(22, 16))
    add("core_noodle_bar", "diner", "Nudelbar im Pendlertunnel", ["Core", "Sunstrip"], 12, 10, L_diner(12, 10), shop="diner_menu")
    add("core_store_247", "shop_kiosk", "24/7-Markt am Tower", ["Core"], 12, 12, L_shop(12, 12), shop="store_247")
    add("core_sky_lounge", "bar_club", "Sky Lounge", ["Core"], 20, 16, L_bar(20, 16, club=False), shop="bar_drinks")
    add("core_capsule_safehouse", "safehouse", "Kapsel-Versteck im Parkhaus", ["Core"], 9, 9, L_safehouse(9, 9))
    # Saltmarket
    add("salt_worker_flat", "apartment_small", "Arbeiterwohnung am Kai", ["Saltmarket"], 8, 10, L_apartment_small(8, 10))
    add("salt_family_house", "apartment_large", "Hafenarbeiter-Reihenhaus", ["Saltmarket", "LanternHill"], 13, 14, L_apartment_large(13, 14, True))
    add("salt_dock_bunks", "slum_room", "Schlafbaracke der Tageloehner", ["Saltmarket"], 14, 10, L_flophouse(14, 10, 4))
    add("salt_dock_warehouse", "warehouse", "Docklager 7", ["Saltmarket"], 28, 24, L_warehouse(28, 24),
        extra_secrets=[sec("salt_dock_manifest", "office", 25.0, 4.0, "note", floor=1)])
    add("salt_dock_bar", "bar_club", "Kneipe Zum Ankerkettenriss", ["Saltmarket"], 16, 14, L_bar(16, 14), shop="bar_drinks")
    add("salt_fish_imbiss", "diner", "Fischimbiss am Markt", ["Saltmarket"], 10, 9, L_diner(10, 9), shop="diner_menu")
    add("salt_kiosk", "shop_kiosk", "Hafenkiosk", ["Saltmarket", "Rustline"], 9, 8, L_shop(9, 8), shop="kiosk_basic")
    add("salt_union_hall", "community_hall", "Gewerkschaftshalle der Dockers' Union", ["Saltmarket"], 20, 16, L_community(20, 16, church=False),
        access={"faction": "union", "minStanding": 0})
    add("salt_boat_workshop", "workshop_garage", "Boots- und Lkw-Werkstatt", ["Saltmarket"], 18, 14, L_workshop(18, 14), shop="garage_parts")
    add("salt_container_safehouse", "safehouse", "Containerversteck", ["Saltmarket"], 10, 8, L_safehouse(10, 8))
    # Lantern Hill
    add("lantern_studio", "apartment_small", "Atelierwohnung unterm Dach", ["LanternHill"], 8, 9, L_apartment_small(8, 9, studio=True))
    add("lantern_oldtown_flat", "apartment_large", "Altbauwohnung mit Treppenhaus", ["LanternHill"], 12, 14, L_apartment_large(12, 14, True))
    add("lantern_underground_club", "bar_club", "Club Kellerlicht", ["LanternHill"], 20, 18, L_bar(20, 18, club=True), shop="bar_drinks",
        extra_secrets=[sec("lantern_club_tape", "office", 18.5, 16.0, "note")])
    add("lantern_hackerspace", "hackerspace", "Collective-Hackerspace", ["LanternHill"], 16, 14, L_hackerspace(16, 14), shop="hacker_gear",
        extra_secrets=[sec("lantern_aegis_leak", "server", 14.0, 2.0, "note", hidden=True)],
        interactives=[{"kind": "terminal", "floor": 0, "room": "server", "at": [12.0, 2.5], "rot": 180, "id": "leak_terminal", "hack": 2, "links": "lantern_aegis_leak"}])
    add("lantern_st_merrin", "community_hall", "Kirche St. Merrin", ["LanternHill"], 18, 26, L_community(18, 26, church=True),
        extra_secrets=[sec("lantern_bell_note", "vestry", 16.0, 4.0, "note")])
    add("lantern_street_clinic", "clinic", "Klinik ohne Lizenz", ["LanternHill", "Rustline"], 14, 13, L_clinic(14, 13), shop="clinic_meds")
    add("lantern_record_shop", "shop_kiosk", "Plattenladen", ["LanternHill"], 10, 10, L_shop(10, 10), shop="records_lantern")
    add("lantern_taverna", "diner", "Treppen-Taverne", ["LanternHill"], 12, 11, L_diner(12, 11), shop="diner_menu")
    add("lantern_rooftop_safehouse", "safehouse", "Dachversteck des Collective", ["LanternHill"], 10, 10, L_safehouse(10, 10))
    # Greenmoor
    add("green_villa", "apartment_large", "Villa mit Doppelgarage", ["Greenmoor"], 18, 20, L_apartment_large(18, 20, True, garage=True),
        extra_secrets=[sec("green_villa_ledger", "study", 15.5, 3.0, "stash", hidden=True)],
        interactives=[{"kind": "terminal", "floor": 0, "room": "study", "at": [14.0, 2.5], "rot": 180, "id": "villa_safe", "hack": 3, "links": "green_villa_ledger"}])
    add("green_townhouse", "apartment_large", "Reihenhaus im Gated Court", ["Greenmoor"], 12, 16, L_apartment_large(12, 16, True))
    add("green_boutique", "shop_kiosk", "Boutique", ["Greenmoor"], 12, 12, L_shop(12, 12), shop="boutique_greenmoor")
    add("green_security_post", "police_station", "Private Sicherheitswache", ["Greenmoor"], 16, 14, L_police(16, 14), style="suburban")
    add("green_club_house", "office_lobby", "Country-Club-Haus", ["Greenmoor"], 22, 18, L_office(22, 18, 1), style="suburban")
    add("green_cafe", "diner", "Country-Club-Cafe", ["Greenmoor"], 12, 10, L_diner(12, 10), shop="diner_menu")
    add("green_garage_safehouse", "safehouse", "Versteck in der Gaertnerhuette", ["Greenmoor"], 9, 8, L_safehouse(9, 8), style="worker")
    # Rustline
    add("rust_squat", "slum_room", "Besetzte Werkhalle", ["Rustline"], 9, 8, L_slum_single(9, 8), style="slum")
    add("rust_flophouse", "slum_room", "Absteige an der Bahnlinie", ["Rustline", "Saltmarket"], 16, 11, L_flophouse(16, 11, 5), style="slum")
    add("rust_chop_shop", "workshop_garage", "Chop-Shop der Wrenchers", ["Rustline"], 20, 16, L_workshop(20, 16), shop="garage_parts",
        access={"faction": "wrenchers", "minStanding": 0, "hack": 2},
        extra_secrets=[sec("rust_vin_list", "office", 18.0, 9.0, "note")])
    add("rust_race_garage", "workshop_garage", "Rennwerkstatt", ["Rustline"], 18, 14, L_workshop(18, 14), shop="garage_parts")
    add("rust_scrap_hall", "warehouse", "Schrotthalle", ["Rustline"], 30, 24, L_warehouse(30, 24))
    add("rust_backalley_clinic", "clinic", "Hinterhof-Klinik", ["Rustline"], 12, 13, L_clinic(12, 13), style="slum", shop="clinic_meds")
    add("rust_scrap_hackerspace", "hackerspace", "Schrott-Hackerspace", ["Rustline"], 14, 12, L_hackerspace(14, 12), shop="hacker_gear")
    add("rust_biker_bar", "bar_club", "Bikerbar Rostfrei", ["Rustline"], 16, 14, L_bar(16, 14), shop="bar_drinks")
    add("rust_bunker_safehouse", "safehouse", "Bunker unter der Raffinerie", ["Rustline"], 11, 10, L_safehouse(11, 10))
    add("rust_worker_flat", "apartment_small", "Werkswohnung", ["Rustline"], 8, 9, L_apartment_small(8, 9))
    add("rust_truck_stop", "diner", "Fernfahrer-Imbiss", ["Rustline"], 13, 10, L_diner(13, 10), shop="diner_menu")
    # Sunstrip
    add("sun_motel_room", "apartment_small", "Motelzimmer am Boulevard", ["Sunstrip"], 7, 9, L_apartment_small(7, 9, studio=True))
    add("sun_neon_club", "bar_club", "Club Wheelhouse", ["Sunstrip"], 22, 18, L_bar(22, 18, club=True), shop="bar_drinks")
    add("sun_souvenir", "shop_kiosk", "Souvenirladen", ["Sunstrip"], 10, 10, L_shop(10, 10), shop="souvenir_sunstrip")
    add("sun_burger", "diner", "Burgerbude am Pier", ["Sunstrip"], 12, 10, L_diner(12, 10), shop="diner_menu")
    add("sun_beach_house", "apartment_large", "Strandhaus", ["Sunstrip", "Greenmoor"], 14, 14, L_apartment_large(14, 14, True))
    add("sun_hinterland_room", "slum_room", "Hinterland-Zimmer", ["Sunstrip"], 8, 8, L_slum_single(8, 8), style="slum")
    add("sun_arcade_backroom", "hackerspace", "Spielhalle mit Hinterzimmer", ["Sunstrip"], 16, 12, L_hackerspace(16, 12))
    add("sun_beach_patrol", "police_station", "Strandwache", ["Sunstrip"], 16, 13, L_police(16, 13))
    add("sun_storage_hall", "warehouse", "Lager der Promenadenbetriebe", ["Sunstrip"], 22, 18, L_warehouse(22, 18))

def main():
    random.seed(7)
    build_templates()
    furnishing = {"version": 1, "units": "m", "note": "Front = lokale +X-Achse; pos = Teilmitte relativ zum Objektursprung am Boden (Wand-/Deckenobjekte: z relativ zum Boden bzw. zur Decke).",
                  "shapes": ["cube", "cylinder", "sphere", "cone", "plane"], "styles": STYLES, "roomKinds": [{"kind": k, "rules": r} for k, r in ROOM_KINDS.items()], "items": ITEMS}
    shops = [{"id": s["id"], "label": s["label"], "owner": s["owner"], "open": s["open"], "category": s["category"],
              "wares": [{"item": w[0], "label": w[1], "price": w[2], "stock": w[3]} for w in s["wares"]]} for s in SHOPS]
    interiors = {"version": 1, "units": "m", "grid": G,
                 "defaults": {"wallHeight": 3.0, "wallThickness": 0.2, "doorHeight": 2.2, "windowSill": 0.9, "windowTop": 2.2, "slabThickness": 0.2},
                 "districtStyles": DISTRICT_STYLES, "roomTypes": ROOM_TYPES, "shops": shops, "templates": TEMPLATES}
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "furnishing.json"), "w", encoding="utf-8") as fh:
        json.dump(furnishing, fh, ensure_ascii=False, indent=1)
    with open(os.path.join(OUT_DIR, "interiors.json"), "w", encoding="utf-8") as fh:
        json.dump(interiors, fh, ensure_ascii=False, indent=1)
    print("furnishing.json: %d Objekte, %d Stile, %d Raumarten" % (len(ITEMS), len(STYLES), len(ROOM_KINDS)))
    print("interiors.json: %d Raumtypen, %d Vorlagen, %d Laeden" % (len(ROOM_TYPES), len(TEMPLATES), len(SHOPS)))

if __name__ == "__main__":
    main()
