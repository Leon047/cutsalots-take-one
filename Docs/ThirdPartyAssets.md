# Third-party assets and credits

## Unreal Engine

Cutsalots: Take One uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

The Windows distribution includes the engine's CEF/Chromium, Droid Sans, and Noto license files alongside these credits.

## Asset packs

- [Ultra Dynamic Sky](https://www.fab.com/listings/84fda27a-c79f-49c9-8458-82401fb37cfb) by Everett Gunther. Sky and lighting in `Map_Saga`.

The Megascans assets were imported at the medium quality tier. The game applies its own palette.

- [Nordic Forest Ledge Rock Medium](https://www.fab.com/listings/034fcd08-2e37-407f-9e32-26291a8d18a3) by Quixel Megascans. Mountain slopes.
- [Nordic Forest Cluster Rock Small](https://www.fab.com/listings/f186c225-178a-482b-a122-5508a318a4f0) by Quixel Megascans. Riverbanks.
- [Nordic Forest Tree Stump Birch Medium](https://www.fab.com/listings/140c2265-c446-4080-8f03-dd58299c3f8c) by Quixel Megascans. Ashen forest stumps.
- [Coast African Debris Driftwood S 03](https://www.fab.com/listings/34a31ebb-9cda-484e-8a69-e8ea263b3678) by Quixel Megascans. Burned dock debris.
- [Beach Gravel With Pebbles](https://www.fab.com/listings/8fbcf4f7-a969-46f5-b786-acd5aad0054a) by Quixel Megascans. Shoreline surface.
- [Mossy Ground](https://www.fab.com/listings/aa33d733-44e7-4a7e-945b-be3c443afc18) by Quixel Megascans. The 2K base color and normal add detail to the shared forest and colony ground.
- [Watermills / Nature Environment](https://www.fab.com/listings/a2ab9a3c-4862-421a-8f58-47463695c6ba) by Leartes Studios. Shoreline grass and ferns with Saga materials.
- [Ultimate Animated Animal Pack](https://quaternius.com/packs/ultimateanimatedanimals.html) by Quaternius, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). Unreal's import metadata for the Bull mesh and all 12 original animation clips identifies `Ultimate Animated Animal Pack/FBX/Bull.fbx` as their source. The boar and the three night-creature mushrooms use retargeted Bull animations.
- [Ultimate Animated Character Pack](https://quaternius.com/packs/ultimatedanimatedcharacter.html) by Quaternius, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). The Viking male, female, and lord meshes and their imported animations name `Viking_Male.fbx`, `Viking_Female.fbx`, and `VikingHelmet.fbx` respectively. All three filenames match the creator's FBX inventory. The Viking male clips also supply source animations for the axolotls.
- [Ultimate RPG Pack](https://quaternius.com/packs/ultimaterpg.html) by Quaternius, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). The arrow's import record names this pack and `FBX/Arrow.fbx`. The axe and original bow records match `Axe_small.obj`, `Bow_Wooden.obj`, and the bow's companion `Bow_Wooden.mtl` in the creator's OBJ inventory. The sword's `Sword.obj` also matches this pack and was imported from the same local OBJ folder as the axe and original bow.
- [Modular Weapons Pack](https://quaternius.com/packs/medievalweapons.html) by Quaternius, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). The round shield's import filename `Shield_Round.fbx` matches the creator's FBX inventory.

## Other credited content

- **Roboto** by Google. The HUD uses the Roboto font bundled with Unreal Engine. See the [bundled Roboto license](Roboto-LICENSE.txt).
- **Tripo**. The Viking house kit includes Tripo-generated meshes alongside team-built Blender modules. See the [module inventory](VikingHouseKit.md). The Cutsalot warrior villager (`Content/Characters/axolotl_warrior`) is a Tripo-generated mesh, texture set and rig, made from the team's reference art in `Docs/References/FurryAxolotlClasses` with a T-pose sheet drawn in ChatGPT. The archer, druid, blacksmith and jarl heroes (`Content/Characters/axolotl_archer`, `axolotl_druid`, `axolotl_blacksmith`, `axolotl_jarl`) are made the same way, each from the team's reference art with a T-pose sheet drawn in ChatGPT. The three raider variants (`Content/Characters/axolotl_frog`, `axolotl_bird`, `axolotl_amphibian`) are also Tripo-generated meshes, texture sets and rigs, made from the team's T-pose reference art in `Reference/EnemyTPoses`. Their animations are retargeted from the Viking character clips. The three night-creature mushrooms (`Content/Characters/mushroom_purple`, `mushroom_cup`, `mushroom_morel`) are Tripo-generated meshes and texture sets made from the team's reference images in `mushroom-chars`; they are skinned in Blender to a copy of the Bull skeleton (their Tripo rig is not used) and animated with the Bull clips retargeted onto them.
- **Tripo** also generated the boar mesh and textures (`Content/Animals/Boar`) from the ChatGPT reference in `Reference/boar-tripo-input.png`. The boar was skinned to a fitted copy of the Bull skeleton in Blender and uses retargeted Bull animations.
- **ElevenLabs**. The repository includes generation scripts for the soundtrack and environmental sound effects.

## Provenance to confirm

- The current bow mesh records `Bow_Wooden.FBX`. Both Quaternius weapon packs list that basename, and the original OBJ import evidence applies to the earlier bow asset and its materials. The source of the current FBX remains unverified.
- The staff was imported from `FBX/Staff.fbx` in a generic download folder. Quaternius publishes a [CC0 staff model](https://poly.pizza/m/PnGRvO4Lwd) with the same name, but the game mesh and its original pack remain unverified.
- Imported props and pine variants have Tripo references in package data. Those references alone do not establish the source of each mesh, texture, or embedded audio clip.
- The original account entitlements and license versions for marketplace assets are not recorded here. The listing links identify the products. Acquisition records establish the terms for each copy.
