"""Clean up a Tripo mushroom creature and skin it to a Bull-compatible quadruped skeleton for Unreal.

Run headless:
    blender.exe -b -P Scripts/blender/prep_quadruped.py -- <key> <tripo.fbx> <bull_ref.fbx> <out dir>

<key> is purple, cup or morel; <Key> below is KEY.capitalize().
Inputs
    <tripo.fbx>     the Tripo Studio export (textures in <tripo>.fbm/). Only its MESH is used: Tripo's "Other" rig is a
                    generic 16-bone tree (no clear four legs), so it is dropped (rig path = Blender fallback of
                    mushroom-chars/SONNET_PROMPT.md, step 2).
    <bull_ref.fbx>  /Game/Animals/Bull/Bull exported from Unreal (57 bones, AnimalArmature names).
The Bull armature is uniformly scaled to the mushroom, each of the four legs is moved under the real leg of the mesh,
the neck / head / tail chains are shrunk into the body, the IK / pole helper bones are dropped. All other bone names and
the hierarchy are kept, so the retarget in Unreal is a one-to-one chain mapping. Skinning is by rules, not by heat:
legs by distance to the leg axis and height, the rest of the body by position along the spine.
Output in <out dir>:
    SK_Mushroom<Key>.fbx          armature + mesh in cm, imports at real size
    T_Mushroom<Key>_*.jpg         base colour, normal, roughness, metallic
    <key>_bones.json              bones, roles (root, spine, four legs, neck, tail), feet, weight summary
    check_front/side/back/34.png  textured stills of the rest pose
    check_pose.png, check_pose2.png  posed stills: two diagonal legs swung 35 deg, then the other two
    check_weights.png             leg weight map (colour = dominant leg)
Blender axes: the creature faces -Y, Z up, left = +X, feet at Z = 0, pivot at the centre of the four feet. Same axes as the
Bull and the axolotls, so it lands +Y-facing in Unreal like both.
Exits with code 1 on any failure.
"""
import bpy
import json
import math
import os
import shutil
import sys
import traceback
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[sys.argv.index("-P") + 1] if "-P" in sys.argv else __file__)))
import prep_axolotl as pa  # noqa: E402  generic helpers only (clear_scene, select_only, renders); names are overridden below

# Per-creature settings.
#   yaw   degrees about +Z that turn the Tripo model so its front looks to -Y, or "lean": from the feet towards the top
#   zb    height (m) where the legs end under the cap / bowl / body: leg weights fade out between 0.8 zb and 1.15 zb
#   scale  multiplier on the creature's size in metres (1.0 keeps Tripo's size)
CFG = {
    "purple": {"yaw": 0.0, "zb": 0.20, "scale": 1.0},
    # cup: no clear front, the side facing the reference camera is -Y in the Tripo model
    "cup": {"yaw": 0.0, "zb": 0.28, "scale": 1.0},
    # morel: yaw "lean" turns the direction its cone tip leans to onto -Y (it rams with the tip). The four legs stand in a
    # diamond relative to that direction (one leg straight ahead), which no four-legged gait fits, so the model is turned a
    # further lean_offset degrees to put the leg rectangle square to the front; the tip then leans 45 deg to one side of it.
    "morel": {"yaw": "lean", "lean_offset": 45.0, "zb": 0.30, "scale": 1.0},
}
BONES_DROP_PREFIX = ("PoleTarget", "IK", "FF")
SPINE = ["Back", "Torso", "Torso2", "Torso3"]
NECK = ["Neck1", "Neck2", "Neck3", "Head", "Head_end"]
TAIL = ["Tail1", "Tail2", "Tail3", "Tail4", "Tail5", "Tail6", "Tail7", "Tail7_end"]
LEGS = {   # key -> (shoulder, [leg bones top to bottom]); L = +X, F = -Y
    "FL": ("FrontShoulder_L", ["FrontUpperLeg_L", "FrontLowerLeg_L", "FrontLowerLeg_L_end"]),
    "FR": ("FrontShoulder_R", ["FrontUpperLeg_R", "FrontLowerLeg_R", "FrontLowerLeg_R_end"]),
    "BL": ("BackShoulder_L", ["BackLeg_L", "BackUpperLeg_L", "BackLowerLeg_L", "BackLowerLeg_L_end"]),
    "BR": ("BackShoulder_R", ["BackLeg_R", "BackUpperLeg_R", "BackLowerLeg_R", "BackLowerLeg_R_end"]),
}
SHRINK = 0.30             # neck / head / tail chains are pulled to this fraction of their length, towards the body
FRONT_HIP_BULL_Z = 256.0  # Bull front upper-leg head height in the imported armature (cm): the scale is chosen so it lands at zb
MAX_INFLUENCES = 4
USAGE = "usage: blender -b -P prep_quadruped.py -- <key> <tripo.fbx> <bull_ref.fbx> <out dir>"


