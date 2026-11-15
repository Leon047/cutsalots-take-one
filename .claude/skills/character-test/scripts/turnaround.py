# Turnaround capture of one character: front, back and right-side stills, optionally with a weapon and a shield
# held the way the game holds them (same socket / orientation / size math as ATSCUnit::AddWeapon in TSCUnit.cpp).
#
# Run in the editor through execute_python (globals do not survive between calls, so set them in the same call):
#   NAME = "warrior"; WEAPON = "axe"; SHIELD = True; OUT_DIR = r"<dir>"
#   exec(open(r"<repo>/.claude/skills/character-test/scripts/turnaround.py").read()); RESULT = await run(); RESULT
#
# Globals (all but NAME optional):
#   NAME    character: folder key under /Game/Characters or /Game/Animals ("warrior", "axolotl_frog", "viking_lord",
#           "mushroom_cup", "Bull"), or a full /Game/... path of a skeletal mesh
#   WEAPON  None | "axe" | "sword" | "bow" | "staff"
#   SHIELD  False | True
#   CLIP    pose clip name, default "Idle";  TIME  seconds into the clip, default 0
#   SIZE    still size in px, default 768;   OUT_DIR  where the stills go
# The scene is built far above the map from temporary actors and removed afterwards; nothing is saved.
import unreal, asyncio, os, math, json

NAME = globals().get("NAME")
WEAPON = globals().get("WEAPON") or None
SHIELD = bool(globals().get("SHIELD", False))
CLIP = globals().get("CLIP", "Idle")
TIME = float(globals().get("TIME", 0.0))
SIZE = int(globals().get("SIZE", 768))
if "OUT_DIR" not in globals():
    OUT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "CharacterTest"

BASE = unreal.Vector(0, 0, 60000)
FIGURE_H = 120.0  # VikingHeight in TSCUnit.cpp
FOV = 30.0
LUX = 4.0
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
AR = unreal.AssetRegistryHelpers.get_asset_registry()
ROOTS = ["/Game/Characters", "/Game/Animals"]

# Mirrors FTSCWeaponDef in TSCUnit.cpp: mesh, socket, hand side, mesh axis that goes to socket +X / +Z, grip point
# (mesh units), longest dimension as a fraction of the figure height. Keep in step with DefSword .. DefStaff there.
ITEMS = {
    "sword":  (["/Game/Weapons/Sword/Sword"], "weapon_r", "R", (1, 0, 0), (0, -1, 0), (0, 0.03, 0), 0.55),
    "axe":    (["/Game/Weapons/Axe/Axe_small", "/Game/Weapons/Аxe/Axe_small"], "weapon_r", "R", (1, 0, 0), (0, -1, 0), (0, 0.45, 0), 0.45),
    "shield": (["/Game/Weapons/Shield/Shield_Round"], "shield_l", "L", (0, -1, 0), (0, 0, 1), (0, 0, 0), 0.4),
    "bow":    (["/Game/Weapons/Bow/Bow_Wooden"], "bow_l", "L", (-1, 0, 0), (0, 1, 0), (0, 0, 0), 0.42),
    "staff":  (["/Game/Weapons/Staff/Staff"], "staff_r", "R", (1, 0, 0), (0, 0, 1), (0, 0, 0), 0.5),
}
HAND_BONES = {"R": ["RightHand", "Fist_R"], "L": ["LeftHand", "Fist_L"]}
# name, direction from the figure to the camera. The figures face +Y, so their right side is -X.
VIEWS = [("front", (0, 1, 0)), ("back", (0, -1, 0)), ("right", (-1, 0, 0))]


def _cls(a):
    return str(a.asset_class_path.asset_name)


