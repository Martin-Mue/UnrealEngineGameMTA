"""Ordnet vorhandene Skeletal Meshes den Kleidungsstuecken in wardrobe.json zu.

Konvention: /Game/Characters/Clothing/<Slot>/SK_<ItemId>   (z. B. /Game/Characters/Clothing/jacket/SK_jkt_denim)
Das Skript sucht rekursiv unter /Game/Characters/Clothing nach Assets namens SK_<ItemId> und
schreibt den Objektpfad ins Feld "mesh". Mit DRY_RUN = True wird nur berichtet.
"""
import unreal

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from common import load_json, save_json

ROOT = "/Game/Characters/Clothing"
DRY_RUN = False


def main():
    wardrobe = load_json("wardrobe.json")
    found = {}
    for obj_path in unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False):
        name = obj_path.split("/")[-1].split(".")[0]
        if name.startswith("SK_"):
            found[name[3:]] = obj_path
    assigned = missing = 0
    for item in wardrobe["items"]:
        path = found.get(item["id"])
        if path:
            if item.get("mesh") != path:
                item["mesh"] = path
            assigned += 1
        else:
            missing += 1
    unreal.log("MTA6: %d Kleidungsstuecke mit Mesh, %d ohne (Platzhalter)" % (assigned, missing))
    if not DRY_RUN:
        save_json("wardrobe.json", wardrobe)


main()