def log(*a):
    print("[quad]", *a, flush=True)


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def set_names(key):
    pas = key.capitalize()
    pa.KEY, pa.PASCAL = key, pas
    pa.MESH_NAME = f"SK_Mushroom{pas}"
    pa.MAT_NAME = f"M_Mushroom{pas}"
    pa.TEX_SUFFIXES = {k: f"T_Mushroom{pas}_{n}" for k, n in (("basecolor", "BaseColor"), ("normal", "Normal"),
                                                              ("roughness", "Roughness"), ("metallic", "Metallic"))}
    return pas


# --------------------------------------------------------------------------------------------- mesh

def import_mesh(path):
    pa.clear_scene()
    bpy.ops.import_scene.fbx(filepath=path, use_anim=False, ignore_leaf_bones=False, automatic_bone_orientation=False,
                             use_custom_normals=True)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    if not meshes:
        raise RuntimeError("no mesh in the Tripo FBX")
    for m in meshes:
        m.parent = None
        for mod in list(m.modifiers):
            m.modifiers.remove(mod)
        m.vertex_groups.clear()
    pa.select_only(meshes, meshes[0])
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
    mesh.name = mesh.data.name = pa.MESH_NAME
    return mesh


def mesh_points(mesh):
    return [mesh.matrix_world @ v.co for v in mesh.data.vertices]


def place_mesh(mesh, yaw_deg, scale):
    """Yaw to face -Y, scale, feet on Z = 0. Pivot (X/Y) is set later from the feet."""
    me = mesh.data
    m = Matrix.Rotation(math.radians(yaw_deg), 4, 'Z') @ Matrix.Scale(scale, 4)
    me.transform(m)
    mn_z = min(v.co.z for v in me.vertices)
    me.transform(Matrix.Translation((0, 0, -mn_z)))
    me.update()


def lean_yaw(mesh):
    """Yaw (deg) that turns the direction from the feet to the top of the model onto -Y."""
    pts = mesh_points(mesh)
    H = max(p.z for p in pts) - min(p.z for p in pts)
    z0 = min(p.z for p in pts)
    top = [p for p in pts if p.z > z0 + 0.93 * H]
    low = [p for p in pts if p.z < z0 + 0.10 * H]
    tx, ty = sum(p.x for p in top) / len(top), sum(p.y for p in top) / len(top)
    lx, ly = sum(p.x for p in low) / len(low), sum(p.y for p in low) / len(low)
    phi = math.degrees(math.atan2(ty - ly, tx - lx))
    log(f"lean: top ({tx:.3f}, {ty:.3f}) over feet ({lx:.3f}, {ly:.3f}) -> direction {phi:.1f} deg")
    return -90.0 - phi


def kmeans4(low):
    """k-means (k = 4) on the XY of the lowest vertices, seeded by farthest points. Returns 4 lists of points."""
    c = [Vector((sum(p.x for p in low) / len(low), sum(p.y for p in low) / len(low), 0))]
    for _ in range(4):
        far = max(low, key=lambda p: min((Vector((p.x, p.y, 0)) - q).length for q in c))
        c.append(Vector((far.x, far.y, 0)))
    c = c[1:]
    for _ in range(30):
        groups = [[] for _ in range(4)]
        for p in low:
            i = min(range(4), key=lambda j: (Vector((p.x, p.y, 0)) - c[j]).length)
            groups[i].append(p)
        new = [Vector((sum(p.x for p in g) / len(g), sum(p.y for p in g) / len(g), 0)) if g else c[i] for i, g in enumerate(groups)]
        if all((a - b).length < 1e-5 for a, b in zip(new, c)):
            break
        c = new
    return groups


