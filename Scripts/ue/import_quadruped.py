# The Saga Cutter - game jam project.
# Imports one Tripo mushroom creature (quadruped, Bull-compatible skeleton) into /Game/Characters/mushroom_<key>
# (idempotent: reimports over existing assets). Made from import_axolotl.py; source: Art/Tripo/mushroom_<key>/ (Scripts/blender/prep_quadruped.py).
# Run inside the editor (CHAR defaults to "warrior"; REPO defaults to the repo path below):
#   CHAR = "purple"; exec(open(r"<repo>/Scripts/ue/import_quadruped.py").read())
#   Mushroom_<Key> (skeletal mesh), Mushroom_<Key>_Skeleton, M_Mushroom<Key>, T_Mushroom<Key>_{BaseColor,Normal,Roughness,Metallic}
# No animation; materials and textures are NOT taken from the FBX.
import os
import unreal

if "CHAR" not in globals():
    CHAR = "purple"
PASCAL = CHAR.capitalize()
if "REPO" not in globals():
    REPO = r"C:\Users\Sega\Documents\Projects\TheSagaCutter"
SRC = os.path.join(REPO, "Art", "Tripo", "mushroom_" + CHAR)
FOLDER = "/Game/Characters/mushroom_" + CHAR
MESH_NAME = "Mushroom_" + PASCAL
SKEL_NAME = MESH_NAME + "_Skeleton"
MAT_NAME = "M_Mushroom" + PASCAL
TEX_PREFIX = "T_Mushroom" + PASCAL + "_"
FBX_NAME = "SK_Mushroom" + PASCAL + ".fbx"
CLIP_PREFIX = MESH_NAME + "AnimalArmature_"
IK_RIG_NAME = "IK_Mushroom_" + PASCAL
RTG_NAME = "RTG_Bull_To_Mushroom_" + PASCAL
# The FBX carries real centimetre data (Blender scene scale_length 0.01, exported with FBX_SCALE_NONE): bones and
# vertices in cm, no node scale, UnitScaleFactor 1. Imported as is, with no unit conversion and no uniform scale, so the
# mesh reference pose equals the skeleton reference pose (scale 1 on every bone).
SCALE = 1.0
TEXTURES = ["BaseColor", "Normal", "Roughness", "Metallic"]
# Set FORCE_TEXTURES_AND_MATERIAL = True before exec to reimport the textures and rebuild the material too.
if "FORCE_TEXTURES_AND_MATERIAL" not in globals():
    FORCE_TEXTURES_AND_MATERIAL = False
EAL = unreal.EditorAssetLibrary
CLIPS = ["Idle", "Walk", "Gallop", "Attack_Headbutt", "Idle_HitReact_Left", "Death"]
# Emissive glow from the bright pixels of the base colour (purple: the two eyes). key -> (luminance threshold, gain, strength)
EMISSIVE = {"purple": (0.85, 20.0, 6.0)}

if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.make_directory(FOLDER)


def run_task(filename, name, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", filename)
    t.set_editor_property("destination_path", FOLDER)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    if options is not None:
        t.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return t


# --- Textures ---
for kind in TEXTURES:
    name = TEX_PREFIX + kind
    if not FORCE_TEXTURES_AND_MATERIAL and EAL.does_asset_exist("%s/%s" % (FOLDER, name)):
        continue
    run_task(os.path.join(SRC, name + ".jpg"), name)
    tex = unreal.load_asset("%s/%s.%s" % (FOLDER, name, name))
    if kind == "BaseColor":
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    else:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)

# --- Skeletal mesh ---
# A rerun over an existing mesh would be an atomic reimport that keeps the old import settings, so the mesh is deleted first (the skeleton stays).
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("create_physics_asset", False)
ui.set_editor_property("automated_import_should_detect_type", False)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.skeletal_mesh_import_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", False)
ui.skeletal_mesh_import_data.set_editor_property("convert_scene", True)
ui.skeletal_mesh_import_data.set_editor_property("convert_scene_unit", False)
ui.skeletal_mesh_import_data.set_editor_property("import_uniform_scale", SCALE)

# Mesh AND skeleton are recreated on every run (a reused skeleton keeps its old reference pose). Their dependents go
# first: the retargeted clips, the retargeter and the mushroom IK Rig (Scripts/ue/retarget_quadruped.py rebuilds them).
unreal.SystemLibrary.collect_garbage()
for p in ["%s/%s%s" % (FOLDER, CLIP_PREFIX, c) for c in CLIPS] + [
        FOLDER + "/" + RTG_NAME, FOLDER + "/" + IK_RIG_NAME,
        "%s/%s" % (FOLDER, MESH_NAME), "%s/%s" % (FOLDER, SKEL_NAME)]:
    if EAL.does_asset_exist(p):
        if not EAL.delete_asset(p):
            raise RuntimeError("[mushroom] could not delete " + p)
# Purge the deleted objects from memory, or the importer finds the old skeleton object by name and merges into it
# (keeping its stale reference pose).
unreal.SystemLibrary.collect_garbage()

task = run_task(os.path.join(SRC, FBX_NAME), MESH_NAME, ui)
imported = list(task.get_editor_property("imported_object_paths"))
mesh = unreal.load_asset("%s/%s.%s" % (FOLDER, MESH_NAME, MESH_NAME))
if not mesh:
    raise RuntimeError("[mushroom] mesh import failed: %s" % imported)

