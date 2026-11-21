# -*- coding: utf-8 -*-
"""Gemeinsame Hilfsfunktionen fuer die MTA-6-Editor-Skripte (Unreal Python, UE 5.8).

Wird von build_materials.py, build_level.py und setup_day_night.py per sys.path importiert.
Alle Funktionen loggen ueber unreal.log*, damit die Ausgaben im Editor-/Commandlet-Log landen.
"""
import unreal

MATERIAL_DIR = "/Game/MTA6/Materials"
MAP_DIR = "/Game/MTA6/Maps"
MPC_PATH = MATERIAL_DIR + "/MPC_MTA6_World"

_PREFIX = "[MTA6] "


def info(msg):
    unreal.log(_PREFIX + str(msg))


def warn(msg):
    unreal.log_warning(_PREFIX + str(msg))


def error(msg):
    unreal.log_error(_PREFIX + str(msg))


def enum_value(enum_cls, *candidates):
    """Enum-Wert robust aufloesen.

    Die Python-Namen werden aus den C++-Namen generiert (z. B. MATUSAGE_InstancedStaticMeshes ->
    MATUSAGE_INSTANCED_STATIC_MESHES). Wir probieren die Kandidaten und vergleichen zuletzt
    ohne Unterstriche/Gross-Klein-Schreibung, statt einen Namen zu raten.
    """
    for name in candidates:
        if hasattr(enum_cls, name):
            return getattr(enum_cls, name)
    wanted = [c.replace("_", "").lower() for c in candidates]
    for attr in dir(enum_cls):
        if attr.startswith("_"):
            continue
        norm = attr.replace("_", "").lower()
        if norm in wanted:
            return getattr(enum_cls, attr)
    raise AttributeError("Enum-Wert %s nicht in %s gefunden" % (candidates, enum_cls))


def set_prop(obj, name, value, quiet=False):
    """set_editor_property mit Fehlerbehandlung (loest Pre/PostEditChange aus)."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:  # noqa: BLE001 - API-Unterschiede sollen das Skript nicht abbrechen
        if not quiet:
            warn("Eigenschaft '%s' an %s nicht gesetzt: %s" % (name, obj.get_name() if hasattr(obj, "get_name") else obj, exc))
        return False


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        if unreal.EditorAssetLibrary.make_directory(path):
            info("Ordner angelegt: " + path)
        else:
            error("Ordner konnte nicht angelegt werden: " + path)
            return False
    return True


def load_or_create_asset(name, package_dir, asset_class, factory):
    """Vorhandenes Asset laden oder neu anlegen. Gibt (asset, created) zurueck."""
    full = package_dir + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        asset = unreal.EditorAssetLibrary.load_asset(full)
        if asset is not None and isinstance(asset, asset_class):
            return asset, False
        error("Asset %s existiert, hat aber den falschen Typ (%s)" % (full, type(asset).__name__))
        return None, False
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = tools.create_asset(name, package_dir, asset_class, factory)
    if asset is None:
        error("Asset konnte nicht erzeugt werden: " + full)
        return None, False
    info("Asset angelegt: " + full)
    return asset, True


def save_asset(asset):
    path = asset.get_path_name()
    ok = unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    if ok:
        info("Gespeichert: " + path)
    else:
        error("Speichern fehlgeschlagen: " + path)
    return ok
