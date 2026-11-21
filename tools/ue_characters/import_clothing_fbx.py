"""Importiert Kleidungs-FBX-Dateien als Skeletal Meshes auf ein gemeinsames Skelett.

Aufruf im Editor (Python-Konsole):
    import import_clothing_fbx as ic
    ic.run(r"D:/Quelle/Clothing", "/Game/Characters/Skeletons/SK_MetaHuman_Body_Skeleton")
FBX-Dateinamen: <slot>__<itemId>.fbx, z. B. jacket__jkt_denim.fbx  ->  /Game/Characters/Clothing/jacket/SK_jkt_denim
Danach assign_clothing_meshes.py ausfuehren.
"""
import os

import unreal

DEST_ROOT = "/Game/Characters/Clothing"


def run(source_dir, skeleton_path):
    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if not skeleton:
        unreal.log_error("MTA6: Skelett nicht gefunden: " + skeleton_path)
        return
    tasks = []
    for fname in sorted(os.listdir(source_dir)):
        if not fname.lower().endswith(".fbx") or "__" not in fname:
            continue
        slot, item = os.path.splitext(fname)[0].split("__", 1)
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("import_materials", True)
        ui.set_editor_property("import_animations", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.set_editor_property("skeleton", skeleton)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(source_dir, fname))
        task.set_editor_property("destination_path", DEST_ROOT + "/" + slot)
        task.set_editor_property("destination_name", "SK_" + item)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("options", ui)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    unreal.log("MTA6: %d Kleidungs-FBX importiert" % len(tasks))
