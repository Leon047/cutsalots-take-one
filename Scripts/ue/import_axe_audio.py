"""Import the two mono axe impact WAVs as cooked SoundWaves."""
from pathlib import Path

import unreal

folder = "/Game/Vikings/Audio/Axe"
source = Path(unreal.Paths.project_dir()).resolve().parent / "Docs" / "Audio" / "SFX"

for name, filename in (
    ("SW_AxeWood", "axe_wood_take1.wav"),
    ("SW_AxeEnemy", "axe_enemy_take1.wav"),
):
    task = unreal.AssetImportTask()
    task.filename = str(source / filename)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    assets = task.get_objects()
    path = f"{folder}/{name}.{name}"
    if len(assets) != 1 or not isinstance(assets[0], unreal.SoundWave) or assets[0].get_path_name() != path:
        raise RuntimeError(f"Axe audio import failed: {path}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(assets[0], only_if_is_dirty=False):
        raise RuntimeError(f"Axe audio save failed: {path}")
    unreal.log(f"[axe-audio] Imported {path}: {assets[0].get_editor_property('duration'):.2f}s")
