"""Hand sockets on one axolotl character's mesh (Docs/Plans/axolotl-all-classes.md, S4; first made for the warrior in
S6 of Docs/Plans/axolotl-warrior-tripo.md).

weapon_r / staff_r on RightHand, shield_l / bow_l on LeftHand, in the Viking socket convention
(+Z = where the shaft / blade points, +X = edge / facing / shooting side).

Orientation: the retargeter copies each Viking arm bone's global rotation change one to one onto the axolotl
bone (Fist_R -> RightHand), so a socket that should turn with the paw exactly as the Viking socket turns with
the fist is   R_axo_local = inv(Hand_comp(t)) * Fist_comp(t) * R_viking_local   at any frame t of a clip and
its retargeted copy. It is solved on Idle frame 0 and checked on other frames.
Position: centre of the paw, halfway between the hand bone and the middle-finger tip (<Hand>Middle4).
Units: the axolotl bones have scale 1 and cm, so the location is plain cm in hand space; socket scale 1.

Rerunnable: existing sockets with these names are removed first (mesh and skeleton). Run in the editor
(execute_python, which allows top-level await; CHAR defaults to "warrior"):
    CHAR = "archer"; exec(open(r"<repo>/Scripts/ue/sockets_axolotl.py").read()); await run()
A global SOCKET_LEAN / SOCKET_SHIFT set before exec overrides the per-character table value.
"""
import copy
import math
import unreal

if "CHAR" not in globals():
    CHAR = "warrior"
PASCAL = CHAR.capitalize()

