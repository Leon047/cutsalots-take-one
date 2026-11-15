---
name: character-test
description: Capture a turnaround screenshot of one character of TheSagaCutter - front, back and right side on one sheet - optionally holding a weapon (axe, sword, bow, staff) and a shield exactly as the game attaches them. Use to check a character model, its textures, its pose or how a weapon / shield sits in the hand sockets - when the user says "/character-test", "test the <character>", "show me the <character> with an axe and shield", "screenshot the character from all sides", "how does the shield sit on the frog", or after a change to a character, its sockets or the weapon defs when a visual check would confirm it. Needs the ue-editor skill.
argument-hint: "<character> [axe | sword | bow | staff] [shield] [clip <Name> [time]]"
---

# character-test

One character in, one sheet out: `front | back | right` at the same scale, lit the same way in each view.

Arguments:
- `<character>` - a folder key under `/Game/Characters` or `/Game/Animals`: full (`axolotl_frog`, `viking_lord`,
  `mushroom_cup`, `Bull`) or its last part (`frog`, `lord`, `cup`). A full `/Game/...` skeletal mesh path also works.
  Required; ask once if missing.
- weapon - `axe`, `sword`, `bow` or `staff`. Optional; none = empty hand.
- `shield` - optional; adds the round shield.
- clip / time - optional pose, default `Idle` at 0 s (for example `SwordSlash 0.5` to see the weapon mid-swing).

## Steps

1. Editor: follow the ue-editor skill (`ue_status`, else `ue_start` with `offscreen: true`). Call
   `PythonScriptingTools.read_me` once per conversation.
2. Capture: one `execute_python` call (toolset `editor_toolset.toolsets.python_scripting.PythonScriptingTools`).
   Globals do not survive between calls, so set them all here; leave out the ones not asked for.
   ```python
   NAME = "warrior"; WEAPON = "axe"; SHIELD = True   # CLIP = "SwordSlash"; TIME = 0.5; SIZE = 768
   exec(open(r"<repo>/.claude/skills/character-test/scripts/turnaround.py").read())
   RESULT = await run()
   RESULT
   ```
   It returns JSON: `tag`, the mesh, the clip, the socket each item went on, `warnings`, `out_dir`, the three files.
   Default `out_dir` is `UE/Saved/CharacterTest` (git-ignored).
3. Sheet: `python .claude/skills/character-test/scripts/make_sheet.py <out_dir> <tag>` writes `<out_dir>/<tag>.png`.
4. Look at the sheet yourself (Read), then show it to the user (SendUserFile) with any `warnings` and what you see.
5. `ue_stop` if this skill started the editor and nothing else needs it.

Several characters or loadouts: repeat step 2's three lines per variant inside one `execute_python` call (set the
globals again before each `exec`), then one `make_sheet.py` per tag.

## What to check on the sheet

- All three views show the whole figure with what it holds; the right view shows the right hand (weapon side) nearest.
- Front: face, textures, the weapon not crossing the head or body. Back: back of the shield, tail, no torn skin.
- Right: the weapon shaft and shield plane; hand closed on the grip, not floating beside it.
- A warning "socket ... is missing, attached to the bone" means the mesh has no hand sockets: for axolotls rerun
  `Scripts/ue/sockets_axolotl.py` (see the tripo-character skill). Animals and mushrooms have no hands; items are skipped.

## How it works / limits

- `scripts/turnaround.py` builds a temporary scene 600 m above the map (figure fitted to 120 cm as in the game, floor,
  key light that follows the camera, `SceneCapture2D`, manual exposure) and destroys it afterwards. It saves nothing
  and does not touch the level. The figures face +Y, so the cameras sit at +Y (front), -Y (back) and -X (right).
- The offscreen editor does not tick animation: the pose is set by re-initialising a single-node animation at `TIME`.
- Items are placed at the socket's world transform with the math of `ATSCUnit::AddWeapon`. The table `ITEMS` in the
  script mirrors `DefSword .. DefStaff` in `UE/Source/TheSagaCutter/TSCUnit.cpp`: **when those change, change `ITEMS`
  too**, or the sheet shows the old grip. A new weapon kind needs a row there.
- It shows the plain asset: no role tunic colour, raider tint, random size or the 12 % named-hero scale. For the real
  in-game look use the play-mode review of the tripo-character skill (`scripts/pie_review.py`).
- Unknown or ambiguous name: `run()` raises a `ValueError` that lists the known characters; ask the user which one.