def find_feet(mesh):
    """Four foot centres (FL FR BL BR) from the lowest vertices (k-means). Front is -Y, left is +X."""
    pts = mesh_points(mesh)
    H = max(p.z for p in pts)
    low = [p for p in pts if p.z < 0.10 * H]
    groups = kmeans4(low)
    cent = [Vector((sum(p.x for p in g) / len(g), sum(p.y for p in g) / len(g), 0)) for g in groups]
    order = sorted(range(4), key=lambda i: cent[i].y)
    front, back = order[:2], order[2:]
    names = {}
    for pair, tag in ((front, "F"), (back, "B")):
        a, b = sorted(pair, key=lambda i: -cent[i].x)   # larger X first = left
        names[a], names[b] = tag + "L", tag + "R"
    feet, radius = {}, []
    for i, g in enumerate(groups):
        k = names[i]
        if len(g) < 30:
            raise RuntimeError(f"foot {k}: only {len(g)} low vertices, the model has no clean four-leg stance")
        sole = [p for p in g if p.z < 0.04 * H] or g
        c = Vector((sum(p.x for p in sole) / len(sole), sum(p.y for p in sole) / len(sole), 0.0))
        feet[k] = c
        radius.append(sum((Vector((p.x, p.y, 0)) - c).length for p in sole) / len(sole))
        log(f"foot {k}: {len(g)} low verts, centre ({c.x:.3f}, {c.y:.3f}), mean radius {radius[-1]:.3f}")
    return feet, sum(radius) / 4.0, H


def recentre(mesh, feet):
    """Move the pivot (X/Y) to the centre of the four feet."""
    cx = sum(f.x for f in feet.values()) / 4
    cy = sum(f.y for f in feet.values()) / 4
    mesh.data.transform(Matrix.Translation((-cx, -cy, 0)))
    mesh.data.update()
    return {k: Vector((f.x - cx, f.y - cy, 0.0)) for k, f in feet.items()}


# --------------------------------------------------------------------------------------------- armature

def import_bull(path):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(filepath=path, use_anim=False, ignore_leaf_bones=False, automatic_bone_orientation=False)
    new = [o for o in bpy.context.scene.objects if o not in before]
    arm = [o for o in new if o.type == 'ARMATURE'][0]
    for o in new:
        if o is not arm and o.type != 'ARMATURE':
            bpy.data.objects.remove(o, do_unlink=True)
    pa.select_only([arm], arm)
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if any(abs(a - b) > 1e-4 for a, b in zip(arm.matrix_world.to_scale(), (1, 1, 1))):
        raise RuntimeError("bull armature still scaled")
    arm.name = arm.data.name = "AnimalArmature"
    return arm


def fit_armature(arm, feet, zb):
    """Uniform scale, legs under the real legs, neck / head / tail shrunk, IK / pole bones dropped."""
    k = zb * 1.05 / FRONT_HIP_BULL_Z
    pa.select_only([arm], arm)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.data.edit_bones
    for b in list(eb):
        if b.name.startswith(BONES_DROP_PREFIX):
            eb.remove(b)
    orig = {b.name: (b.head.copy(), b.tail.copy()) for b in eb}
    for b in eb:
        b.use_connect = False

    def sc(v):
        return v * k

    new = {n: (sc(h), sc(t)) for n, (h, t) in orig.items()}

    # spine centre sits above the centre of the feet
    bull_feet = [sc(orig[ch[-1]][0]) for _, ch in LEGS.values()]
    bcx = sum(v.x for v in bull_feet) / 4
    bcy = sum(v.y for v in bull_feet) / 4
    shift = Vector((-bcx, -bcy, 0.0))
    new = {n: (h + shift, t + shift) for n, (h, t) in new.items()}

    # legs: each chain moves rigidly under its foot; the shoulder keeps its head and follows with its tail
    moves = {}
    for lk, (sh, chain) in LEGS.items():
        ankle = new[chain[-1]][0]                        # head of the *_end bone, the ankle above the sole
        d = Vector((feet[lk].x - ankle.x, feet[lk].y - ankle.y, 0.0))
        moves[lk] = d
        for n in chain:
            h, t = new[n]
            new[n] = (h + d, t + d)
        h, t = new[sh]
        new[sh] = (h, t + d)

    # neck / head: towards Torso3 head, tail: towards Back head
    def shrink(chain, pivot):
        for n in chain:
            h, t = new[n]
            new[n] = (pivot + (h - pivot) * SHRINK, pivot + (t - pivot) * SHRINK)

    shrink(NECK, new["Torso3"][1])
    shrink(TAIL, new["Back"][0])

    for n, (h, t) in new.items():
        b = eb[n]
        b.head, b.tail = h, t
    # reconnect where the original chain was connected (head == parent tail), so Unreal sees the same hierarchy
    bpy.ops.object.mode_set(mode='OBJECT')
    log(f"armature: scale {k:.4f}, {len(arm.data.bones)} bones, leg moves "
        + ", ".join(f"{lk} ({d.x:+.3f},{d.y:+.3f})" for lk, d in moves.items()))
    return k


