# -*- coding: utf-8 -*-
"""
Prueft Content/Data/interiors.json und furnishing.json.

Pruefungen:
  * Pflichtfelder, eindeutige IDs (Vorlagen, Objekte, Geheimnisse)
  * Raeume im Grundriss, auf dem 0,5-m-Raster, keine Ueberlappung je Etage
  * Tueren liegen auf einer echten Wand zwischen den genannten Raeumen (bzw. Aussenwand)
  * Fenster liegen auf einer Aussenwand ihres Raums
  * Treppen liegen in ihrem Raum und werden oben von einem Raum aufgenommen
  * Erreichbarkeit: jeder Raum ist von einem Eingang aus erreichbar (Tueren + Treppen)
  * Mindestens ein Eingang "front"; Eingangsarten gueltig
  * Querverweise: Raumtyp, Stil, Laden, Moebel-IDs (props + roomKinds), NPC-Raeume
Aufruf: <python> tools/ue_interiors/validate_interiors.py   (Exitcode 1 bei Fehlern)
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "Content", "Data")
G = 0.5
EPS = 1e-6
ENTRANCES = {"front", "back", "roof", "emergency", "garage"}
SHAPES = {"cube", "cylinder", "sphere", "cone", "plane"}
MOUNTS = {"floor", "wall", "ceiling", "surface"}
PLACES = {"wall", "corner", "center", "scatter", "wallDecor", "ceiling", "surface"}
DISTRICTS = {"Core", "Saltmarket", "LanternHill", "Greenmoor", "Rustline", "Sunstrip"}
INTERACTIVE_KINDS = {"door", "switch", "terminal", "hiding", "secret"}


def on_grid(v):
    return abs(v / G - round(v / G)) < EPS


def load(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as fh:
        return json.load(fh)


def cells_of(rect):
    x, y, w, d = rect
    return {(i, j) for i in range(round(x / G), round((x + w) / G)) for j in range(round(y / G), round((y + d) / G))}


def wall_edges(owner, at, width):
    """Liefert (Orientierung, Raumpaare) der Wandkanten unter einer Oeffnung."""
    x, y = at
    res = {}
    # senkrechte Wand (Linie x = const)
    if on_grid(x):
        i = round(x / G)
        pairs = []
        for j in range(-2, 4000):
            cy = (j + 0.5) * G
            if abs(cy - y) < width / 2 - EPS:
                pairs.append((owner.get((i - 1, j)), owner.get((i, j))))
            if cy > y + width:
                break
        res["v"] = pairs
    if on_grid(y):
        j = round(y / G)
        pairs = []
        for i in range(-2, 4000):
            cx = (i + 0.5) * G
            if abs(cx - x) < width / 2 - EPS:
                pairs.append((owner.get((i, j - 1)), owner.get((i, j))))
            if cx > x + width:
                break
        res["h"] = pairs
    return res


def opening_ok(owner, at, width, a, b):
    want = {a, b}
    for _, pairs in wall_edges(owner, at, width).items():
        if pairs and len(pairs) == round(width / G) and all({p[0] or "outside", p[1] or "outside"} == want for p in pairs):
            return True
    return False


def validate():
    errors, warnings = [], []
    interiors = load("interiors.json")
    furn = load("furnishing.json")

    item_ids = set()
    for it in furn["items"]:
        if it["id"] in item_ids:
            errors.append("Moebel-ID doppelt: " + it["id"])
        item_ids.add(it["id"])
        if it.get("mount", "floor") not in MOUNTS:
            errors.append("%s: unbekannter mount %s" % (it["id"], it.get("mount")))
        if not it.get("parts"):
            errors.append("%s: keine Teile" % it["id"])
        for p in it.get("parts", []):
            if p["shape"] not in SHAPES:
                errors.append("%s: unbekannte Form %s" % (it["id"], p["shape"]))
            if len(p["pos"]) != 3 or len(p["size"]) != 3 or min(p["size"]) <= 0:
                errors.append("%s: Teil mit ungueltiger Groesse" % it["id"])
            if not (isinstance(p["color"], str) and p["color"].startswith("#") and len(p["color"]) in (4, 7)):
                errors.append("%s: Farbe %s" % (it["id"], p["color"]))
    style_ids = {s["id"] for s in furn["styles"]}
    room_kinds = {rk["kind"]: rk["rules"] for rk in furn["roomKinds"]}
    if len(room_kinds) != len(furn["roomKinds"]):
        errors.append("roomKinds: Raumart doppelt")
    for kind, rules in room_kinds.items():
        for r in rules:
            if r["place"] not in PLACES:
                errors.append("roomKinds.%s: place %s" % (kind, r["place"]))
            for pid in r["pick"]:
                if pid not in item_ids:
                    errors.append("roomKinds.%s: Moebel-ID fehlt: %s" % (kind, pid))

    rtypes = {r["id"] for r in interiors["roomTypes"]}
    shops = {s["id"] for s in interiors["shops"]}
    for d, s in interiors["districtStyles"].items():
        if d not in DISTRICTS: errors.append("districtStyles: Bezirk %s" % d)
        if s not in style_ids: errors.append("districtStyles: Stil %s" % s)

    tids, secret_ids = set(), set()
    per_district = {d: 0 for d in DISTRICTS}
    for t in interiors["templates"]:
        tid = t["id"]
        E = lambda m: errors.append("%s: %s" % (tid, m))
        if tid in tids: E("Vorlagen-ID doppelt")
        tids.add(tid)
        if t["roomType"] not in rtypes: E("Raumtyp unbekannt " + t["roomType"])
        if t.get("style") and t["style"] not in style_ids: E("Stil unbekannt " + t["style"])
        if t.get("shop") and t["shop"] not in shops: E("Laden unbekannt " + t["shop"])
        for d in t["districts"]:
            if d not in DISTRICTS: E("Bezirk unbekannt " + d)
            else: per_district[d] += 1
        fw, fd = t["footprint"]
        floors = {f["level"]: f for f in t["floors"]}
        owners, rooms_by_level = {}, {}
        node_ids = []
        for f in t["floors"]:
            lvl = f["level"]
            owner = {}
            rooms_by_level[lvl] = {}
            for r in f["rooms"]:
                x, y, w, d = r["rect"]
                if not all(on_grid(v) for v in r["rect"]): E("Raum %s nicht im Raster" % r["id"])
                if w < 1.5 or d < 1.5: E("Raum %s zu klein" % r["id"])
                if x < -EPS or y < -EPS or x + w > fw + EPS or y + d > fd + EPS: E("Raum %s ausserhalb Grundriss" % r["id"])
                if r["kind"] not in room_kinds: E("Raumart %s ohne Fuellregeln" % r["kind"])
                if r["id"] in rooms_by_level[lvl]: E("Raum-ID doppelt %s" % r["id"])
                rooms_by_level[lvl][r["id"]] = r
                for c in cells_of(r["rect"]):
                    if c in owner: E("Raeume %s und %s ueberlappen (Etage %d)" % (owner[c], r["id"], lvl)); break
                    owner[c] = r["id"]
                node_ids.append((lvl, r["id"]))
            owners[lvl] = owner
        # Kanten fuer Erreichbarkeit
        adj = {n: set() for n in node_ids}
        starts, kinds = [], set()
        for f in t["floors"]:
            lvl, owner = f["level"], owners[f["level"]]
            for dr in f["doors"]:
                a, b = dr["rooms"]
                w = dr.get("width", 1.0)
                if a not in rooms_by_level[lvl] or (b != "outside" and b not in rooms_by_level[lvl]):
                    E("Tuer verweist auf unbekannten Raum %s/%s" % (a, b)); continue
                if not opening_ok(owner, dr["at"], w, a, b):
                    E("Tuer %s/%s bei %s liegt nicht auf gemeinsamer Wand" % (a, b, dr["at"]))
                if b == "outside":
                    ent = dr.get("entrance")
                    if ent:
                        if ent not in ENTRANCES: E("Eingangsart %s" % ent)
                        kinds.add(ent)
                        starts.append((lvl, a))
                else:
                    adj[(lvl, a)].add((lvl, b)); adj[(lvl, b)].add((lvl, a))
            for wn in f["windows"]:
                if wn["room"] not in rooms_by_level[lvl]: E("Fenster in unbekanntem Raum"); continue
                if not opening_ok(owner, wn["at"], wn.get("width", 1.0), wn["room"], "outside"):
                    E("Fenster %s bei %s nicht auf Aussenwand" % (wn["room"], wn["at"]))
            for st in f.get("stairs", []):
                up = st["toLevel"]
                cells = cells_of(st["rect"])
                if any(owner.get(c) != st["room"] for c in cells): E("Treppe nicht vollstaendig in %s" % st["room"])
                if up not in owners: E("Treppe zu fehlender Etage %d" % up); continue
                ups = {owners[up].get(c) for c in cells}
                if len(ups) != 1 or None in ups: E("Treppe wird oben nicht von genau einem Raum aufgenommen")
                else:
                    u = ups.pop()
                    adj[(lvl, st["room"])].add((up, u)); adj[(up, u)].add((lvl, st["room"]))
        if "front" not in kinds: E("kein Eingang 'front'")
        seen, todo = set(starts), list(starts)
        while todo:
            n = todo.pop()
            for m in adj.get(n, ()):
                if m not in seen: seen.add(m); todo.append(m)
        for n in node_ids:
            if n not in seen: E("Raum %s (Etage %d) nicht erreichbar" % (n[1], n[0]))
        # Props, Geheimnisse, NPCs, Interaktive
        def check_point(kind, e):
            lvl = e.get("floor", 0)
            r = rooms_by_level.get(lvl, {}).get(e["room"])
            if not r: E("%s in unbekanntem Raum %s" % (kind, e["room"])); return
            x, y, w, d = r["rect"]
            px, py = e["at"][0], e["at"][1]
            if not (x - EPS <= px <= x + w + EPS and y - EPS <= py <= y + d + EPS): E("%s ausserhalb von Raum %s" % (kind, e["room"]))
        for p in t.get("props", []):
            if p["item"] not in item_ids: E("Moebel-ID fehlt: " + p["item"])
            check_point("Prop", p)
        for s in t.get("secrets", []):
            if s["id"] in secret_ids: E("Geheimnis-ID doppelt " + s["id"])
            secret_ids.add(s["id"])
            check_point("Geheimnis", s)
        for n in t.get("npcSlots", []): check_point("NPC-Slot", n)
        for it in t.get("interactives", []):
            if it["kind"] not in INTERACTIVE_KINDS: E("Interaktiv-Art " + it["kind"])
            check_point("Interaktiv", it)
            if it.get("links") and it["links"] not in {s["id"] for s in t.get("secrets", [])}:
                warnings.append("%s: Link %s zeigt auf kein Geheimnis dieser Vorlage" % (tid, it["links"]))
    for d, n in per_district.items():
        if n < 5: warnings.append("Bezirk %s hat nur %d Vorlagen" % (d, n))
    stats = {"items": len(item_ids), "styles": len(style_ids), "roomKinds": len(room_kinds), "roomTypes": len(rtypes),
             "templates": len(tids), "shops": len(shops), "secrets": len(secret_ids), "perDistrict": per_district}
    return errors, warnings, stats


if __name__ == "__main__":
    errs, warns, st = validate()
    for w in warns: print("WARNUNG:", w)
    for e in errs: print("FEHLER:", e)
    print(json.dumps(st, ensure_ascii=False))
    print("OK" if not errs else "%d Fehler" % len(errs))
    sys.exit(1 if errs else 0)
