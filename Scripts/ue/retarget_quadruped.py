"""IK Rigs, retargeter and the six retargeted Bull clips for one mushroom creature (quadruped).
Made from retarget_axolotl.py. The mushroom skeleton is the Bull skeleton fitted in Blender (same bone names and
hierarchy, IK / pole bones dropped, neck / head / tail shrunk into the body), so every chain maps one to one.

Rerunnable: the target IK Rig and this creature's retargeter are deleted and rebuilt, the clips are overwritten.
The shared source rig /Game/Characters/mushroom_shared/IK_Bull is created only when missing, never rebuilt.
Run in the editor through execute_python:

    CHAR = "purple"; exec(open(r"<repo>/Scripts/ue/retarget_quadruped.py").read()); RESULT

Set RUN_EXPORT = False before exec to only rebuild the rigs and retargeter.
A global PELVIS_H / PELVIS_V / PELVIS_Z set before exec overrides the TUNE value.
"""
import copy
import unreal

if "CHAR" not in globals():
    CHAR = "purple"
PASCAL = CHAR.capitalize()

SRC_FOLDER = "/Game/Animals/Bull"
SHARED = "/Game/Characters/mushroom_shared"
DST_NAME = "Boar" if CHAR == "boar" else "Mushroom_" + PASCAL
DST_FOLDER = "/Game/Animals/Boar" if CHAR == "boar" else "/Game/Characters/mushroom_" + CHAR
SRC_MESH = SRC_FOLDER + "/Bull"
DST_MESH = DST_FOLDER + "/" + DST_NAME
SRC_RIG = SHARED + "/IK_Bull"
DST_RIG = DST_FOLDER + "/IK_" + DST_NAME
RTG = DST_FOLDER + "/RTG_Bull_To_" + DST_NAME
SRC_PREFIX = "BullAnimalArmature_"
DST_PREFIX = DST_NAME + "AnimalArmature_"
CLIPS = ["Idle", "Walk", "Gallop", "Attack_Headbutt", "Idle_HitReact_Left", "Death"]
if CHAR == "boar":
    CLIPS.append("Eating")

# Same chains on both rigs (the bone names are identical). No neck / head / tail in the mushroom mesh: their bones carry
# no weights, they are mapped anyway so the clips stay whole.
CHAINS = [
    ("Spine", "Back", "Torso3"),
    ("Neck", "Neck1", "Neck3"),
    ("Head", "Head", "Head_end"),
    ("Tail", "Tail1", "Tail7_end"),
    ("FrontLeft", "FrontShoulder_L", "FrontLowerLeg_L_end"),
    ("FrontRight", "FrontShoulder_R", "FrontLowerLeg_R_end"),
    ("BackLeft", "BackShoulder_L", "BackLowerLeg_L_end"),
    ("BackRight", "BackShoulder_R", "BackLowerLeg_R_end"),
]
ROOT = "Body"

if "RUN_EXPORT" not in globals():
    RUN_EXPORT = True

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []


def _split(path):
    return path.rsplit("/", 1)


def _delete(path):
    for _ in range(2):
        if not EAL.does_asset_exist(path):
            return True
        unreal.load_asset(path)
        EAL.delete_asset(path)
        unreal.SystemLibrary.collect_garbage()
    if EAL.does_asset_exist(path):
        raise RuntimeError("could not delete " + path)
    return True


