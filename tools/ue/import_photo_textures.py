# -*- coding: utf-8 -*-
"""Importiert die CC0-Fototexturen (ambientCG, 1K JPG) aus tools/photo_textures als Texture2D-Assets.

Ziel: /Game/MTA6/Textures/Photo/<Id>/T_<Id>_{BC,N,R,AO}
  BC  Color             sRGB an,  TC_Default
  N   NormalDX          sRGB aus, TC_Normalmap (DirectX-Konvention wie UE, kein Green-Flip)
  R   Roughness         sRGB aus, TC_Masks
  AO  AmbientOcclusion  sRGB aus, TC_Masks (nur wenn im Paket vorhanden)
Alle: Wrap/Wrap, Mips aus der Texturgruppe, Max Texture Size 1024.

Quellen fehlen? Erst tools/fetch_photo_textures.ps1 ausfuehren.
Aufruf (ohne Fenster):
  UnrealEditor-Cmd.exe "E:/Projekte/GTA 6/MTA6.uproject" -run=pythonscript
      -script="E:/Projekte/GTA 6/tools/ue/import_photo_textures.py" -unattended -nosplash -stdout
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal  # noqa: E402
from mta_ue_common import enum_value, ensure_directory, error, info, set_prop, warn  # noqa: E402

PROJECT = unreal.SystemLibrary.get_project_directory()
SRC = os.path.join(PROJECT, "tools", "photo_textures")
DEST = "/Game/MTA6/Textures/Photo"
MAX_SIZE = 1024

# Pakete und benoetigte Karten (Metalness wird nicht verwendet).
PACKS = ["Asphalt031", "Concrete034", "Rock064", "Grass004", "Ground037", "Bricks097", "Metal064"]
MAPS = [  # (Suffix im Dateinamen, Asset-Suffix, Art)
    ("Color", "BC", "color"),
    ("NormalDX", "N", "normal"),
    ("Roughness", "R", "mask"),
    ("AmbientOcclusion", "AO", "mask"),
]


def configure(tex, kind):
    tc = unreal.TextureCompressionSettings
    if kind == "color":
        set_prop(tex, "srgb", True)
        set_prop(tex, "compression_settings", enum_value(tc, "TC_DEFAULT", "TC_Default"))
        set_prop(tex, "lod_group", enum_value(unreal.TextureGroup, "TEXTUREGROUP_WORLD", "TEXTUREGROUP_World"))
    elif kind == "normal":
        set_prop(tex, "srgb", False)
        set_prop(tex, "compression_settings", enum_value(tc, "TC_NORMALMAP", "TC_Normalmap"))
        set_prop(tex, "flip_green_channel", False)
        set_prop(tex, "lod_group", enum_value(unreal.TextureGroup, "TEXTUREGROUP_WORLD_NORMAL_MAP",
                                              "TEXTUREGROUP_WorldNormalMap"))
    else:
        set_prop(tex, "srgb", False)
        set_prop(tex, "compression_settings", enum_value(tc, "TC_MASKS", "TC_Masks"))
        set_prop(tex, "lod_group", enum_value(unreal.TextureGroup, "TEXTUREGROUP_WORLD", "TEXTUREGROUP_World"))
    set_prop(tex, "mip_gen_settings", enum_value(unreal.TextureMipGenSettings, "TMGS_FROM_TEXTURE_GROUP",
                                                 "TMGS_FromTextureGroup"))
    wrap = enum_value(unreal.TextureAddress, "TA_WRAP", "TA_Wrap")
    set_prop(tex, "address_x", wrap)
    set_prop(tex, "address_y", wrap)
    set_prop(tex, "max_texture_size", MAX_SIZE)


def main():
    if not os.path.isdir(SRC):
        error("Quellordner fehlt: %s (tools/fetch_photo_textures.ps1 ausfuehren)" % SRC)
        return 1
    if not ensure_directory(DEST):
        return 1

    jobs = []  # (Asset-Pfad, Art)
    tasks = []
    for pack in PACKS:
        folder = os.path.join(SRC, pack)
        if not os.path.isdir(folder):
            error("Paket fehlt: " + folder)
            continue
        dest_dir = DEST + "/" + pack
        ensure_directory(dest_dir)
        for suffix, short, kind in MAPS:
            src = os.path.join(folder, "%s_1K-JPG_%s.jpg" % (pack, suffix))
            if not os.path.isfile(src):
                if short == "AO":
                    info("%s: keine AO-Karte im Paket (uebersprungen)" % pack)
                else:
                    error("Datei fehlt: " + src)
                continue
            name = "T_%s_%s" % (pack, short)
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", src)
            t.set_editor_property("destination_path", dest_dir)
            t.set_editor_property("destination_name", name)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            tasks.append(t)
            jobs.append((dest_dir + "/" + name, kind))

    if not tasks:
        error("Keine Texturen zum Importieren gefunden")
        return 1
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    ok = 0
    for path, kind in jobs:
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            error("Import fehlgeschlagen: " + path)
            continue
        configure(tex, kind)
        if unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False):
            ok += 1
            info("Textur importiert: %s (%s)" % (path, kind))
        else:
            error("Speichern fehlgeschlagen: " + path)
    info("Fototexturen importiert: %d von %d" % (ok, len(jobs)))
    if ok != len(jobs):
        warn("Nicht alle Texturen importiert")
        return 1
    return 0


if __name__ == "__main__":
    _rc = main()
    if _rc != 0:
        raise SystemExit(_rc)
