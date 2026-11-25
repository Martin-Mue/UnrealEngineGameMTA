# Importiert die Fahrzeugklaenge (Content/Audio/Generated/Vehicle/*.wav, erzeugt von
# tools/audio/make_vehicle_sounds.py) als SoundWave-Assets nach /Game/MTA6/Audio/Vehicle und legt
# die 3D-Daempfung ATT_Vehicle (Radius ca. 5000 cm) an.
# Aufruf (ohne Fenster):
#   "F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Projekte\GTA 6\MTA6.uproject"
#       -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_vehicle_sounds.py"
#       -unattended -nosplash -stdout -abslog=F:\UnrealEngine\mta6_audio.log
import os
import unreal

PROJECT = unreal.SystemLibrary.get_project_directory()
SRC = os.path.join(PROJECT, "Content", "Audio", "Generated", "Vehicle")
DEST = "/Game/MTA6/Audio/Vehicle"
ATT_NAME = "ATT_Vehicle"
INNER_RADIUS = 1200.0
OUTER_RADIUS = 5000.0

# Einzelklaenge; alles andere laeuft in Dauerschleife
ONE_SHOTS = ("veh_collision", "veh_door")


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
    """Kugelfoermige Daempfung: innen 1200 cm voll laut (Verfolgerkamera ca. 7,5-10,5 m), bis 5000 cm auf null (natuerlicher Verlauf)."""
    path = DEST + "/" + ATT_NAME
    att = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not att:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = None
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
    log("Daempfung angelegt: %s (Radius %.0f cm)" % (path, OUTER_RADIUS))
    return att


def main():
    if not os.path.isdir(SRC):
        log("Quellordner fehlt: " + SRC)
        return 1
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".wav"))
    if not files:
        log("Keine WAV-Dateien gefunden (erst tools/audio/make_vehicle_sounds.py ausfuehren)")
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
        loop = name not in ONE_SHOTS
        set_prop(asset, "looping", loop)
        # Kurze Loops: gute Qualitaet (Motor wird stark gepitcht); Einzelklaenge etwas staerker komprimiert
        set_prop(asset, "compression_quality", 80 if loop else 60)
        if loop:
            # Stumme Loops (Lautstaerke 0) virtualisieren und beim Hoerbarwerden neu starten
            try:
                asset.set_editor_property("virtualization_mode", unreal.VirtualizationMode.RESTART)
            except Exception as e:
                warn("virtualization_mode bei %s: %s" % (name, e))
        if att:
            set_prop(asset, "attenuation_settings", att)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        try:
            dur = "%.2f s" % asset.get_editor_property("duration")
        except Exception:
            dur = "?"
        log("SoundWave %s loop=%s dauer=%s" % (name, loop, dur))
        ok += 1
    log("Fahrzeugsound importiert: %d von %d, Daempfung %s" % (ok, len(files), "ok" if att else "FEHLT"))
    return 0 if ok == len(files) and att else 1


if __name__ == "__main__":
    raise SystemExit(main())
