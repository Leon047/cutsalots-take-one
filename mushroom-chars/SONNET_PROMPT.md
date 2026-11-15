# Task: three four-legged mushroom enemies, from reference image to playable unit

## Goal

Turn the three reference images in `mushroom-chars/` into rigged, textured, animated skeletal meshes and make the
night enemy (`ETSCRole::Creature`) use them. They are **quadrupeds**: four legs, no arms, no head, no weapon. They walk
and run like an animal and attack by **ramming the target with the body**. Their animations come from the animal clips
that are already in the project, retargeted onto the new skeletons.

| Key | Reference | Shape | Front of the creature |
|---|---|---|---|
| `purple` | `mushroom-chars/purple-mushroom.png` | tall ribbed purple cap like a hood, four short stubby legs under it | the side with the two glowing eyes |
| `cup` | `mushroom-chars/orange-cup-fungus.png` | wide orange bowl on four wavy legs | no clear front: pick the side facing the camera in the image |
| `morel` | `mushroom-chars/golden-morel.png` | honeycomb morel body full of through-holes, leaning cone tip, four arched legs | the direction the cone tip leans to (it rams with the tip) |

`<Key>` below is `key.capitalize()`.

## What already exists (read before you start)

- `.claude/skills/tripo-character/SKILL.md` — the pipeline that made the axolotl characters: ChatGPT T-pose sheet,
  Tripo Studio model + auto-rig, Blender clean-up, Unreal import / retarget / sockets, C++ routing, PIE review. It is
  **humanoid only** (Mixamo rig, Viking source clips, hand sockets). Use it as the pattern for tools, settings, checks
  and style of work. Do not invoke it as is and do not change it or its scripts in a way that breaks the axolotls.
- `Scripts/blender/prep_axolotl.py`, `Scripts/ue/import_axolotl.py`, `Scripts/ue/retarget_axolotl.py` — the humanoid
  scripts. Write quadruped versions next to them (`prep_quadruped.py`, `import_quadruped.py`,
  `retarget_quadruped.py`), copying what fits. No sockets script: these creatures hold nothing.
- `UE/Content/Animals/<Name>/` — seven animals (Bull, Cow, Deer, Stag, Fox, Wolf, Husky), each with its own skeleton
  and clips named `<Name>AnimalArmature_<Clip>`: `Idle`, `Walk`, `Gallop`, `Attack_Headbutt` (Bull, Cow, Deer, Stag)
  or `Attack` (Fox, Wolf, Husky), `Idle_HitReact_Left`, `Death`, `Eating`.
- `UE/Source/TheSagaCutter/TSCUnit.cpp` — `BuildAnimal()` (about line 455) loads an animal mesh, fits it by length,
  puts the feet on the ground and maps the clips onto the shared `ETSCAnim` slots. `BuildFigure()` (about line 196)
  builds the current Creature from primitives: a pale cap with glowing spots and tendrils. Lines near 1097, 1112 and
  1273 have Creature-only behaviour (death timing, tendril motion). Read all of these.
- The editor is driven through the `ue-editor` skill and the unreal-mcp tools. Tripo Studio runs in the user's
  signed-in Chrome (Claude in Chrome). Blender is at `C:\Tools\blender-5.2.1-windows-x64\blender.exe`.

## Decisions already made

- **Retarget source: the Bull** (`/Game/Animals/Bull`). It has a heavy body on short legs like these mushrooms, and
  `Attack_Headbutt` is the closest clip to a body ram. If the Bull gives a clearly worse result than the Cow or Stag
  on the contact sheet, switch and say so.
- **Clips to make, per character:** `Idle`, `Walk`, `Gallop`, `Attack_Headbutt`, `Idle_HitReact_Left`, `Death`.
- **Names:** `Art/Tripo/mushroom_<key>/` (export in `rigged/<key>_rigged.fbx`, cleaned `SK_Mushroom<Key>.fbx`,
  textures `T_Mushroom<Key>_*.jpg`, `<key>_bones.json`, `check_*.png`), UE folder `/Game/Characters/mushroom_<key>`,
  mesh `Mushroom_<Key>`, material `M_Mushroom<Key>`, IK rig `IK_Mushroom_<Key>`, retargeter
  `RTG_Bull_To_Mushroom_<Key>`, clips `Mushroom_<Key>AnimalArmature_<Clip>`.
- **Game use:** each spawned Creature picks one of the three models (same way the axolotl variants `AxolotlFrog /
  AxolotlBird / AxolotlAmphibian` are picked in `BuildViking`). When a package is missing, the old primitive figure
  stays as the fallback.
- **Credits:** one clean pass costs 75 per character (55 generate + 20 rig), 225 in total. You may spend up to
  **300** Tripo credits without asking. Ask before any spend beyond that.
- **Order:** do `purple` first, from image to the retarget contact sheet in Unreal (steps 1-4). It has the simplest
  shape. Only when its clips look right, generate `cup` and `morel`. This keeps a wrong approach from costing three
  generations.

## Steps

### 1. Input image
Try the reference image as the Tripo input first, unchanged: all three already stand in a neutral pose with four
separate legs fully visible. Make a cleaned image in ChatGPT (new chat in Chrome, attach the reference) only if the
Tripo model comes out wrong (fused legs, missing leg, flat back). In that case ask for: the same creature, same
colours and surface, 3/4 front view, standing still on four separate legs spread apart, plain light grey background,
flat even light, no ground shadow, no scenery, whole figure in frame. Save it to
`mushroom-chars/tripo_input/<key>.png` and look at it before going on.

### 2. Model and rig in Tripo Studio
Same model settings as the skill: HD Model, single image, H3.1, Ultra Mesh, Texture 4K, Remove Lighting, PBR,
Triangle, polycount 30000. Open the result and check: four separate legs, nothing fused, about 30k faces, for the
morel the holes go through, for the purple the eyes are there.