AX_MESH = "/Game/Characters/axolotl_%s/Axolotl_%s" % (CHAR, PASCAL)
VK_MESH = "/Game/Characters/viking_male/Viking_Male"
AX_CLIP = "/Game/Characters/axolotl_%s/Axolotl_%sCharacterArmature_" % (CHAR, PASCAL)
VK_CLIP = "/Game/Characters/viking_male/Viking_MaleCharacterArmature_"
SOCKETS = [  # name, viking bone, axolotl bone, paw tip bone
    ("weapon_r", "Fist_R", "RightHand", "RightHandMiddle4"),
    ("staff_r", "Fist_R", "RightHand", "RightHandMiddle4"),
    ("shield_l", "Fist_L", "LeftHand", "LeftHandMiddle4"),
    ("bow_l", "Fist_L", "LeftHand", "LeftHandMiddle4"),
]
# S8 (warrior plan) per-socket corrections on top of the Viking-matched orientation / paw-centre position:
#  - weapon_r: lean the shaft by (deg about socket +X, deg about socket +Y), post-multiplied in socket space. +X (the edge)
#    is unchanged, so the edge still leads the swing. With the Viking orientation the axe / sword crossed the big head in
#    Idle, Run and RecieveHit (clearance to a 21 cm head sphere -3.5 / -18 / -4 cm); with (-40, -5): +13 / -1.6 / +13,
#    SwordSlash +0.8 (scratch scan over the retargeted clips, see the S8 report).
#  - shield_l: SOCKET_FIT on every character (see the code): face out, strap side on the forearm, forearm through the
#    straps and the paw on the grip bar. (The older SOCKET_SHIFT for it kept the Viking orientation, which put the
#    disc across the body with the straps turned away from the arm.)
# A key without an entry uses the warrior values.
SOCKET_TUNE = {
    "warrior": {
        "SOCKET_LEAN": {"weapon_r": (-40.0, -5.0)},
        "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}},
    },
    # S9 (all-classes plan). Hakon: with the Viking orientation the bow showed its string side outward (socket +X, the
    # bow's back, pointed at the body) and leaned 21-25 deg in Idle. 160 deg about +Y turns the string toward the body
    # (the bow is symmetric, so it may end up upside down); 10 deg about +X stands it up: tilt Idle 4, Shoot_OneHanded
    # 5 deg (Walk 26, Run 55 with the arm swing), head clearance >= 8 cm (scratch scan over the retargeted clips).
    "archer": {
        "SOCKET_LEAN": {"weapon_r": (-40.0, -5.0), "bow_l": (10.0, 160.0)},
        "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}},
    },
    # Sigrun: the staff stood upright (Idle tilt 5 deg) but its foot hung 26 cm (mesh) above the ground with the short
    # axolotl arm. The forearm runs within ~21 deg of the staff axis in every clip, so a shift of 14 cm away from the
    # elbow slides the staff down along itself (the 3.2 cm along +X cancels most of the sideways part): foot at
    # 14 cm in Idle, top at shoulder height. No lean helps Punch: the paw is at the face there, so the staff crosses
    # the crown in every orientation that keeps Idle upright.
    "druid": {
        "SOCKET_LEAN": {"weapon_r": (-40.0, -5.0)},
        "SOCKET_SHIFT": {"staff_r": (-14.0, 3.2)},
        "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}},
    },
    # Raiders: SOCKET_FIT (see the code) puts the strap side on the arm and the forearm through the straps; the older
    # SOCKET_ROLL / SOCKET_OFFSET notes below describe the first attempt, which left the arm through the disc.
    # Raiders (frog / bird / amphibian). The Shield_Round mesh is a disc in socket X-Z, so its face normal is +Y (not +X,
    # see the comment on shield_l above) and the handle is two parallel bars: an arm strap and a grip bar. The Viking
    # orientation leaves the bars along the forearm. A roll of 90 deg about the socket's own +Y (SOCKET_ROLL, not SOCKET_LEAN: LEAN composes in the parent frame and would turn the disc edge-on) lays them across
    # it, so the forearm passes under the strap and the fist closes on the grip bar. SOCKET_OFFSET (cm, socket X / Y / Z
    # before lean and roll) then puts the fist on the grip bar: 1.5 of it is the old shift along X, plus 4 / 4 found by a grid
    # of shots in PIE. The shield's own shift along X is 0 because after the roll X is no longer the same direction.
    "frog":      {"SOCKET_LEAN": {"weapon_r": (-40.0, -5.0), },
                  "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}}},
    "bird":      {"SOCKET_LEAN": {"weapon_r": (-40.0, -5.0), },
                  "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}}},
    "amphibian": {"SOCKET_LEAN": {"weapon_r": (-40.0, -5.0), },
                  "SOCKET_FIT": {"shield_l": {"grip": (0.0, -27.0, -31.0), "unit": 0.205}}},
}
_stune = copy.deepcopy(SOCKET_TUNE.get(CHAR, SOCKET_TUNE["warrior"]))
if "SOCKET_LEAN" not in globals():
    SOCKET_LEAN = _stune["SOCKET_LEAN"]
if "SOCKET_SHIFT" not in globals():
    SOCKET_SHIFT = _stune.get("SOCKET_SHIFT", {})
if "SOCKET_OFFSET" not in globals():
    SOCKET_OFFSET = _stune.get("SOCKET_OFFSET", {})
if "SOCKET_ROLL" not in globals():
    SOCKET_ROLL = _stune.get("SOCKET_ROLL", {})
if "SOCKET_FIT" not in globals():
    SOCKET_FIT = _stune.get("SOCKET_FIT", {})
FOREARM = {"RightHand": "RightForeArm", "LeftHand": "LeftForeArm"}
P = unreal.AnimPoseExtensions
W = unreal.AnimPoseSpaces.WORLD
E = unreal.AnimPoseEvaluationOptions()
log = ["%s: lean %s shift %s" % (CHAR, SOCKET_LEAN, SOCKET_SHIFT)]


def comp_rot(seq, t, bone):
    return P.get_bone_pose(P.get_anim_pose_at_time(seq, t, E), bone, W).rotation


def ang(q):
    return math.degrees(q.get_angle())


ax_mesh = unreal.load_asset(AX_MESH)
vk_mesh = unreal.load_asset(VK_MESH)
ax_skel = ax_mesh.skeleton
ref = ax_skel.get_reference_pose()
vk_idle, ax_idle = unreal.load_asset(VK_CLIP + "Idle"), unreal.load_asset(AX_CLIP + "Idle")

# remove old copies (on the mesh and on the skeleton)
sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
for name in [s[0] for s in SOCKETS] + ["Socket", "None"]:  # (+ unnamed leftovers of failed attempts)
    for _ in range(4):
        if ax_mesh.find_socket(name) is None:
            break
        sub.remove_socket(ax_mesh, name)

PLAN = []
for name, vbone, abone, tip in SOCKETS:
    vs = vk_mesh.find_socket(name)
    r_vk = vs.relative_rotation.quaternion()
    # solve on Idle frame 0
    sock_comp = comp_rot(vk_idle, 0.0, vbone).multiply(r_vk)
    r_ax = comp_rot(ax_idle, 0.0, abone).inversed().multiply(sock_comp)
    # check on other clips / frames: the axolotl socket's component rotation vs the Viking socket's
    errs = []
    for clip, t in (("Idle", 2.0), ("Walk", 0.4), ("SwordSlash", 0.5), ("Run", 0.3)):
        sv, sa = unreal.load_asset(VK_CLIP + clip), unreal.load_asset(AX_CLIP + clip)
        a = comp_rot(sv, t, vbone).multiply(r_vk)
        b = comp_rot(sa, t, abone).multiply(r_ax)
        errs.append("%s %.1f" % (clip, ang(a.inversed().multiply(b))))
    # position: halfway between hand and paw tip, in hand space (ref pose)
    hand = P.get_bone_pose(ref, abone, W)
    mid = (hand.translation + P.get_bone_pose(ref, tip, W).translation) * 0.5
    loc = hand.inverse_transform_location(mid)

    r_ax0 = r_ax  # socket rotation before the lean: SOCKET_OFFSET is measured in this frame
    if name in SOCKET_LEAN:
        ax_deg, ay_deg = SOCKET_LEAN[name]

        def _q(axis, deg):
            s = math.sin(math.radians(deg) / 2)
            return unreal.Quat(axis[0] * s, axis[1] * s, axis[2] * s, math.cos(math.radians(deg) / 2))
        r_ax = r_ax.multiply(_q((1, 0, 0), ax_deg)).multiply(_q((0, 1, 0), ay_deg))
    if name in SOCKET_ROLL:
        # a roll about the socket's own +Y (the face normal of Shield_Round): spins the item in its own plane, so the disc
        # keeps facing the same way and only the handle turns (measured: the angle between old and new socket is exactly
        # the roll, about local Y). Same as the SOCKET_LEAN +Y term, but kept apart so the unrolled frame stays available.
        _rs = math.sin(math.radians(SOCKET_ROLL[name]) / 2)
        r_ax = r_ax.multiply(unreal.Quat(0, _rs, 0, math.cos(math.radians(SOCKET_ROLL[name]) / 2)))
    if name in SOCKET_FIT:
        # Fit the item to the arm instead of nudging the Viking socket. Frame solved on Idle frame 0 in hand space:
        #   item +Z = towards the elbow (forearm axis), item -Y = towards the body (the strap side of Shield_Round
        #   faces the arm), item +Y = out; item X = Y x Z. The paw centre is put on GRIP (item units, cm = units * UNIT).
        # Shield_Round (mesh units, disc radius 100): face +Y, boss y +24, disc back y -12, straps y -25..-37; upper strap
        # z +6..+25, grip bar z -22..-41 (both along X), so the forearm runs along -Z from the upper strap to the grip bar.
        _fit = SOCKET_FIT[name]
        _pose = P.get_anim_pose_at_time(ax_idle, 0.0, E)
        _h = P.get_bone_pose(_pose, abone, W)

        def _hs(b):
            return _h.inverse_transform_location(P.get_bone_pose(_pose, b, W).translation)

        _mid = (_hs(abone) + _hs(tip)) * 0.5
        _f = _mid - _hs(FOREARM[abone])
        _f = _f * (1.0 / _f.length())
        _b = _hs("Spine") - _mid
        _n = _b - _f * (_b.x * _f.x + _b.y * _f.y + _b.z * _f.z)
        _n = _n * (1.0 / _n.length())
        _ez, _ey = _f * -1.0, _n * -1.0
        _ex = unreal.Vector(_ey.y * _ez.z - _ey.z * _ez.y, _ey.z * _ez.x - _ey.x * _ez.z, _ey.x * _ez.y - _ey.y * _ez.x)
        _m = [[_ex.x, _ey.x, _ez.x], [_ex.y, _ey.y, _ez.y], [_ex.z, _ey.z, _ez.z]]
        _tr = _m[0][0] + _m[1][1] + _m[2][2]
        if _tr > 0:
            _s = math.sqrt(_tr + 1.0) * 2
            _qw, _qx, _qy, _qz = 0.25 * _s, (_m[2][1] - _m[1][2]) / _s, (_m[0][2] - _m[2][0]) / _s, (_m[1][0] - _m[0][1]) / _s
        elif _m[0][0] > _m[1][1] and _m[0][0] > _m[2][2]:
            _s = math.sqrt(1.0 + _m[0][0] - _m[1][1] - _m[2][2]) * 2
            _qw, _qx, _qy, _qz = (_m[2][1] - _m[1][2]) / _s, 0.25 * _s, (_m[0][1] + _m[1][0]) / _s, (_m[0][2] + _m[2][0]) / _s
        elif _m[1][1] > _m[2][2]:
            _s = math.sqrt(1.0 + _m[1][1] - _m[0][0] - _m[2][2]) * 2
            _qw, _qx, _qy, _qz = (_m[0][2] - _m[2][0]) / _s, (_m[0][1] + _m[1][0]) / _s, 0.25 * _s, (_m[1][2] + _m[2][1]) / _s
        else:
            _s = math.sqrt(1.0 + _m[2][2] - _m[0][0] - _m[1][1]) * 2
            _qw, _qx, _qy, _qz = (_m[1][0] - _m[0][1]) / _s, (_m[0][2] + _m[2][0]) / _s, (_m[1][2] + _m[2][1]) / _s, 0.25 * _s
        _q_item = unreal.Quat(_qx, _qy, _qz, _qw)  # the item mesh's own frame in hand space
        # The socket is not the item frame: ATSCUnit::AddWeapon maps DefShield's LocalX (item -Y) to socket +X and LocalZ
        # (item +Z) to socket +Z, so socket +Y = item +X. Socket rotation = item frame * inverse(that mapping).
        _map = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0, -1, 0), unreal.Vector(0, 0, 1)).quaternion()
        r_ax = _q_item.multiply(_map)
        _g = unreal.Vector(*_fit["grip"]) * _fit["unit"]
        loc = _mid - _q_item.rotate_vector(_g)
        log.append("%s fit: forearm %s body-perp %s" % (name, _f, _n))
    elif name in SOCKET_SHIFT:
        up_cm, out_cm = SOCKET_SHIFT[name]
        elbow = hand.inverse_transform_location(P.get_bone_pose(ref, FOREARM[abone], W).translation)
        loc = loc + elbow * (up_cm / elbow.length()) + r_ax.rotate_vector(unreal.Vector(out_cm, 0, 0))
    if name in SOCKET_OFFSET:
        loc = loc + r_ax0.rotate_vector(unreal.Vector(*SOCKET_OFFSET[name]))

    PLAN.append((name, abone, loc, r_ax.rotator()))
    log.append("%-8s on %-9s loc (%.2f, %.2f, %.2f) rot %s | Viking %s rot %s | check err deg: %s" % (
        name, abone, loc.x, loc.y, loc.z, r_ax.rotator(), vbone, vs.relative_rotation, ", ".join(errs)))