def resolve(name):
    """-> (skeletal mesh path, folder). Raises ValueError with the choices when the name is unknown or ambiguous."""
    if not name:
        raise ValueError("NAME is not set")
    if name.startswith("/Game/"):
        if unreal.EditorAssetLibrary.does_asset_exist(name):
            return name.split(".")[0], name.rsplit("/", 1)[0]
        raise ValueError("no asset at %s" % name)
    folders = []
    for r in ROOTS:
        folders += [str(p) for p in AR.get_sub_paths(r, False)]
    n = name.lower().replace(" ", "_")
    leaf = lambda p: p.rsplit("/", 1)[1].lower()
    hits = ([p for p in folders if leaf(p) == n] or [p for p in folders if leaf(p).endswith("_" + n)]
            or [p for p in folders if n in leaf(p)])
    if len(hits) != 1:
        raise ValueError("character '%s' %s. Known: %s" % (
            name, "is ambiguous (%s)" % ", ".join(sorted(leaf(p) for p in hits)) if hits else "not found",
            ", ".join(sorted(p.rsplit("/", 1)[1] for p in folders))))
    meshes = [a for a in AR.get_assets_by_path(hits[0], False) if _cls(a) == "SkeletalMesh"]
    if not meshes:
        raise ValueError("no skeletal mesh in %s" % hits[0])
    return str(meshes[0].package_name), hits[0]


def find_clip(folder, mesh, clip):
    skel = mesh.skeleton.get_path_name()
    seqs = [a for a in AR.get_assets_by_path(folder, False) if _cls(a) == "AnimSequence"]
    names = sorted(str(a.asset_name) for a in seqs)
    for a in seqs:
        if str(a.asset_name).lower().endswith("_" + clip.lower()):
            s = a.get_asset()
            if s.get_editor_property("skeleton").get_path_name() == skel:
                return s, names
    return None, names


def apply_pose(comp, seq, t):
    # The offscreen editor does not tick animation; re-setting the mesh re-runs InitAnim, which evaluates the
    # single-node animation at the saved position and rebuilds the render state.
    m = comp.get_skinned_asset()
    ad = unreal.SingleAnimationPlayData()
    ad.anim_to_play = seq
    ad.saved_position = t
    ad.saved_play_rate = 0.0
    ad.saved_looping = False
    ad.saved_playing = False
    comp.set_editor_property("animation_data", ad)
    comp.set_skinned_asset_and_update(None, True)
    comp.set_skinned_asset_and_update(m, True)


def place_item(kind, comp, warnings):
    """Spawn the item's mesh at the hand socket's world transform (ATSCUnit::AddWeapon, written out in world space)."""
    paths, socket, side, lx, lz, grip, size = ITEMS[kind]
    sm = None
    for p in paths:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            sm = unreal.load_asset(p)
            break
    if not sm:
        warnings.append("%s: mesh not found (%s)" % (kind, paths[0]))
        return None, None
    used = socket
    if not comp.does_socket_exist(socket):
        used = next((b for b in HAND_BONES[side] if comp.does_socket_exist(b)), None)
        if not used:
            warnings.append("%s: no socket '%s' and no hand bone on this skeleton, item skipped" % (kind, socket))
            return None, None
        warnings.append("%s: socket '%s' is missing, attached to the bone '%s' instead (as the game does)" % (kind, socket, used))
    st = comp.get_socket_transform(used, unreal.RelativeTransformSpace.RTS_WORLD)
    sq = st.rotation
    box = sm.get_bounding_box()
    d = box.max - box.min
    wscale = size * FIGURE_H / max(d.x, d.y, d.z, 1e-4)
    rot = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(*lx), unreal.Vector(*lz)).quaternion().inversed()
    loc = st.translation - sq.rotate_vector(rot.rotate_vector(unreal.Vector(*grip))) * wscale
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, 0))
    a.set_mobility(unreal.ComponentMobility.MOVABLE)
    a.static_mesh_component.set_static_mesh(sm)
    a.set_actor_transform(unreal.Transform(loc, sq.multiply(rot).rotator(), unreal.Vector(wscale, wscale, wscale)), False, False)
    a.set_actor_label("TMP_CHARTEST_" + kind)
    return a, used