def make_rig(path, mesh_path, chains, reuse=False):
    folder, name = _split(path)
    if EAL.does_asset_exist(path):
        if reuse:
            rig = unreal.load_asset(path)
            ctl = unreal.IKRigController.get_controller(rig)
            log.append("%s: reused, root %s, chains %s" % (name, ctl.get_retarget_root(),
                                                           [str(c.chain_name) for c in ctl.get_retarget_chains()]))
            return rig
        _delete(path)
    if not EAL.does_directory_exist(folder):
        EAL.make_directory(folder)
    rig = tools.create_asset(name, folder, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    ctl = unreal.IKRigController.get_controller(rig)
    ctl.set_skeletal_mesh(unreal.load_asset(mesh_path))
    for i in reversed(range(ctl.get_num_solvers())):
        ctl.remove_solver(i)
    ctl.set_retarget_root(ROOT)
    for cname, start, end in chains:
        got = ctl.add_retarget_chain(cname, start, end, "None")
        if str(got) != cname:
            raise RuntimeError("chain %s on %s came back as %s" % (cname, name, got))
    log.append("%s: root %s, chains %s" % (name, ctl.get_retarget_root(),
                                           [str(c.chain_name) for c in ctl.get_retarget_chains()]))
    return rig


# ---------------------------------------------------------------- tuning
# Pelvis: the Bull's Body bone bobs and moves; the pelvis op scales that to the mushroom. PELVIS_H / PELVIS_V multiply the
# op's own height ratio, PELVIS_Z (cm) is a static lift.
TUNE = {
    # PELVIS_Z 1.5: measured Idle soles at -1.5 cm (the Bull's idle stands a little lower than its rest pose) on all three.
    "purple": {"PELVIS_H": 1.0, "PELVIS_V": 1.0, "PELVIS_Z": 1.5},
}
_tune = copy.deepcopy(TUNE.get(CHAR, TUNE["purple"]))
for _k in ("PELVIS_H", "PELVIS_V", "PELVIS_Z"):
    if _k not in globals():
        globals()[_k] = _tune[_k]
log.append("tuning %s: PELVIS_H %s PELVIS_V %s PELVIS_Z %s" % (CHAR, PELVIS_H, PELVIS_V, PELVIS_Z))

# ---------------------------------------------------------------- IK Rigs and retargeter
_delete(RTG)
src_rig = make_rig(SRC_RIG, SRC_MESH, CHAINS, reuse=True)
dst_rig = make_rig(DST_RIG, DST_MESH, CHAINS)

folder, name = _split(RTG)
rtg = tools.create_asset(name, folder, unreal.IKRetargeter, unreal.IKRetargetFactory())
rc = unreal.IKRetargeterController.get_controller(rtg)
S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
rc.set_ik_rig(S, src_rig)
rc.set_ik_rig(T, dst_rig)
rc.set_preview_mesh(S, unreal.load_asset(SRC_MESH))
rc.set_preview_mesh(T, unreal.load_asset(DST_MESH))
if rc.get_num_retarget_ops() == 0:
    rc.add_default_ops()
rc.assign_ik_rig_to_all_ops(S, src_rig)
rc.assign_ik_rig_to_all_ops(T, dst_rig)

FK_ONLY_DROP = ("IKRetargetIKChainsOp", "IKRetargetRunIKRigOp", "IKRetargetSpeedPlantingOp",
                "IKRetargetStrideWarpingOp", "IKRetargetFloorConstraintOp", "IKRetargetScaleGoalsOp",
                "IKRetargetOffsetGoalsOp", "IKRetargetAlignPoleVectorOp", "IKRetargetWeaponGoalOp")
for i in reversed(range(rc.get_num_retarget_ops())):
    t = rc.get_op_type_by_name(rc.get_op_name(i))
    if any(t.endswith(d) for d in FK_ONLY_DROP):
        rc.remove_retarget_op(i)

rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
log.append("chain map: %s" % {str(k): str(v) for k, v in rc.get_chain_map().items()})

# Retarget pose: the mushroom skeleton is the Bull skeleton scaled, with the legs moved sideways. Aligning chain to chain
# makes the target pose match the Bull's, bone by bone.
if "ALIGN" not in globals():
    ALIGN = True
if ALIGN:
    rc.auto_align_all_bones(T, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)

_ref = unreal.load_asset(SRC_MESH).skeleton.get_reference_pose()
src_pelvis_h = unreal.AnimPoseExtensions.get_bone_pose(_ref, ROOT, unreal.AnimPoseSpaces.WORLD).translation.z
for i in range(rc.get_num_retarget_ops()):
    opname = rc.get_op_name(i)
    t = rc.get_op_type_by_name(opname)
    ctl = rc.get_op_controller(i)
    if t.endswith("IKRetargetPelvisMotionOp"):
        ctl.set_source_pelvis_bone(ROOT)
        ctl.set_target_pelvis_bone(ROOT)
        st = ctl.get_settings()
        # Plain multipliers: the op already scales the Bull's pelvis motion by target / source pelvis height. (The axolotl
        # script divides by the source height; measured here that left the Body at z ~0, 1/91 of its rest height.)
        st.scale_horizontal = PELVIS_H
        st.scale_vertical = PELVIS_V
        st.translation_offset_global = unreal.Vector(0.0, 0.0, PELVIS_Z)
        ctl.set_settings(st)
        log.append("pelvis: source height %.2f cm, scale_h %.5f scale_v %.5f offset_z %.1f" % (
            src_pelvis_h, st.scale_horizontal, st.scale_vertical, PELVIS_Z))
    elif t.endswith("IKRetargetFKChainsOp"):
        st = ctl.get_settings()
        chains = []
        for c in list(st.chains_to_retarget):
            chains.append(c)
            c.enable_fk = True
            c.rotation_mode = unreal.FKChainRotationMode.ONE_TO_ONE  # same bone counts on both sides
            c.translation_mode = unreal.FKChainTranslationMode.NONE
        st.chains_to_retarget = chains
        ctl.set_settings(st)
        log.append("fk: %s" % [(str(c.target_chain_name), str(c.rotation_mode).split(".")[-1].split(":")[0])
                               for c in ctl.get_settings().chains_to_retarget])
    elif t.endswith("IKRetargetRootMotionOp"):
        rc.set_retarget_op_enabled(i, False)
    log.append("op %d %s (%s) enabled=%s" % (i, opname, t.rsplit(".", 1)[-1], rc.get_retarget_op_enabled(i)))

EAL.save_loaded_asset(src_rig, True)
for a in (dst_rig, rtg):
    EAL.save_loaded_asset(a, False)

rep = rc.validate_retargeter(unreal.load_asset(SRC_MESH), unreal.load_asset(DST_MESH))
try:
    log.append("validate: %s" % rep.to_dict() if hasattr(rep, "to_dict") else str(rep))
except Exception as e:  # noqa: BLE001
    log.append("validate: %s" % e)

# ---------------------------------------------------------------- batch export
if RUN_EXPORT:
    import re
    unreal.SystemLibrary.collect_garbage()
    pat = re.compile(r"^%s(%s)\d*$" % (re.escape(DST_PREFIX), "|".join(CLIPS)))
    for p in EAL.list_assets(DST_FOLDER, recursive=False):
        n = str(p).split(".")[0].rsplit("/", 1)[-1]
        if pat.match(n):
            _delete(str(p).split(".")[0])
    unreal.SystemLibrary.collect_garbage()
    src_assets = []
    for c in CLIPS:
        ad = unreal.EditorAssetLibrary.find_asset_data(SRC_FOLDER + "/" + SRC_PREFIX + c)
        if not ad.is_valid():
            raise RuntimeError("missing source clip " + c)
        src_assets.append(ad)
    inp = unreal.IKRetargetBatchOperationInputs()
    inp.assets_to_retarget = src_assets
    inp.ik_retarget_asset = rtg
    inp.source_mesh = unreal.load_asset(SRC_MESH)
    inp.target_mesh = unreal.load_asset(DST_MESH)
    inp.search = "Bull"
    inp.replace = "Mushroom_" + PASCAL
    inp.prefix = ""
    inp.suffix = ""
    inp.target_path = DST_FOLDER
    inp.use_source_path = False
    inp.include_referenced_assets = False
    inp.overwrite_existing_files = True
    made = []
    todo = list(src_assets)
    for attempt in range(2):
        inp.assets_to_retarget = todo
        results = unreal.IKRetargetBatchOperation.run_batch_retarget_with_results(inp)
        todo = []
        for r in results:
            if r.status == unreal.IKRetargetBatchAssetStatus.RETARGETED:
                made.append(str(r.retargeted_asset.package_name))
            else:
                log.append("attempt %d: %s %s %s" % (attempt, r.source_asset.asset_name, r.status, r.reason))
                todo.append(r.source_asset)
        if not todo:
            break
    log.append("exported %d: %s" % (len(made), [m.rsplit("_", 1)[-1] for m in made]))

    dst_skel = unreal.load_asset(DST_MESH).skeleton
    for c in CLIPS:
        want = DST_FOLDER + "/" + DST_PREFIX + c
        if not EAL.does_asset_exist(want):
            cand = [p for p in made if p.endswith(c) and p.startswith(DST_FOLDER)]
            if cand:
                EAL.rename_asset(cand[0], want)
        seq = unreal.load_asset(want)
        src = unreal.load_asset(SRC_FOLDER + "/" + SRC_PREFIX + c)
        if seq is None:
            log.append("%s MISSING" % want)
            continue
        ok = seq.get_editor_property("skeleton") == dst_skel
        log.append("%-20s src %.3fs dst %.3fs skel_ok=%s" % (c, src.get_play_length(), seq.get_play_length(), ok))
        EAL.save_loaded_asset(seq, False)

RESULT = "\n".join(log)
