"""Gemeinsame Helfer fuer die MTA-6-Figuren-Werkzeuge (Unreal-Python, aber ohne unreal-Import nutzbar)."""
import json
import os

SLOTS = ["hat", "glasses", "mask", "neck", "top", "jacket", "fullbody", "bottom", "gloves", "shoes", "bag"]


def project_dir():
    """Projektordner: im Editor ueber unreal, sonst relativ zu dieser Datei (tools/ue_characters -> Projekt)."""
    try:
        import unreal  # noqa: F401
        return unreal.Paths.project_dir()
    except ImportError:
        return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def data_path(name):
    return os.path.join(project_dir(), "Content", "Data", name)


def load_json(name):
    with open(data_path(name), "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(name, obj):
    with open(data_path(name), "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)
        f.write("\n")


def resolve_outfit(wardrobe, preset_id):
    """Deterministisches Outfit aus einem Preset: erstes Item jedes Slots mit Chance >= 1."""
    presets = {p["id"]: p for p in wardrobe["presets"]}
    preset = presets.get(preset_id)
    outfit = {}
    if not preset:
        return outfit
    blocked = set()
    items = {i["id"]: i for i in wardrobe["items"]}
    order = ["fullbody"] + [s for s in SLOTS if s != "fullbody"]
    for slot in order:
        rule = preset["slots"].get(slot)
        if not rule or not rule["items"] or rule.get("chance", 1) < 1 or slot in blocked:
            continue
        item = items[rule["items"][0]]
        outfit[slot] = item["id"]
        blocks = item.get("blocks") or (wardrobe["rules"]["fullbodyDefaultBlocks"] if slot == "fullbody" else [])
        blocked.update(blocks)
    return outfit