# --------------------------------------------------------------------------------------------- weights

def bone_pts(arm, name):
    b = arm.data.bones[name]
    return b.head_local.copy(), b.tail_local.copy()


def hat_weights(centres, x):
    """centres: list of (value, name) sorted by value ascending. Linear 'hat' weights of x; clamped at both ends."""
    if x <= centres[0][0]:
        return {centres[0][1]: 1.0}
    if x >= centres[-1][0]:
        return {centres[-1][1]: 1.0}
    for (a, na), (b, nb) in zip(centres, centres[1:]):
        if a <= x <= b:
            t = (x - a) / (b - a) if b > a else 0.0
            return {na: 1.0 - t, nb: t}
    return {centres[-1][1]: 1.0}


def skin(arm, mesh, feet, leg_r, zb):
    me = mesh.data
    mesh.vertex_groups.clear()
    deform = [n for lk in LEGS for n in LEGS[lk][1]] + SPINE
    vg = {n: mesh.vertex_groups.new(name=n) for n in deform}
    # leg height centres (z of the bone midpoint), per leg
    leg_c = {}
    for lk, (_, chain) in LEGS.items():
        cs = []
        for n in chain:
            h, t = bone_pts(arm, n)
            cs.append(((h.z + t.z) / 2, n))
        leg_c[lk] = sorted(cs)
    sp = []
    for n in SPINE:
        h, t = bone_pts(arm, n)
        sp.append(((h.y + t.y) / 2, n))
    sp.sort()
    r0, r1 = leg_r * 1.1, leg_r * 2.4
    z_full, z_zero = 0.8 * zb, 1.15 * zb
    dom_leg = {}
    stats = {"leg_verts": 0}
    for v in me.vertices:
        p = mesh.matrix_world @ v.co
        fz = 1.0 - smooth((p.z - z_full) / (z_zero - z_full))
        g = {}
        if fz > 0:
            for lk, f in feet.items():
                d = math.hypot(p.x - f.x, p.y - f.y)
                gi = (1.0 - smooth((d - r0) / (r1 - r0))) * fz
                if gi > 1e-4:
                    g[lk] = gi
        tot = sum(g.values())
        if tot > 1.0:
            g = {k_: w / tot for k_, w in g.items()}
            tot = 1.0
        w = {}
        for lk, gi in g.items():
            for n, hw in hat_weights(leg_c[lk], p.z).items():
                w[n] = w.get(n, 0.0) + gi * hw
        if g:
            dom_leg[v.index] = max(g, key=g.get)
            stats["leg_verts"] += 1
        for n, hw in hat_weights(sp, p.y).items():
            w[n] = w.get(n, 0.0) + (1.0 - tot) * hw
        # keep the 4 largest, renormalise
        top = sorted(w.items(), key=lambda t: -t[1])[:MAX_INFLUENCES]
        s = sum(x for _, x in top) or 1.0
        for n, x in top:
            if x / s > 1e-4:
                vg[n].add([v.index], x / s, 'REPLACE')
    log(f"skinning: {stats['leg_verts']} vertices carry leg weight, leg radius {leg_r:.3f}, zb {zb:.3f}")
    return dom_leg


def check_weights(mesh):
    me = mesh.data
    names = {g.index: g.name for g in mesh.vertex_groups}
    none = bad = mx = 0
    for v in me.vertices:
        ws = [g.weight for g in v.groups if g.weight > 0]
        if not ws:
            none += 1
            continue
        mx = max(mx, len(ws))
        if abs(sum(ws) - 1.0) > 1e-3:
            bad += 1
    log(f"weights: unweighted={none} max_influences={mx} not_normalised={bad}")
    if none or bad or mx > MAX_INFLUENCES:
        raise RuntimeError("weight check failed")
    return {"unweighted": none, "max_influences": mx}


