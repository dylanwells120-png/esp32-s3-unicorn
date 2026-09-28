"""Lightweight table the hologram sits on.

Four straight legs and an open top. The hood drops into a shallow recess so
it cannot slide off. Prints with the tabletop on the bed and the legs pointing
up, so there are no bridges.

Run from the repo root:
    /Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_stand_blender.py

Writes, next to this file:
    hologram_stand.blend
    stand.stl
    stand_preview.png

All sizes are millimetres (1 Blender unit = 1 mm).
"""

import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(OUT))
import hologram_box_blender as holo  # noqa: E402

PLAY = 0.6               # gap around the hood in the recess
TRAY_WALL = 2.4
LEDGE = 7.0              # rim the hood sits on; the middle of the top is open
RECESS = 4.0             # how far the hood sinks into the top
LEG = 7.0                # square leg thickness
LEG_H = 58.0
FLOOR = 1.6              # thickness of the ledge

BOX_W = holo.W
BOX_D = holo.D
INNER_W = BOX_W + 2 * PLAY
INNER_D = BOX_D + 2 * PLAY
STAND_W = INNER_W + 2 * TRAY_WALL
STAND_D = INNER_D + 2 * TRAY_WALL
TOP_Z = LEG_H
TOP_H = FLOOR + RECESS

SEG = 24


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.001
    scene.unit_settings.length_unit = "MILLIMETERS"
    return scene


def collection(name, hidden=False):
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    if hidden:
        coll.hide_render = True
    return coll


def material(name, rgba):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = rgba
    return mat


def mesh_object(name, bm, coll, location, mat=None):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    obj.location = location
    coll.objects.link(obj)
    if mat:
        me.materials.append(mat)
    return obj


def box(name, coll, x0, y0, z0, x1, y1, z1, mat=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(x1 - x0, y1 - y0, z1 - z0), verts=bm.verts)
    return mesh_object(name, bm, coll, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), mat)


def boolean(target, cutter, op="DIFFERENCE"):
    mod = target.modifiers.new(f"{op.title()} {cutter.name}", "BOOLEAN")
    mod.operation = op
    mod.object = cutter
    mod.solver = "EXACT"
    return mod


def cutter_style(obj):
    obj.display_type = "WIRE"
    obj.hide_render = True


def build(print_coll, cutters, ref):
    dark = material("Black PLA", (0.07, 0.07, 0.08, 1))
    box_mat = material("Hologram", (0.12, 0.43, 0.85, 1))

    stand = box("Stand", print_coll, 0, 0, TOP_Z, STAND_W, STAND_D, TOP_Z + TOP_H, dark)
    recess = box("Recess", cutters, TRAY_WALL, TRAY_WALL, TOP_Z + FLOOR,
                 STAND_W - TRAY_WALL, STAND_D - TRAY_WALL, TOP_Z + TOP_H + 1)
    opening = box("Top opening", cutters, TRAY_WALL + LEDGE, TRAY_WALL + LEDGE, TOP_Z - 1,
                  STAND_W - TRAY_WALL - LEDGE, STAND_D - TRAY_WALL - LEDGE, TOP_Z + FLOOR + 0.4)
    boolean(stand, recess)
    boolean(stand, opening)
    inset = 1.2
    for (x0, y0) in (
        (inset, inset),
        (STAND_W - inset - LEG, inset),
        (inset, STAND_D - inset - LEG),
        (STAND_W - inset - LEG, STAND_D - inset - LEG),
    ):
        leg = box("Leg", cutters, x0, y0, 0, x0 + LEG, y0 + LEG, TOP_Z + 0.4)
        boolean(stand, leg, "UNION")

    for obj in cutters.objects:
        cutter_style(obj)

    # Reference only: the hood sitting in the recess.
    case = box("Hood (reference)", ref, TRAY_WALL + PLAY, TRAY_WALL + PLAY, TOP_Z + FLOOR + 0.2,
               TRAY_WALL + PLAY + BOX_W, TRAY_WALL + PLAY + BOX_D, TOP_Z + FLOOR + 0.2 + holo.HOOD_H + holo.LID_H,
               box_mat)
    return stand, case


def evaluated_triangles(obj, transform=Matrix.Identity(4)):
    deps = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(deps)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    mw = transform @ obj.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    tris = [tuple(verts[i] for i in t.vertices) for t in me.loop_triangles]
    ev.to_mesh_clear()
    return tris


def write_stl(obj, path, transform=Matrix.Identity(4)):
    tris = evaluated_triangles(obj, transform)
    lo = Vector((min(p[i] for t in tris for p in t) for i in range(3)))
    hi = Vector((max(p[i] for t in tris for p in t) for i in range(3)))
    shift = Vector((-lo.x, -lo.y, -lo.z))
    with open(path, "wb") as f:
        f.write(b"hologram stand".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            a, b, c = a + shift, b + shift, c + shift
            n = (b - a).cross(c - a).normalized()
            f.write(struct.pack("<12fH", *n, *a, *b, *c, 0))
    size = hi - lo
    print(f"wrote {path.name}: {len(tris)} triangles, {size.x:.1f} x {size.y:.1f} x {size.z:.1f} mm")


def volume(obj):
    deps = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    bm.from_object(obj, deps)
    bm.transform(obj.matrix_world)
    vol = abs(bm.calc_volume())
    bm.free()
    return vol


def render_preview(scene, parts, path):
    stand, case = parts
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    shading.show_object_outline = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1200
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("World")
    scene.world = world
    world.color = (0.92, 0.92, 0.94)
    scene.display.shading.background_type = "WORLD"
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Camera", cam_data)
    scene.collection.objects.link(cam)
    target = Vector((STAND_W / 2, STAND_D / 2, LEG_H * 0.45))
    cam.location = target + Vector((-180, -260, 120))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    print(f"wrote {path.name}")


def main():
    scene = reset_scene()
    print_coll = collection("Print")
    cutters = collection("Cutters", hidden=True)
    ref = collection("Reference")
    stand, case = build(print_coll, cutters, ref)
    print(f"stand {STAND_W:.1f} x {STAND_D:.1f} x {LEG_H + TOP_H:.1f} mm")
    print(f"recess {INNER_W:.1f} x {INNER_D:.1f} x {RECESS:.1f} mm for the hood")
    # Legs up, so the tabletop is the first thing on the bed.
    write_stl(stand, OUT / "stand.stl", Matrix.Rotation(math.pi, 4, "X"))
    print(f"Stand: {volume(stand) / 1000:.1f} cm^3 of plastic")
    render_preview(scene, (stand, case), OUT / "stand_preview.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hologram_stand.blend"))
    print("wrote hologram_stand.blend")


if __name__ == "__main__":
    main()
