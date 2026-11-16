"""Import the boar's crop-trampling escape call as a SoundWave."""
from pathlib import Path

import unreal

folder = "/Game/Vikings/Audio/Boar"
name = "SW_BoarFlee"
source = Path(unreal.Paths.project_dir()).resolve().parent / "Docs" / "Audio" / "SFX"

task = unreal.AssetImportTask()
task.filename = str(source / "boar_grunt_deep_pig_squeal.wav")
task.destination_path = folder
task.destination_name = name
task.automated = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

assets = task.get_objects()
path = f"{folder}/{name}.{name}"
if len(assets) != 1 or not isinstance(assets[0], unreal.SoundWave) or assets[0].get_path_name() != path:
    raise RuntimeError(f"Boar audio import failed: {path}")
if not unreal.EditorAssetLibrary.save_loaded_asset(assets[0], only_if_is_dirty=False):
    raise RuntimeError(f"Boar audio save failed: {path}")
unreal.log(f"[boar-audio] Imported {path}: {assets[0].get_editor_property('duration'):.2f}s")