Rig: in the Rig tool pick the **quadruped** type, not Humanoid. I do not know the exact label or bone names of
Tripo's quadruped rig: look at what the UI offers and report what you chose. Then export FBX for Blender, 4K, with
skeleton, and place it under `Art/Tripo/mushroom_<key>/rigged/`
(`.claude/skills/tripo-character/scripts/place_tripo_export.sh` shows how the axolotl exports are placed; it writes
to `axolotl_<key>`, so copy its logic, do not point it at the axolotl folders).

**Fallback if Tripo has no usable quadruped rig, or its auto-rig fails on these shapes** (likely for the morel with
its holes, possible for the purple with legs hidden under the cap): do not rig in Tripo and do not pay the 20
credits. Export the unrigged model, export the Bull skeleton from Unreal, and in Blender fit a copy of the Bull
armature to the mushroom (scale it, move the leg bones into the four legs, keep bone names and hierarchy), then skin
with automatic weights and fix the legs by hand-written weight rules if needed. The mesh then uses a Bull-compatible
skeleton and the retarget in step 4 becomes a one-to-one chain mapping.

### 3. Blender clean-up (`Scripts/blender/prep_quadruped.py`)
Headless, same call form as `prep_axolotl.py`. It must: strip bone-name prefixes, apply scale and rotation so the
model is in metres with Z up and the creature's **front** (see the table) on the same axis the axolotl script uses,
put the lowest foot at z = 0, check for unweighted vertices, write `<key>_bones.json` (bone list plus roles: root,
spine chain, the four leg chains named by front/back and left/right), export `SK_Mushroom<Key>.fbx` and the
textures, and render check stills: front, side, back, 3/4, and two posed stills that bend each leg so wrong weights
show. Must exit 0. Look at every still.

Check on the posed stills: each leg bends only its own leg; the cap / bowl / cone does not stretch with a leg; on
the purple the cap skirt does not tear where it hangs over the legs.

### 4. Unreal: import and retarget
`import_quadruped.py`: import mesh, skeleton, textures, build the material (follow `import_axolotl.py`; give the
purple's eyes emissive if the base colour alone does not glow at night). `retarget_quadruped.py`: build IK rigs for
the Bull and for the mushroom, with chains for spine and four legs, a retargeter, and the six clips. The mushrooms
have no neck, head or tail: leave those Bull chains unmapped, or map head / neck to the front of the spine when
that makes the ram read better. Rerunnable like the axolotl scripts (delete and rebuild own assets, nothing else).
Do not save `Map_Saga`.

Verify **by animating**, never by the rest pose. `capture_clips.py`, `contact_sheet.py` and `measure_soles.py` in
`.claude/skills/tripo-character/scripts/` do this for the axolotls; they assume axolotl names and two feet, so
copy and adapt them (keep the copies in `Scripts/ue/` or the scratchpad, your choice, say which). Save the sheet
to `Art/Tripo/mushroom_<key>/check_retarget_sheet.png`.

Accept when: all bones have scale 1; six clips with the source lengths; in Idle all four soles within ±2 cm of the
ground; in Walk and Gallop the legs step in a believable four-legged pattern and no leg crosses through the body;
the body stays whole in every frame; `Death` ends lying or collapsed, not floating; `Attack_Headbutt` shows a clear
forward lunge of the body.

### 5. C++ and the body-ram attack
Route `ETSCRole::Creature` to the new models. Reuse `BuildAnimal()` rather than writing a second copy: extend the
model table or give it a folder and prefix so it can load from `/Game/Characters/mushroom_<key>`. Map the clips to
the same `ETSCAnim` slots the animals use (attack on `Slash`). Size: fit so the tallest one (`purple`) stands about
130-150 cm and the others keep their proportions to it; read the Creature's attack range and target height in the
code and keep them consistent with the new size. Check that the Creature-only code paths (tendril motion, 0.7 s
death removal) still make sense with a skeletal mesh: the `Death` clip must be visible before the unit is removed.

The attack must read as a ram at game camera distance. If the retargeted headbutt alone is too small (the Bull
moves mostly its head, which the mushrooms do not have), add a short procedural lunge in the swing: move the body
pivot toward the target and back over the swing time. Keep damage timing as it is.

Build with `sh Scripts/build_editor.sh` with the editor closed, then restart the editor.

### 6. Review in play mode
PIE on `Map_Saga`. `TSCAuto LAAL X 16` reaches the night with all four heroes alive
(`.claude/skills/tripo-character/scripts/pie_review.py` has helpers for stills of a unit). Take stills of each of
the three mushrooms walking and attacking a villager, save them to `Art/Tripo/mushroom_<key>/review_night.png`,
and look at them. Check: feet on the ground, facing the direction of travel (not sideways or backwards), right
size next to an axolotl, visible in the dark, the ram reaches the target.

### 7. Credits file
Add the three models to the Tripo line in `Docs/ThirdPartyAssets.md`.

## Rules

- Do not commit. Do not save `Map_Saga`. One editor instance, driven by one agent at a time.
- Do not change the axolotl scripts, assets or the `tripo-character` skill, except shared C++ in `TSCUnit.cpp`.
- A Tripo sign-in page or a credit prompt is the user's to answer: stop and ask.
- If a step fails its checks twice with different fixes, stop on that character, keep the evidence (stills, logs),
  and go on with what does not depend on it.

## Report at the end

What was made per character; which rig path was used (Tripo quadruped rig or the Blender fallback) and the bone
names found; credits spent; what the contact sheets and night stills show; what is still wrong; what was not
tested. Give the paths of the stills.
