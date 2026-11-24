# Importiert die erzeugten WAV-Dateien (Content/Audio/Generated) als SoundWave-Assets nach /Game/Audio/Generated.
# Aufruf (ohne Fenster):
#   UnrealEditor-Cmd.exe MTA6.uproject -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue_audio/import_audio.py" -unattended -nosplash -stdout
import os
import unreal

PROJECT = unreal.SystemLibrary.get_project_directory()
SRC = os.path.join(PROJECT, "Content", "Audio", "Generated")
DEST = "/Game/Audio/Generated"

def log(msg):
    unreal.log("[MTA6] " + msg)

def main():
    if not os.path.isdir(SRC):
        log("Quellordner fehlt: " + SRC)
        return 1
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".wav"))
    if not files:
        log("Keine WAV-Dateien gefunden (erst tools/audio/gen_all.py ausfuehren)")
        return 1
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
        # Atmosphaeren und Radio-Schleifen laufen in Dauerschleife; Donner ist ein Einzelklang
        if name != "amb_thunder":
            try:
                asset.set_editor_property("looping", True)
            except Exception as e:
                log("looping nicht setzbar bei %s: %s" % (name, e))
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        ok += 1
    log("Audio importiert: %d von %d" % (ok, len(files)))
    return 0 if ok == len(files) else 1

if __name__ == "__main__":
    raise SystemExit(main())
