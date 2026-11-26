# -*- coding: utf-8 -*-
"""MTA 6 - Level /Game/MTA6/Maps/Kestrel_Bay aufbauen (Unreal Python, UE 5.8).

Inhalt: AMTACityGenerator (Boden-Kacheln, Gehwege, Gebaeude, Parks, Laternen), DirectionalLight (Sonne,
Atmosphaeren-Sonnenlicht), SkyAtmosphere, SkyLight (Echtzeit-Capture), ExponentialHeightFog (volumetrisch),
VolumetricCloud, PostProcessVolume (unbound: Lumen-GI/-Reflexionen, Bloom, Auto-Exposure, leichte
Farbkorrektur), PlayerStart bei (-400, -4000, 200), AMTATimeOfDay, WorldSettings-GameMode AMTA6GameMode,
AMTAWorldDetail (Meer/Kueste/Berge/Landmarken/Moeblierung, Label "WorldDetail", nach der Stadt erzeugt).

Voraussetzungen: C++-Modul MTA6 gebaut (sonst fehlen AMTACityGenerator/AMTATimeOfDay -> Ersatzboden),
vorher build_materials.py ausgefuehrt. Das Skript ist idempotent: alle von ihm erzeugten Aktoren tragen
den Tag "MTA6_Generated" und werden beim erneuten Lauf ersetzt.

Start (Editor mit Fenster empfohlen, LevelEditorSubsystem braucht einen Editor-Kontext):
  "F:\\UnrealEngine\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe" "E:\\Projekte\\GTA 6\\MTA6.uproject"
      -ExecutePythonScript="E:/Projekte/GTA 6/tools/ue/build_level.py"
Alternativ headless:
  UnrealEditor-Cmd.exe "E:\\Projekte\\GTA 6\\MTA6.uproject" -run=pythonscript
      -script="E:/Projekte/GTA 6/tools/ue/build_level.py" -unattended -nosplash -stdout
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal  # noqa: E402
from mta_ue_common import (MAP_DIR, MATERIAL_DIR, enum_value, ensure_directory, error, info,  # noqa: E402
                           set_prop, warn)

LEVEL_PATH = MAP_DIR + "/Kestrel_Bay"
TAG = "MTA6_Generated"
PLAYER_START = unreal.Vector(-400.0, -4000.0, 200.0)

ACTOR_SUB = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVEL_SUB = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR_SUB = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def load_class(path):
    try:
        cls = unreal.load_class(None, path)
    except Exception:  # noqa: BLE001
        cls = None
    if cls is None:
        warn("Klasse nicht gefunden: %s (C++-Modul gebaut?)" % path)
    return cls


def load_material(name):
    path = "%s/%s" % (MATERIAL_DIR, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        warn("Material fehlt: %s (build_materials.py zuerst ausfuehren)" % path)
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def open_or_create_level():
    if not ensure_directory(MAP_DIR):
        return False
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        info("Lade vorhandenes Level " + LEVEL_PATH)
        return LEVEL_SUB.load_level(LEVEL_PATH)
    info("Lege neues Level an: " + LEVEL_PATH)
    # Kein World Partition: die Stadt (640 m) ist klein, ein Generator-Aktor haelt alles.
    return LEVEL_SUB.new_level(LEVEL_PATH, False)


def remove_old_actors():
    removed = 0
    for actor in ACTOR_SUB.get_all_level_actors():
        try:
            tags = [str(t) for t in actor.get_editor_property("tags")]
        except Exception:  # noqa: BLE001
            continue
        if TAG in tags:
            ACTOR_SUB.destroy_actor(actor)
            removed += 1
    if removed:
        info("%d alte MTA6-Aktoren entfernt" % removed)


def spawn(cls, location, rotation=None, label=None):
    if cls is None:
        return None
    actor = ACTOR_SUB.spawn_actor_from_class(cls, location, rotation or unreal.Rotator())
    if actor is None:
        error("Spawn fehlgeschlagen: %s" % cls.get_name())
        return None
    actor.set_editor_property("tags", [unreal.Name(TAG)])
    if label:
        actor.set_actor_label(label)
    info("Gespawnt: %s" % (label or cls.get_name()))
    return actor


def movable(component):
    if component is not None:
        try:
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
        except Exception as exc:  # noqa: BLE001
            warn("Mobility nicht gesetzt: %s" % exc)


def build_lighting():
    # Sonne: Rotation/Intensitaet uebernimmt spaeter AMTATimeOfDay (Startwert 18 Uhr-Licht).
    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), unreal.Rotator(roll=0.0, pitch=-35.0, yaw=0.0), "Sun")
    if sun:
        comp = sun.get_editor_property("light_component")
        movable(comp)
        set_prop(comp, "intensity", 75000.0)          # Lux, physikalisch (Auto-Exposure regelt)
        set_prop(comp, "atmosphere_sun_light", True)  # bAtmosphereSunLight
        set_prop(comp, "cast_shadows", True)
        set_prop(comp, "dynamic_shadow_distance_movable_light", 25000.0, quiet=True)
    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), label="SkyAtmosphere")

    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 1000), label="SkyLight")
    if sky:
        comp = sky.get_editor_property("light_component")
        movable(comp)
        set_prop(comp, "real_time_capture", True)  # nimmt SkyAtmosphere/Wolken mit, kein Recapture noetig
        set_prop(comp, "lower_hemisphere_is_black", False, quiet=True)

    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), label="HeightFog")
    if fog:
        comp = fog.get_editor_property("component")
        set_prop(comp, "fog_density", 0.006)
        set_prop(comp, "fog_height_falloff", 0.2)
        set_prop(comp, "enable_volumetric_fog", True)
        # 5700 XT: volumetrischer Nebel nur bis 60 m Tiefe (Froxel-Kosten), Qualitaet ueber r.VolumetricFog.*.
        set_prop(comp, "volumetric_fog_distance", 6000.0)
        set_prop(comp, "volumetric_fog_scattering_distribution", 0.6)

    spawn(unreal.VolumetricCloud, unreal.Vector(0, 0, 0), label="VolumetricCloud")


def build_post_process():
    ppv = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), label="PostProcess_Global")
    if not ppv:
        return
    set_prop(ppv, "unbound", True)
    s = ppv.get_editor_property("settings")

    def ov(name, value):
        # Jede Einstellung braucht ihr bOverride_-Flag (Python: override_<name>).
        set_prop(s, "override_" + name, True)
        set_prop(s, name, value)

    try:
        ov("dynamic_global_illumination_method", enum_value(unreal.DynamicGlobalIlluminationMethod, "LUMEN", "Lumen"))
        ov("reflection_method", enum_value(unreal.ReflectionMethod, "LUMEN", "Lumen"))
    except AttributeError as exc:
        warn("Lumen-Enums nicht aufloesbar: %s" % exc)
    # Lumen-Qualitaet fuer Software-RT auf RDNA1: 1.0 = Engine-Standard, nicht hoeher.
    ov("lumen_scene_lighting_quality", 1.0)
    ov("lumen_scene_detail", 1.0)
    ov("lumen_scene_view_distance", 20000.0)
    ov("lumen_final_gather_quality", 1.0)
    ov("lumen_max_trace_distance", 20000.0)
    ov("lumen_reflection_quality", 1.0)
    # Auto-Exposure (EV100, da ExtendDefaultLuminanceRange aktiv): Nacht mit Laternen bis Mittag.
    try:
        ov("auto_exposure_method", enum_value(unreal.AutoExposureMethod, "AEM_HISTOGRAM", "AEM_Histogram"))
    except AttributeError as exc:
        warn("AutoExposureMethod nicht aufloesbar: %s" % exc)
    ov("auto_exposure_min_brightness", -2.0)
    ov("auto_exposure_max_brightness", 15.0)
    ov("auto_exposure_bias", 0.0)
    ov("bloom_intensity", 0.25)
    # Leichte Farbkorrektur (Godot: Kontrast 1.08, Saettigung 1.12, ACES).
    ov("color_saturation", unreal.Vector4(1.08, 1.08, 1.08, 1.0))
    ov("color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0))
    ov("vignette_intensity", 0.3)
    set_prop(ppv, "settings", s)


def build_city():
    gen_cls = load_class("/Script/MTA6.MTACityGenerator")
    if gen_cls is None:
        # Ersatzboden, damit das Level wenigstens befahrbar ist.
        warn("AMTACityGenerator fehlt - lege nur einen Ersatzboden an.")
        ground = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -50), label="Ground_Fallback")
        if ground:
            smc = ground.get_editor_property("static_mesh_component")
            smc.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube"))
            ground.set_actor_scale3d(unreal.Vector(4000.0, 4000.0, 1.0))
            mat = load_material("M_MTA_Ground")
            if mat:
                smc.set_material(0, mat)
        return None

    gen = spawn(gen_cls, unreal.Vector(0, 0, 0), label="CityGenerator")
    if gen is None:
        return None
    for prop, mat_name in (("building_material", "M_MTA_Building"), ("ground_material", "M_MTA_Ground"),
                           ("park_material", "M_MTA_Park"), ("lamp_material", "M_MTA_LampEmissive"),
                           ("metal_material", "M_MTA_Metal"), ("tree_trunk_material", "M_MTA_TreeTrunk"),
                           ("foliage_material", "M_MTA_Foliage"), ("roof_material", "M_MTA_Roof")):
        mat = load_material(mat_name)
        if mat:
            set_prop(gen, prop, mat)
    set_prop(gen, "seed", 6)
    set_prop(gen, "generate_ground", True)
    try:
        gen.call_method("Generate")
    except Exception as exc:  # noqa: BLE001
        warn("Generate() nicht aufrufbar (%s) - OnConstruction erzeugt die Stadt beim Laden." % exc)
    return gen


def build_world_detail(gen):
    """AMTAWorldDetail: Meer, Kueste, Berge, Landmarken, Moeblierung, Eingangsstandorte (nach der Stadt)."""
    cls = load_class("/Script/MTA6.MTAWorldDetail")
    if cls is None or gen is None:
        warn("AMTAWorldDetail nicht angelegt (Klasse oder Stadtgenerator fehlt).")
        return None
    detail = spawn(cls, gen.get_actor_location(), label="WorldDetail")
    if detail is None:
        return None
    set_prop(detail, "city", gen)
    for prop, mat_name in (("water_material", "M_MTA_Water"), ("concrete_material", "M_MTA_Concrete"),
                           ("painted_metal_material", "M_MTA_PaintedMetal"), ("neon_material", "M_MTA_Neon"),
                           ("signage_material", "M_MTA_Signage"), ("mountain_material", "M_MTA_Mountain"),
                           ("road_material", "M_MTA_Road"), ("trunk_material", "M_MTA_TreeTrunk"),
                           ("foliage_material", "M_MTA_Foliage")):
        mat = load_material(mat_name)
        if mat:
            set_prop(detail, prop, mat)
    set_prop(detail, "seed", 6)
    try:
        detail.call_method("Generate")
    except Exception as exc:  # noqa: BLE001
        warn("WorldDetail.Generate() nicht aufrufbar (%s) - BeginPlay erzeugt die Details." % exc)
    return detail


def build_time_of_day(gen):
    tod_cls = load_class("/Script/MTA6.MTATimeOfDay")
    if tod_cls is None:
        warn("AMTATimeOfDay fehlt - setup_day_night.py nach dem Build ausfuehren.")
        return
    tod = spawn(tod_cls, unreal.Vector(0, 0, 200), label="TimeOfDay")
    if tod is None:
        return
    for actor in ACTOR_SUB.get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            set_prop(tod, "sun", actor)
        elif isinstance(actor, unreal.SkyLight):
            set_prop(tod, "sky_light", actor)
        elif isinstance(actor, unreal.ExponentialHeightFog):
            set_prop(tod, "height_fog", actor)
    if gen is not None:
        set_prop(tod, "city_generator", gen)
    mpc_path = MATERIAL_DIR + "/MPC_MTA6_World"
    if unreal.EditorAssetLibrary.does_asset_exist(mpc_path):
        set_prop(tod, "world_parameters", unreal.EditorAssetLibrary.load_asset(mpc_path))
    set_prop(tod, "time_of_day_hours", 18.0)


def set_game_mode():
    world = EDITOR_SUB.get_editor_world()
    gm = load_class("/Script/MTA6.MTA6GameMode")
    if world is None or gm is None:
        error("WorldSettings-GameMode nicht gesetzt (Welt oder Klasse fehlt)")
        return False
    ws = world.get_world_settings()
    return set_prop(ws, "default_game_mode", gm)


def main():
    info("build_level.py startet")
    if not open_or_create_level():
        error("Level konnte weder geladen noch angelegt werden: " + LEVEL_PATH)
        return 1
    remove_old_actors()
    build_lighting()
    build_post_process()
    gen = build_city()
    build_world_detail(gen)
    build_time_of_day(gen)
    spawn(unreal.PlayerStart, PLAYER_START, unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0), "PlayerStart")  # Blick nach +Y entlang Strasse X=0
    ok_gm = set_game_mode()
    if not LEVEL_SUB.save_current_level():
        error("Level konnte nicht gespeichert werden")
        return 1
    info("Level gespeichert: " + LEVEL_PATH)
    return 0 if ok_gm else 1


if __name__ == "__main__":
    _rc = main()
    if _rc != 0:
        raise SystemExit(_rc)