# The importer names a new skeleton <mesh>_Skeleton, which is exactly SKEL_NAME. Verify.
skel = mesh.get_editor_property("skeleton")
if skel.get_name() != SKEL_NAME:
    unreal.EditorAssetLibrary.rename_asset(skel.get_path_name().split(".")[0], "%s/%s" % (FOLDER, SKEL_NAME))
    skel = mesh.get_editor_property("skeleton")

# --- Material ---
MAT_PATH = "%s/%s" % (FOLDER, MAT_NAME)
MEL = unreal.MaterialEditingLibrary
mat = unreal.load_asset(MAT_PATH + "." + MAT_NAME)
BUILD_MAT = FORCE_TEXTURES_AND_MATERIAL or not mat
if mat and BUILD_MAT:
    MEL.delete_all_material_expressions(mat)
elif not mat:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MAT_NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def build_material(mat):
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, getattr(unreal, "MaterialExpression" + cls), x, y)


    def tex_node(kind, x, y, sampler):
        n = expr("TextureSampleParameter2D", x, y)
        n.set_editor_property("parameter_name", kind)
        n.set_editor_property("texture", unreal.load_asset("%s/%s%s.%s%s" % (FOLDER, TEX_PREFIX, kind, TEX_PREFIX, kind)))
        n.set_editor_property("sampler_type", sampler)
        return n


    S = unreal.MaterialSamplerType
    base = tex_node("BaseColor", -900, -300, S.SAMPLERTYPE_COLOR)
    nrm = tex_node("Normal", -900, 0, S.SAMPLERTYPE_NORMAL)
    rough = tex_node("Roughness", -900, 300, S.SAMPLERTYPE_MASKS)
    metal = tex_node("Metallic", -900, 600, S.SAMPLERTYPE_MASKS)

    diffuse = expr("VectorParameter", -900, -550)
    diffuse.set_editor_property("parameter_name", "DiffuseColor")
    diffuse.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    metal_scale = expr("ScalarParameter", -900, 850)
    metal_scale.set_editor_property("parameter_name", "MetallicScale")
    metal_scale.set_editor_property("default_value", 0.5)

    mul_col = expr("Multiply", -500, -350)
    MEL.connect_material_expressions(base, "RGB", mul_col, "A")
    MEL.connect_material_expressions(diffuse, "", mul_col, "B")

    mul_met = expr("Multiply", -500, 650)
    MEL.connect_material_expressions(metal, "R", mul_met, "A")
    MEL.connect_material_expressions(metal_scale, "", mul_met, "B")

    if CHAR in EMISSIVE:
        thr, gain, strength = EMISSIVE[CHAR]
        lum = expr("Constant3Vector", -900, -800)
        lum.set_editor_property("constant", unreal.LinearColor(0.3, 0.6, 0.1, 1.0))
        dot = expr("DotProduct", -700, -700)
        MEL.connect_material_expressions(base, "RGB", dot, "A")
        MEL.connect_material_expressions(lum, "", dot, "B")
        sub = expr("Subtract", -500, -700)
        thr_c = expr("Constant", -700, -600)
        thr_c.set_editor_property("r", thr)
        MEL.connect_material_expressions(dot, "", sub, "A")
        MEL.connect_material_expressions(thr_c, "", sub, "B")
        mul_g = expr("Multiply", -300, -700)
        gain_c = expr("Constant", -500, -600)
        gain_c.set_editor_property("r", gain)
        MEL.connect_material_expressions(sub, "", mul_g, "A")
        MEL.connect_material_expressions(gain_c, "", mul_g, "B")
        sat = expr("Saturate", -150, -700)
        MEL.connect_material_expressions(mul_g, "", sat, "")
        mask_col = expr("Multiply", 0, -700)
        MEL.connect_material_expressions(base, "RGB", mask_col, "A")
        MEL.connect_material_expressions(sat, "", mask_col, "B")
        str_c = expr("ScalarParameter", 0, -600)
        str_c.set_editor_property("parameter_name", "EmissiveStrength")
        str_c.set_editor_property("default_value", strength)
        emi = expr("Multiply", 200, -700)
        MEL.connect_material_expressions(mask_col, "", emi, "A")
        MEL.connect_material_expressions(str_c, "", emi, "B")
        MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.connect_material_property(mul_col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(mul_met, "", unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)


if BUILD_MAT:
    build_material(mat)

# --- Assign to slot(s) ---
mats = list(mesh.get_editor_property("materials"))
for i, m in enumerate(mats):
    m.set_editor_property("material_interface", mat)
    mats[i] = m
mesh.set_editor_property("materials", mats)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.EditorAssetLibrary.save_loaded_asset(skel)

# --- Report ---
bb = mesh.get_bounds()
names = [str(n) for n in skel.get_reference_pose().get_bone_names()]
print("[mushroom] %s: bounds origin=%s extent=%s (height %.2f cm)" % (CHAR, bb.origin, bb.box_extent, bb.box_extent.z * 2))
print("[mushroom] bones=%d first=%s roots=%s" % (len(names), names[:1], [n for n in names if n in ("Body", "AnimalArmature")]))
print("[mushroom] slots=%s" % [str(m.get_editor_property("material_slot_name")) for m in mats])
unreal.EditorAssetLibrary.save_directory(FOLDER)
