"""Prueft appearance.json und wardrobe.json samt Querverweisen. Laeuft auch ausserhalb von Unreal:
    python tools/ue_characters/validate_character_data.py
Rueckgabecode 0 = OK, 1 = Fehler."""
import sys

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from common import load_json


def validate():
    w = load_json("wardrobe.json")
    a = load_json("appearance.json")
    err = []
    items = {}
    for i in w["items"]:
        if i["id"] in items:
            err.append("Doppelte Item-ID " + i["id"])
        items[i["id"]] = i
        if i["slot"] not in w["slots"]:
            err.append("Unbekannter Slot bei " + i["id"])
        if i["style"] not in w["styles"]:
            err.append("Unbekannter Stil bei " + i["id"])
    presets = {}
    for p in w["presets"]:
        if p["id"] in presets:
            err.append("Doppeltes Preset " + p["id"])
        presets[p["id"]] = p
        for slot, rule in p["slots"].items():
            for it in rule["items"]:
                if it not in items:
                    err.append("%s: Item %s fehlt" % (p["id"], it))
                elif items[it]["slot"] != slot:
                    err.append("%s: Item %s liegt im falschen Slot" % (p["id"], it))
        for slot, pal in p["palette"].items():
            if pal not in w["palettes"]:
                err.append("%s: Palette %s fehlt" % (p["id"], pal))
    marks = {m["id"] for m in a["marks"]}
    hair = {h["id"] for h in a["hairStyles"]}
    beards = {b["id"] for b in a["beardStyles"]}
    morphs = {m["id"] for m in a["faceMorphs"]}
    for arch in a["archetypes"]:
        if not arch["outfitPresets"]:
            err.append(arch["id"] + ": kein Outfit-Preset")
        for p in arch["outfitPresets"]:
            if p not in presets:
                err.append("%s: Preset %s fehlt" % (arch["id"], p))
        for m in arch["markPool"]:
            if m not in marks:
                err.append("%s: Marke %s fehlt" % (arch["id"], m))
        if len(arch["districtWeights"]) != 6:
            err.append(arch["id"] + ": districtWeights muss 6 Werte haben")
    for c in a["keyCharacters"] + a["creatorPresets"]:
        if c["outfitPreset"] not in presets:
            err.append("%s: Preset %s fehlt" % (c["id"], c["outfitPreset"]))
        if c["hair"]["style"] not in hair:
            err.append(c["id"] + ": Haarstil fehlt")
        if c["beard"]["style"] not in beards:
            err.append(c["id"] + ": Bartstil fehlt")
        for m in c["marks"]:
            if m["id"] not in marks:
                err.append("%s: Marke %s fehlt" % (c["id"], m["id"]))
        for k in c["faceMorphs"]:
            if k not in morphs:
                err.append("%s: Morph %s fehlt" % (c["id"], k))
    for origin, p in a["originOutfits"].items():
        if p not in presets:
            err.append("Herkunft %s: Preset %s fehlt" % (origin, p))
    print("Kleidungsstuecke: %d, Presets: %d, Archetypen: %d, Marken: %d" % (len(items), len(presets), len(a["archetypes"]), len(marks)))
    for e in err:
        print("FEHLER:", e)
    print("OK" if not err else "%d Fehler" % len(err))
    return 0 if not err else 1


if __name__ == "__main__":
    sys.exit(validate())