def attach(arm, mesh):
    pa.select_only([mesh, arm], arm)
    bpy.ops.object.parent_set(type='OBJECT', keep_transform=True)
    for m in list(mesh.modifiers):
        mesh.modifiers.remove(m)
    mod = mesh.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm


# --------------------------------------------------------------------------------------------- export, json, stills

def write_json(arm, path, bounds, feet, extra):
    bones = arm.data.bones

    def r(v):
        return [round(x, 5) for x in v]
    roles = {
        "root": "Body",
        "spine_chain": SPINE,
        "neck_chain": NECK,
        "tail_chain": TAIL,
        "legs": {lk: [LEGS[lk][0]] + LEGS[lk][1] for lk in LEGS},
        "front": "-Y (head of the creature, ram direction)",
    }
    for n in [roles["root"]] + SPINE + NECK + TAIL + [b for lk in LEGS for b in [LEGS[lk][0]] + LEGS[lk][1]]:
        if n not in bones:
            raise RuntimeError("missing bone " + n)
    data = {
        "source": "Tripo Studio mesh (rig dropped) + Bull skeleton fitted in Blender",
        "units": "metres, Blender axes (Z up, faces -Y, left = +X); the FBX is in cm and lands +Y-facing in UE",
        "height_m": round(bounds[1].z - bounds[0].z, 4),
        "bounds_min": r(bounds[0]), "bounds_max": r(bounds[1]),
        "bone_count": len(bones),
        "roles": roles,
        "feet_m": {k: r(v) for k, v in feet.items()},
        "bones": [{"name": b.name, "parent": b.parent.name if b.parent else None,
                   "head": r(b.head_local), "tail": r(b.tail_local), "length": round(b.length, 5)} for b in bones],
    }
    data.update(extra)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    log(f"wrote {path}: {len(bones)} bones")


def to_cm_and_export(arm, mesh, path):
    if arm.animation_data:
        arm.animation_data_clear()
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.01
    pa.select_only([arm, mesh], arm)
    arm.scale = (100.0, 100.0, 100.0)
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'}, global_scale=1.0, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y', use_mesh_modifiers=False,
        mesh_smooth_type='FACE', use_tspace=False, add_leaf_bones=False, primary_bone_axis='Y', secondary_bone_axis='X',
        use_armature_deform_only=False, armature_nodetype='NULL', bake_anim=False, path_mode='STRIP',
        embed_textures=False, use_custom_props=False)
    log(f"exported {path} ({os.path.getsize(path)} bytes)")


def verify_export(path, height_m):
    pa.clear_scene()
    bpy.ops.import_scene.fbx(filepath=path, use_anim=False, ignore_leaf_bones=False, automatic_bone_orientation=False)
    arm = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE'][0]
    mesh = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
    zs = [(mesh.matrix_world @ v.co).z for v in mesh.data.vertices]
    names = [b.name for b in arm.data.bones]
    log(f"re-import: {len(names)} bones, scale {tuple(round(s, 3) for s in arm.scale)}, height {max(zs) - min(zs):.2f} cm")
    if abs((max(zs) - min(zs)) - height_m * 100) > 0.02 * height_m * 100:
        raise RuntimeError("re-imported FBX is not in centimetres")
    if "Body" not in names or "FrontLowerLeg_L_end" not in names:
        raise RuntimeError("expected bones missing after export")


def render_weights(mesh, cam, bounds, dom_leg, path):
    me = mesh.data
    palette = {"FL": (0.9, 0.15, 0.15, 1), "FR": (0.15, 0.4, 0.95, 1), "BL": (0.95, 0.85, 0.1, 1), "BR": (0.2, 0.8, 0.3, 1)}
    names = {g.index: g.name for g in mesh.vertex_groups}
    attr = me.color_attributes.new("WeightCheck", 'FLOAT_COLOR', 'POINT')
    for v in me.vertices:
        legw = sum(g.weight for g in v.groups if names[g.group] != "" and any(names[g.group] in LEGS[lk][1] for lk in LEGS))
        c = palette[dom_leg[v.index]] if v.index in dom_leg and legw > 0.5 else (0.6, 0.6, 0.6, 1)
        attr.data[v.index].color = c
    me.color_attributes.active_color = attr
    sh = bpy.context.scene.display.shading
    prev = sh.color_type
    sh.color_type = 'VERTEX'
    pa.render_view(cam, bounds, (0.5, -1.0, 0.15), path)
    sh.color_type = prev
    me.color_attributes.remove(attr)


