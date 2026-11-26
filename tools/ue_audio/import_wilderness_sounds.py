# Importiert die Wildnis-Klaenge (Content/Audio/Generated/Wilderness/*.wav, erzeugt von
# tools/audio/make_waterfall_sound.py) als SoundWave-Assets nach /Game/MTA6/Audio/Wilderness und legt
# die 3D-Daempfung ATT_Waterfall (innen 15 m voll, bis 400 m hoerbar) an.
# AMTAWildernessBuilder laedt /Game/MTA6/Audio/Wilderness/amb_waterfall (Rueckfall: /Game/Audio/Generated/amb_rain).
# Aufruf (ohne Fenster):
#   "F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Projekte\GTA 6\MTA6.uproject"
#       -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_wilderness_sounds.py"
#       -unattended -nosplash -stdout -abslog=F:\UnrealEngine\mta6_wild_audio.log
import os
import unreal

PROJECT = unreal.SystemLibrary.get_project_directory()
SRC = os.path.join(PROJECT, "Content", "Audio", "Generated", "Wilderness")
DEST = "/Game/MTA6/Audio/Wilderness"
ATT_NAME = "ATT_Waterfall"
INNER_RADIUS = 1500.0
OUTER_RADIUS = 40000.0


def log(msg):
    unreal.log("[MTA6] " + msg)


def warn(msg):
    unreal.log_warning("[MTA6] " + msg)


def set_prop(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        warn("%s nicht setzbar bei %s: %s" % (prop, obj.get_name(), e))
        return False


def make_attenuation():
    path = DEST + "/" + ATT_NAME
    att = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not att:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        try:
            factory = unreal.SoundAttenuationFactory()
        except Exception:
            factory = None
        att = tools.create_asset(ATT_NAME, DEST, unreal.SoundAttenuation, factory)
    if not att:
        warn("SoundAttenuation konnte nicht angelegt werden")
        return None
    s = att.get_editor_property("attenuation")
    set_prop(s, "attenuate", True)
    set_prop(s, "spatialize", True)
    try:
        s.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    except Exception as e:
        warn("attenuation_shape: %s" % e)
    set_prop(s, "attenuation_shape_extents", unreal.Vector(INNER_RADIUS, 0.0, 0.0))
    set_prop(s, "falloff_distance", OUTER_RADIUS - INNER_RADIUS)
    try:
        s.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    except Exception as e:
        warn("distance_algorithm: %s" % e)
    set_prop(att, "attenuation", s)
    unreal.EditorAssetLibrary.save_loaded_asset(att)
    log("Daempfung angelegt: %s" % path)
    return att


def main():
    if not os.path.isdir(SRC):
        log("Quellordner fehlt: " + SRC + " (erst tools/audio/make_waterfall_sound.py ausfuehren)")
        return 1
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".wav"))
    if not files:
        log("Keine WAV-Dateien gefunden")
        return 1
    unreal.EditorAssetLibrary.make_directory(DEST)
    att = make_attenuation()
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(SRC, f))
        t.set_editor_property("destination_path", DEST)
        t.set_editor_property("destination_name", os.path.splitext(f)[0])
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    ok = 0
    for f in files:
        name = os.path.splitext(f)[0]
        asset = unreal.EditorAssetLibrary.load_asset(DEST + "/" + name)
        if not asset:
            log("Import fehlgeschlagen: " + name)
            continue
        set_prop(asset, "looping", True)
        set_prop(asset, "compression_quality", 70)
        try:
            asset.set_editor_property("virtualization_mode", unreal.VirtualizationMode.RESTART)
        except Exception as e:
            warn("virtualization_mode bei %s: %s" % (name, e))
        if att:
            set_prop(asset, "attenuation_settings", att)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        log("SoundWave %s importiert (Loop)" % name)
        ok += 1
    log("Wildnis-Audio importiert: %d von %d" % (ok, len(files)))
    return 0 if ok == len(files) and att else 1


if __name__ == "__main__":
    raise SystemExit(main())