async def run():
    """Create the sockets. SkeletalMeshSocket.socket_name / bone_name are read-only in Python, so the
    EditorToolset AddSocket / SetSocketTransform tools do it (call_tool is provided by execute_python)."""
    mesh_ref = {"refPath": ax_mesh.get_path_name()}
    for name, abone, loc, rot in PLAN:
        await call_tool("EditorToolset.SkeletalMeshToolset.AddSocket", mesh=mesh_ref, socketName=name, boneName=abone)
        await call_tool("EditorToolset.SkeletalMeshToolset.SetSocketTransform", mesh=mesh_ref, socketName=name,
                        translation={"x": loc.x, "y": loc.y, "z": loc.z},
                        rotation={"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
                        scale={"x": 1.0, "y": 1.0, "z": 1.0})
    unreal.EditorAssetLibrary.save_loaded_asset(ax_skel, False)
    unreal.EditorAssetLibrary.save_loaded_asset(ax_mesh, False)
    got = []
    for i in range(ax_mesh.num_sockets()):
        s = ax_mesh.get_socket_by_index(i)
        got.append("%s on %s (%s) loc %s rot %s scale %s" % (s.socket_name, s.bone_name, s.get_outer().get_name(),
                                                             s.relative_location, s.relative_rotation, s.relative_scale))
    log.append("sockets now:\n  " + "\n  ".join(got))
    return "\n".join(log)

# Usage: CHAR = "<key>"; exec(open(r"<repo>/Scripts/ue/sockets_axolotl.py").read()); await run()
