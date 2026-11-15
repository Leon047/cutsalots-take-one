# Sole / body measurement for one mushroom creature. Set CHAR before exec. Result in RESULT.
# Sole height at a frame = min over the four *_end leg bones of (pose z - rest z): the rest pose has the soles at z = 0.
import unreal
PASCAL = CHAR.capitalize()
MESH = "/Game/Animals/Boar/Boar" if CHAR == "boar" else "/Game/Characters/mushroom_%s/Mushroom_%s" % (CHAR, PASCAL)
P = unreal.AnimPoseExtensions
W = unreal.AnimPoseSpaces.WORLD
E = unreal.AnimPoseEvaluationOptions()
mesh = unreal.load_asset(MESH)
ref = mesh.skeleton.get_reference_pose()
BONES = ["FrontLowerLeg_L_end", "FrontLowerLeg_R_end", "BackLowerLeg_L_end", "BackLowerLeg_R_end"]
rest = {b: P.get_bone_pose(ref, b, W).translation.z for b in BONES}
out = {}
clips = ["Idle", "Walk", "Gallop", "Attack_Headbutt", "Idle_HitReact_Left", "Death"]
if CHAR == "boar":
    clips.append("Eating")
for clip in clips:
    seq = unreal.load_asset(MESH + "AnimalArmature_" + clip)
    L = seq.get_play_length()
    n = 60
    lo, hi = 1e9, -1e9
    for i in range(n + 1):
        t = min(L * i / n, L - 0.001)
        pose = P.get_anim_pose_at_time(seq, t, E)
        s = min(P.get_bone_pose(pose, b, W).translation.z - rest[b] for b in BONES)
        lo, hi = min(lo, s), max(hi, s)
    end = P.get_anim_pose_at_time(seq, L - 0.001, E)
    out[clip] = (round(lo, 2), round(hi, 2), round(P.get_bone_pose(end, "Body", W).translation.z, 2),
                 [round(P.get_bone_pose(end, b, W).translation.z - rest[b], 1) for b in BONES])
RESULT = "%s rest Body z %.2f | " % (CHAR, P.get_bone_pose(ref, "Body", W).translation.z) + " ; ".join(
    "%s sole min %.2f max %.2f endBody %.2f endSoles %s" % ((k,) + v) for k, v in out.items())
