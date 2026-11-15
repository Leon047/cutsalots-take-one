# Import the huskies' one-shot sound effects into the cooked Vikings content.
import os
from pathlib import Path

import unreal

folder = "/Game/Vikings/Audio/Dog"
source = Path(unreal.Paths.project_dir()).resolve().parent / "Docs" / "Audio" / "SFX"
clips = {
    "SW_DogBark_1": "dog_bark_take1.wav",
    "SW_DogBark_2": "dog_bark_take2.wav",
    "SW_DogPant": "dog_pant_take1.wav",
    "SW_DogHowl_TakeC": "dog_howl_shaky_take1.wav",
    "SW_DogAttackBark": "dog_attack_bark_take1.wav",
    "SW_DogGrowl": "dog_growl_take1.wav",
}
# Set TSC_DOG_AUDIO_ASSETS to a comma-separated list when updating only some clips.
selection = os.environ.get("TSC_DOG_AUDIO_ASSETS")
if selection:
    clips = {name.strip(): clips[name.strip()] for name in selection.split(",")}

tasks = []
for name, wav in clips.items():
    path = source / wav
    if not path.is_file():
        raise FileNotFoundError(path)
    task = unreal.AssetImportTask()
    task.filename = str(path)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for name, task in zip(clips, tasks):
    path = f"{folder}/{name}.{name}"
    imported = task.get_objects()
    if len(imported) != 1 or not isinstance(imported[0], unreal.SoundWave) or imported[0].get_path_name() != path:
        raise RuntimeError(f"Dog audio import failed: {path}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(imported[0], only_if_is_dirty=False):
        raise RuntimeError(f"Dog audio save failed: {path}")
    unreal.log(f"[dog-audio] Imported {path}: {imported[0].get_editor_property('duration'):.2f}s")
