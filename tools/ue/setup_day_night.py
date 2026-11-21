# -*- coding: utf-8 -*-
"""MTA 6 - Tag/Nacht im Level Kestrel_Bay einrichten bzw. nachziehen (Unreal Python, UE 5.8).

Die eigentliche Logik steckt im C++-Aktor AMTATimeOfDay (Source/MTA6/MTATimeOfDay.*); dieses Skript
sorgt nur dafuer, dass genau ein solcher Aktor im Level steht und mit Sonne, SkyLight, Nebel,
Stadtgenerator und MPC_MTA6_World verbunden ist. Nuetzlich, wenn build_level.py lief, bevor das
C++-Modul gebaut war. Optional: Umgebungsvariable MTA6_HOUR (z. B. 22) setzt die Vorschau-Uhrzeit.

Start:
  UnrealEditor.exe "E:\\Projekte\\GTA 6\\MTA6.uproject" -ExecutePythonScript="E:/Projekte/GTA 6/tools/ue/setup_day_night.py"
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal  # noqa: E402
from mta_ue_common import MAP_DIR, MPC_PATH, error, info, set_prop, warn  # noqa: E402

LEVEL_PATH = MAP_DIR + "/Kestrel_Bay"


def main():
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not level_sub.load_level(LEVEL_PATH):
            error("Level nicht ladbar: " + LEVEL_PATH)
            return 1
    else:
        warn("Level %s fehlt - arbeite im aktuell geoeffneten Level" % LEVEL_PATH)

    tod_cls = unreal.load_class(None, "/Script/MTA6.MTATimeOfDay")
    if tod_cls is None:
        error("AMTATimeOfDay nicht gefunden - C++-Modul MTA6 zuerst bauen.")
        return 1

    actors = actor_sub.get_all_level_actors()
    tods = [a for a in actors if a.get_class() == tod_cls]
    for extra in tods[1:]:
        actor_sub.destroy_actor(extra)
        warn("Doppelten TimeOfDay-Aktor entfernt")
    tod = tods[0] if tods else actor_sub.spawn_actor_from_class(tod_cls, unreal.Vector(0, 0, 200), unreal.Rotator())
    if tod is None:
        error("TimeOfDay-Aktor konnte nicht erzeugt werden")
        return 1
    tod.set_actor_label("TimeOfDay")
    tod.set_editor_property("tags", [unreal.Name("MTA6_Generated")])

    gen_cls = unreal.load_class(None, "/Script/MTA6.MTACityGenerator")
    for a in actors:
        if isinstance(a, unreal.DirectionalLight):
            set_prop(tod, "sun", a)
        elif isinstance(a, unreal.SkyLight):
            set_prop(tod, "sky_light", a)
        elif isinstance(a, unreal.ExponentialHeightFog):
            set_prop(tod, "height_fog", a)
        elif gen_cls is not None and a.get_class() == gen_cls:
            set_prop(tod, "city_generator", a)

    if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH):
        set_prop(tod, "world_parameters", unreal.EditorAssetLibrary.load_asset(MPC_PATH))
    else:
        warn("MPC_MTA6_World fehlt - build_materials.py ausfuehren")

    hour = os.environ.get("MTA6_HOUR")
    if hour:
        try:
            set_prop(tod, "time_of_day_hours", float(hour) % 24.0)
        except ValueError:
            warn("MTA6_HOUR ungueltig: " + hour)
    try:
        tod.call_method("ApplyTimeOfDay")
    except Exception as exc:  # noqa: BLE001
        warn("ApplyTimeOfDay nicht aufrufbar: %s" % exc)

    if not level_sub.save_current_level():
        error("Level konnte nicht gespeichert werden")
        return 1
    info("Tag/Nacht eingerichtet")
    return 0


if __name__ == "__main__":
    _rc = main()
    if _rc != 0:
        raise SystemExit(_rc)