async def run():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    warnings, temps, files = [], [], {}
    if WEAPON and (WEAPON not in ITEMS or WEAPON == "shield"):
        raise ValueError("WEAPON must be one of axe, sword, bow, staff (got %r)" % WEAPON)
    mesh_path, folder = resolve(NAME)
    mesh = unreal.load_asset(mesh_path)
    key = folder.rsplit("/", 1)[1]
    tag = "_".join([key] + ([WEAPON] if WEAPON else []) + (["shield"] if SHIELD else [])
                   + ([CLIP] if CLIP.lower() != "idle" or TIME else []))
    os.makedirs(OUT_DIR, exist_ok=True)
    try:
        mb = mesh.get_bounds()
        scale = FIGURE_H / max(mb.box_extent.z * 2, 1.0)
        # Ground at the bottom of the mesh's own bounds, as the game stands the figure (the posed component's
        # bounds are looser and would sink the floor).
        ground_z = BASE.z + (mb.origin.z - mb.box_extent.z) * scale
        fig = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, BASE, unreal.Rotator(0, 0, 0))
        temps.append(fig)
        fig.set_actor_label("TMP_CHARTEST_FIG")
        comp = fig.skeletal_mesh_component
        comp.set_skinned_asset_and_update(mesh, True)
        comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        fig.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        seq, clip_names = find_clip(folder, mesh, CLIP)
        if seq:
            apply_pose(comp, seq, min(TIME, seq.get_play_length() - 0.001))
        else:
            warnings.append("clip '%s' not found, shown in the reference pose. Clips: %s" % (CLIP, ", ".join(clip_names)))
        await asyncio.sleep(0.2)

        sockets = {}
        for kind in ([WEAPON] if WEAPON else []) + (["shield"] if SHIELD else []):
            a, used = place_item(kind, comp, warnings)
            if a:
                temps.append(a)
                sockets[kind] = used

        # Frame: one box around the figure and what it holds; the same camera distance for the three views.
        fo, fe = fig.get_actor_bounds(False)
        lo, hi = fo - fe, fo + fe
        for a in temps[1:]:
            o, e = a.get_actor_bounds(False)
            lo = unreal.Vector(min(lo.x, o.x - e.x), min(lo.y, o.y - e.y), min(lo.z, o.z - e.z))
            hi = unreal.Vector(max(hi.x, o.x + e.x), max(hi.y, o.y + e.y), max(hi.z, o.z + e.z))
        focus = (lo + hi) * 0.5
        half = (hi - lo) * 0.5
        radius = max(half.x, half.y, half.z)
        dist = radius * 1.12 / math.tan(math.radians(FOV / 2)) + max(half.x, half.y)

        floor = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(focus.x, focus.y, ground_z), unreal.Rotator(0, 0, 0))
        floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        floor.set_actor_scale3d(unreal.Vector(40, 40, 1))
        temps.append(floor)
        sun = EAS.spawn_actor_from_class(unreal.DirectionalLight, focus + unreal.Vector(0, 0, 500), unreal.Rotator(0, -40, 0))
        sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sun.light_component.set_intensity(LUX)
        temps.append(sun)
        cap = EAS.spawn_actor_from_class(unreal.SceneCapture2D, focus, unreal.Rotator(0, 0, 0))
        temps.append(cap)
        cc = cap.capture_component2d
        rt = unreal.RenderingLibrary.create_render_target2d(world, SIZE, SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        cc.texture_target = rt
        cc.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        cc.capture_every_frame = False
        cc.fov_angle = FOV
        pp = cc.post_process_settings
        pp.override_auto_exposure_method = True
        pp.auto_exposure_method = unreal.AutoExposureMethod.AEM_MANUAL
        pp.override_auto_exposure_apply_physical_camera_exposure = True
        pp.auto_exposure_apply_physical_camera_exposure = False
        pp.override_auto_exposure_bias = True
        pp.auto_exposure_bias = 0.0
        cc.post_process_settings = pp
        cc.post_process_blend_weight = 1.0

        for view, d in VIEWS:
            dirv = unreal.Vector(*d)
            camloc = focus + dirv * dist
            cap.set_actor_location_and_rotation(camloc, unreal.MathLibrary.find_look_at_rotation(camloc, focus), False, False)
            # Key light from behind the camera, above and 30 deg to its left, so every view is lit the same way.
            side = unreal.Vector(-dirv.y, dirv.x, 0)
            lightpos = focus + dirv * 1.0 + side * 0.58 + unreal.Vector(0, 0, 0.85)
            sun.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(lightpos, focus), False)
            await asyncio.sleep(0.15)
            cc.capture_scene()
            await asyncio.sleep(0.1)
            fname = "%s_%s.png" % (tag, view)
            unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, fname)
            files[view] = OUT_DIR.replace("\\", "/").rstrip("/") + "/" + fname
        return json.dumps({"tag": tag, "mesh": mesh_path, "clip": seq.get_name() if seq else None, "time": TIME,
                           "sockets": sockets, "warnings": warnings, "out_dir": OUT_DIR, "files": files}, indent=1)
    finally:
        for a in temps:
            EAS.destroy_actor(a)