def render_pose(arm, cam, bounds, path, bones, view, deg=35):
    for b in bones:
        pa.rotate_bone_world(arm, b, 'X', deg)
    pa.render_view(cam, bounds, view, path)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def main():
    argv = sys.argv
    if "--" not in argv or len(argv[argv.index("--") + 1:]) < 4:
        raise SystemExit(USAGE)
    key, tripo, bull, out_dir = argv[argv.index("--") + 1:][:4]
    key = key.strip().lower()
    if key not in CFG:
        raise SystemExit(f"unknown key {key}; known: {list(CFG)}")
    tripo, bull, out_dir = (os.path.abspath(p) for p in (tripo, bull, out_dir))
    cfg = CFG[key]
    os.makedirs(out_dir, exist_ok=True)
    pas = set_names(key)
    log(f"character {key} ({pas})\ninput {tripo}\nbull {bull}\noutput {out_dir}")

    mesh = import_mesh(tripo)
    mat = pa.single_material(mesh)
    if mat:
        mat.name = pa.MAT_NAME
    loose = pa.delete_small_loose_parts(mesh)
    pa.maybe_decimate(mesh)
    log(f"triangles {pa.tri_count(mesh)}, vertices {len(mesh.data.vertices)}")
    yaw = lean_yaw(mesh) + cfg.get("lean_offset", 0.0) if cfg["yaw"] == "lean" else cfg["yaw"]
    cfg = dict(cfg, yaw=round(yaw, 2))
    place_mesh(mesh, yaw, cfg["scale"])
    feet, leg_r, H = find_feet(mesh)
    feet = recentre(mesh, feet)

    arm = import_bull(bull)
    fit_armature(arm, feet, cfg["zb"])
    dom_leg = skin(arm, mesh, feet, leg_r, cfg["zb"])
    wsum = check_weights(mesh)
    attach(arm, mesh)

    pts = mesh_points(mesh)
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    log(f"bounds min {tuple(round(v, 3) for v in mn)} max {tuple(round(v, 3) for v in mx)} height {mx.z - mn.z:.3f} m")
    if abs(mn.z) > 1e-3:
        raise RuntimeError("feet are not on z = 0")

    write_json(arm, os.path.join(out_dir, f"{key}_bones.json"), (mn, mx), feet,
               {"mesh": {"triangles": pa.tri_count(mesh), "vertices": len(mesh.data.vertices), "material": pa.MAT_NAME,
                         "loose_parts": loose}, "weights": wsum, "settings": cfg})
    fbm = os.path.splitext(tripo)[0] + ".fbm"
    pa.copy_textures(fbm, out_dir, mat)

    cam = pa.setup_render(mesh, (mn, mx))
    for name, d in {"front": (0, -1, 0), "side": (1, 0, 0), "back": (0, 1, 0), "34": (-0.7, -1.0, 0.25)}.items():
        pa.render_view(cam, (mn, mx), d, os.path.join(out_dir, f"check_{name}.png"))
    # FL + BR forward 35 deg, seen from the figure's left front; then FR + BL
    render_pose(arm, cam, (mn, mx), os.path.join(out_dir, "check_pose.png"),
                ["FrontUpperLeg_L", "BackLeg_R"], (0.9, -0.6, 0.2))
    render_pose(arm, cam, (mn, mx), os.path.join(out_dir, "check_pose2.png"),
                ["FrontUpperLeg_R", "BackLeg_L"], (-0.9, -0.6, 0.2))
    if bpy.context.scene.render.engine == 'BLENDER_WORKBENCH':
        render_weights(mesh, cam, (mn, mx), dom_leg, os.path.join(out_dir, "check_weights.png"))

    fbx = os.path.join(out_dir, f"{pa.MESH_NAME}.fbx")
    to_cm_and_export(arm, mesh, fbx)
    verify_export(fbx, mx.z - mn.z)
    log("done")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:  # noqa: BLE001
        traceback.print_exc()
        sys.stdout.flush()
        sys.exit(1)
    sys.exit(0)
