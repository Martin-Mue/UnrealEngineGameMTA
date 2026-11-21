"""Legt aus appearance.json (Schluesselfiguren + Creator-Presets) Daten-Assets vom Typ MTAAppearanceAsset an.

Ausfuehren im Editor: Tools > Execute Python Script... oder
    UnrealEditor-Cmd MTA6.uproject -run=pythonscript -script="tools/ue_characters/create_appearance_assets.py"
Voraussetzung: Modul MTA6 ist kompiliert (die Klasse heisst in Python unreal.MTAAppearanceAsset).
Ziel: /Game/Characters/Appearance/Key und /Game/Characters/Appearance/Presets (DA_<id>).
"""
import json

import unreal

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from common import load_json, resolve_outfit

TARGETS = [("keyCharacters", "/Game/Characters/Appearance/Key"), ("creatorPresets", "/Game/Characters/Appearance/Presets")]


def make_asset(asset_tools, folder, entry, wardrobe):
    name = "DA_" + entry["id"]
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.MTAAppearanceAsset)
        asset = asset_tools.create_asset(name, folder, unreal.MTAAppearanceAsset, factory)
    if not asset:
        unreal.log_error("MTA6: Asset nicht erzeugt: " + path)
        return False
    data = dict(entry)
    data["outfit"] = resolve_outfit(wardrobe, entry["outfitPreset"])
    data.pop("outfitPreset", None)
    if not asset.import_from_json_string(json.dumps(data)):
        unreal.log_error("MTA6: JSON-Import fehlgeschlagen: " + entry["id"])
        return False
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return True


def main():
    appearance = load_json("appearance.json")
    wardrobe = load_json("wardrobe.json")
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    ok = 0
    for key, folder in TARGETS:
        unreal.EditorAssetLibrary.make_directory(folder)
        for entry in appearance[key]:
            ok += 1 if make_asset(asset_tools, folder, entry, wardrobe) else 0
    unreal.log("MTA6: %d Aussehen-Assets angelegt/aktualisiert" % ok)


main()
