"""Short bartop the existing hologram box slides into.

The rectangular case from hologram_box_blender.py is unchanged. This cabinet
is a dock: slide the assembled box in from the back, lid on. The screen
is in that lid. The marquee drops on top and a rear flap traps the box.

Run from the repo root:
    /Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_arcade_blender.py

Writes, next to this file:
    hologram_arcade.blend
    arcade_cabinet.stl arcade_marquee.stl
    arcade_preview.png

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

# Outer size of the printed box, lid included. The screen is in the lid.
BOX_W = holo.W
BOX_D = holo.D
BOX_H = holo.BASE_H + holo.HOOD_H + holo.LID_H
BOX_VIEW_X0 = holo.HX0
BOX_VIEW_X1 = holo.HX1
BOX_VIEW_Z0 = holo.BASE_H + holo.PLATE
BOX_VIEW_Z1 = holo.BASE_H + holo.HOOD_H
BOX_SD_Y = holo.PY0 + holo.POCKET_D / 2
BOX_SD_W = holo.SD_SLOT_W
BOX_SD_Z0 = holo.LID_Z + holo.LID_PLATE + holo.SD_SLOT_BOTTOM
BOX_SD_Z1 = holo.LID_Z + holo.LID_H

# --- Dock fit --------------------------------------------------------------
PLAY = 0.8               # clearance around the box on left/right
BACK_PLAY = 2.4          # room behind the box for the marquee lock flap
SIDE = 1.6               # cabinet side walls
BEZEL = 1.6              # front monitor frame
SHELF = 1.6              # plate the box sits on
FLOOR = 1.2
WALL = 1.6               # shell walls
RIB = 1.6

# --- Short shell ------------------------------------------------------------
# A tall pedestal is almost all of the print time. This is only tall enough
# for the slanted deck and the shelf the box sits on.
LOWER_H = 18.0
PANEL_D = 42.0           # control deck, in front of the box
PANEL_DROP = 6.0         # deck slants down toward the player
MARQUEE_H = 16.0
MARQUEE_OVER = 14.0
MARQUEE_DEPTH = 28.0
MARQUEE_SLANT = 12.0
MARQUEE_WALL = 1.2
MARQUEE_LIP = 3.0
FLAP_H = 14.0            # rear flap that locks the box once the marquee is on

SEG = 48

# --- Derived layout. x: left to right, y: front (0) to back, z: up ---------
CAB_W = BOX_W + 2 * SIDE + 2 * PLAY
BAY_X0 = SIDE
BAY_X1 = CAB_W - SIDE
BOX_X0 = SIDE + PLAY
BOX_X1 = BOX_X0 + BOX_W
BOX_Y0 = PANEL_D + BEZEL
BOX_Y1 = BOX_Y0 + BOX_D
CAB_D = BOX_Y1 + BACK_PLAY
SHELF_Z = LOWER_H
BOX_Z0 = SHELF_Z + SHELF
BOX_Z1 = BOX_Z0 + BOX_H
BAY_H = BOX_H + PLAY
TOP_Z = BOX_Z0 + BAY_H
Z_PANEL_FRONT = LOWER_H - PANEL_DROP
Z_PANEL_BACK = LOWER_H + SHELF
VIEW_X0 = BOX_X0 + BOX_VIEW_X0 + 1.0
VIEW_X1 = BOX_X0 + BOX_VIEW_X1 - 1.0
VIEW_Z0 = BOX_Z0 + BOX_VIEW_Z0 + 1.0
VIEW_Z1 = BOX_Z0 + BOX_VIEW_Z1 - 2.0

assert Z_PANEL_FRONT > FLOOR + 4.0, "control deck would cut through the floor"
assert VIEW_Z1 - VIEW_Z0 > 30.0, "monitor window would hide the hologram"


def panel_z(y):
    return Z_PANEL_FRONT + (Z_PANEL_BACK - Z_PANEL_FRONT) * (y / PANEL_D)


# ---------------------------------------------------------------------------
# Scene helpers

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


def cone(name, coll, x, y, z0, h, d0, d1, mat=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=SEG, radius1=d0 / 2, radius2=d1 / 2, depth=h)
    return mesh_object(name, bm, coll, (x, y, z0 + h / 2), mat)


def hex_prism(name, coll, bottom, top, mat=None):
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in list(bottom) + list(top)]
    bm.faces.new(vs[0:4])
    bm.faces.new(list(reversed(vs[4:8])))
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([vs[i], vs[j], vs[4 + j], vs[4 + i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_object(name, bm, coll, (0.0, 0.0, 0.0), mat)


def boolean(target, cutter, op="DIFFERENCE"):
    mod = target.modifiers.new(f"{op.title()} {cutter.name}", "BOOLEAN")
    mod.operation = op
    mod.object = cutter
    mod.solver = "EXACT"
    return mod


def cutter_style(obj):
    obj.display_type = "WIRE"
    obj.hide_render = True


# ---------------------------------------------------------------------------
# Parts

def build(print_coll, cutters, ref):
    dark = material("Black PLA", (0.07, 0.07, 0.08, 1))
    grey = material("Marquee PLA", (0.16, 0.16, 0.18, 1))
    red = material("Buttons", (0.72, 0.12, 0.14, 1))
    box_mat = material("Hologram box", (0.12, 0.43, 0.85, 1))
    sheet_mat = material("Polycarbonate", (0.6, 0.85, 1.0, 0.35))
    sheet_mat.surface_render_method = "BLENDED"

    # Solid tower, then cut the deck, the box bay, and the lower guts.
    cab = box("Cabinet", print_coll, 0, 0, 0, CAB_W, CAB_D, TOP_Z, dark)

    above = hex_prism(
        "Above panel",
        cutters,
        ((-4, -30, panel_z(-30)), (CAB_W + 4, -30, panel_z(-30)),
         (CAB_W + 4, PANEL_D + 0.2, panel_z(PANEL_D + 0.2)), (-4, PANEL_D + 0.2, panel_z(PANEL_D + 0.2))),
        ((-4, -30, panel_z(-30) + 400), (CAB_W + 4, -30, panel_z(-30) + 400),
         (CAB_W + 4, PANEL_D + 0.2, panel_z(PANEL_D + 0.2) + 400), (-4, PANEL_D + 0.2, panel_z(PANEL_D + 0.2) + 400)),
    )
    bay = box("Box bay", cutters, BAY_X0, BOX_Y0, BOX_Z0, BAY_X1, CAB_D + 4, BOX_Z0 + BAY_H + 4)
    view = box("Monitor window", cutters, VIEW_X0, PANEL_D - 1, VIEW_Z0, VIEW_X1, BOX_Y0 + 1, VIEW_Z1)

    # Hollow the pedestal, leaving walls, the shelf, and a few ribs so the
    # shelf can print as short bridges instead of one wide span.
    hollows = [
        box("Lower hollow front", cutters, WALL, WALL, FLOOR, CAB_W - WALL, PANEL_D - WALL,
            Z_PANEL_FRONT - 3.0),
        hex_prism(
            "Panel underside",
            cutters,
            ((WALL, WALL, Z_PANEL_FRONT - 3.2), (CAB_W - WALL, WALL, Z_PANEL_FRONT - 3.2),
             (CAB_W - WALL, PANEL_D - WALL, Z_PANEL_FRONT - 3.2), (WALL, PANEL_D - WALL, Z_PANEL_FRONT - 3.2)),
            ((WALL, WALL, panel_z(WALL) - 2.6), (CAB_W - WALL, WALL, panel_z(WALL) - 2.6),
             (CAB_W - WALL, PANEL_D - WALL, panel_z(PANEL_D - WALL) - 2.6),
             (WALL, PANEL_D - WALL, panel_z(PANEL_D - WALL) - 2.6)),
        ),
        box("Lower hollow mid", cutters, WALL, PANEL_D + WALL, FLOOR, CAB_W - WALL,
            PANEL_D + (CAB_D - PANEL_D) / 2 - RIB / 2, SHELF_Z - 0.05),
        box("Lower hollow back", cutters, WALL, PANEL_D + (CAB_D - PANEL_D) / 2 + RIB / 2, FLOOR,
            CAB_W - WALL, CAB_D - WALL, SHELF_Z - 0.05),
    ]
    floor_hole = box("Floor opening", cutters, WALL + 6, WALL + 6, -1, CAB_W - WALL - 6, CAB_D - WALL - 6, FLOOR + 1)

    sd_y = BOX_Y0 + BOX_SD_Y
    sd_l = box("SD access L", cutters, -1, sd_y - BOX_SD_W / 2, BOX_Z0 + BOX_SD_Z0,
               SIDE + 1, sd_y + BOX_SD_W / 2, BOX_Z0 + BOX_SD_Z1)
    sd_r = box("SD access R", cutters, CAB_W - SIDE - 1, sd_y - BOX_SD_W / 2, BOX_Z0 + BOX_SD_Z0,
               CAB_W + 1, sd_y + BOX_SD_W / 2, BOX_Z0 + BOX_SD_Z1)

    for c in [above, bay, view, *hollows, floor_hole, sd_l, sd_r]:
        boolean(cab, c)

    jx, jy = CAB_W * 0.30, PANEL_D * 0.58
    stick = cone("Joystick", cutters, jx, jy, panel_z(jy) - 0.4, 7.0, 9.5, 4.4, red)
    boolean(cab, stick, "UNION")
    for i, (bx, by) in enumerate(((0.62, 0.48), (0.74, 0.58), (0.68, 0.72))):
        btn = cone(f"Button {i + 1}", cutters, CAB_W * bx, PANEL_D * by, panel_z(PANEL_D * by) - 0.3,
                   1.8, 8.4, 7.2, red)
        boolean(cab, btn, "UNION")

    # Marquee: hangs over the monitor, seats on the side walls, locks the box.
    mx0, mx1 = 0.0, CAB_W
    my0 = PANEL_D - MARQUEE_OVER
    my1 = my0 + MARQUEE_DEPTH
    marquee = hex_prism(
        "Marquee",
        print_coll,
        ((mx0, my0, TOP_Z), (mx1, my0, TOP_Z), (mx1, CAB_D, TOP_Z), (mx0, CAB_D, TOP_Z)),
        ((mx0, my0 + MARQUEE_SLANT, TOP_Z + MARQUEE_H), (mx1, my0 + MARQUEE_SLANT, TOP_Z + MARQUEE_H),
         (mx1, CAB_D, TOP_Z + MARQUEE_H), (mx0, CAB_D, TOP_Z + MARQUEE_H)),
        grey,
    )
    void = hex_prism(
        "Marquee hollow",
        cutters,
        ((mx0 + MARQUEE_WALL, my0 + MARQUEE_WALL + 2.0, TOP_Z + 0.9),
         (mx1 - MARQUEE_WALL, my0 + MARQUEE_WALL + 2.0, TOP_Z + 0.9),
         (mx1 - MARQUEE_WALL, CAB_D - MARQUEE_WALL, TOP_Z + 0.9),
         (mx0 + MARQUEE_WALL, CAB_D - MARQUEE_WALL, TOP_Z + 0.9)),
        ((mx0 + MARQUEE_WALL, my0 + MARQUEE_SLANT + MARQUEE_WALL, TOP_Z + MARQUEE_H - MARQUEE_WALL),
         (mx1 - MARQUEE_WALL, my0 + MARQUEE_SLANT + MARQUEE_WALL, TOP_Z + MARQUEE_H - MARQUEE_WALL),
         (mx1 - MARQUEE_WALL, CAB_D - MARQUEE_WALL, TOP_Z + MARQUEE_H - MARQUEE_WALL),
         (mx0 + MARQUEE_WALL, CAB_D - MARQUEE_WALL, TOP_Z + MARQUEE_H - MARQUEE_WALL)),
    )
    art = hex_prism(
        "Marquee art",
        cutters,
        ((mx0 + 6, my0 + 1.5, TOP_Z + 3.0), (mx1 - 6, my0 + 1.5, TOP_Z + 3.0),
         (mx1 - 6, my0 + 3.2, TOP_Z + MARQUEE_H - 3.0), (mx0 + 6, my0 + 3.2, TOP_Z + MARQUEE_H - 3.0)),
        ((mx0 + 6, my0 + 2.4, TOP_Z + 3.0), (mx1 - 6, my0 + 2.4, TOP_Z + 3.0),
         (mx1 - 6, my0 + 4.1, TOP_Z + MARQUEE_H - 3.0), (mx0 + 6, my0 + 4.1, TOP_Z + MARQUEE_H - 3.0)),
    )
    boolean(marquee, void)
    boolean(marquee, art)
    flap = box("Box lock flap", cutters, BOX_X0 + 3.0, BOX_Y1 + 0.35, BOX_Z1 - FLAP_H,
               BOX_X1 - 3.0, BOX_Y1 + BACK_PLAY - 0.3, TOP_Z + 0.4)
    boolean(marquee, flap, "UNION")

    for obj in cutters.objects:
        cutter_style(obj)

    # Reference: the existing hologram box sitting in the bay (not printed).
    case = box("Box (reference)", ref, BOX_X0, BOX_Y0, BOX_Z0, BOX_X1, BOX_Y1, BOX_Z1, box_mat)
    window = box("Box window (reference)", ref, BOX_X0 + BOX_VIEW_X0, BOX_Y0 - 0.2,
                 BOX_Z0 + BOX_VIEW_Z0, BOX_X0 + BOX_VIEW_X1, BOX_Y0 + 2.0, BOX_Z0 + BOX_VIEW_Z1, sheet_mat)
    return cab, marquee, case, window


# ---------------------------------------------------------------------------
# Export and checks

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
        f.write(b"hologram arcade".ljust(80, b" "))
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


def check_fit(objs):
    tmp = bpy.data.collections.new("fit check")
    bpy.context.scene.collection.children.link(tmp)
    try:
        for i in range(len(objs)):
            for j in range(i + 1, len(objs)):
                a = objs[i].copy()
                a.data = objs[i].data.copy()
                tmp.objects.link(a)
                boolean(a, objs[j], "INTERSECT")
                overlap = volume(a)
                bpy.data.objects.remove(a)
                if overlap > 1.0:
                    raise SystemExit(f"{objs[i].name} and {objs[j].name} overlap by {overlap:.1f} mm^3")
    finally:
        bpy.data.collections.remove(tmp)
    print("fit check: no parts overlap")


def render_preview(scene, parts, path):
    cab, marquee, case, window = parts
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    shading.show_object_outline = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1600
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("World")
    scene.world = world
    world.color = (0.92, 0.92, 0.94)
    scene.display.shading.background_type = "WORLD"

    marquee.location.z += 32
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Camera", cam_data)
    scene.collection.objects.link(cam)
    target = Vector((CAB_W / 2, CAB_D * 0.35, (TOP_Z + MARQUEE_H) / 2))
    cam.location = target + Vector((-200, -320, 80))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    marquee.location.z -= 32
    print(f"wrote {path.name}")


def main():
    scene = reset_scene()
    print_coll = collection("Print")
    cutters = collection("Cutters", hidden=True)
    ref = collection("Reference")
    parts = build(print_coll, cutters, ref)
    cab, marquee, case, window = parts
    print(f"cabinet {CAB_W:.1f} x {CAB_D:.1f} x {TOP_Z + MARQUEE_H:.1f} mm (w x d x h)")
    print(f"box bay {BOX_W + 2 * PLAY:.1f} x {BOX_D + PLAY:.1f} x {BAY_H:.1f} mm")
    print(f"box {BOX_W:.1f} x {BOX_D:.1f} x {BOX_H:.1f} mm slides in from the back")
    print(f"viewing opening {VIEW_X1 - VIEW_X0:.1f} x {VIEW_Z1 - VIEW_Z0:.1f} mm")

    check_fit([cab, case, marquee])

    write_stl(cab, OUT / "arcade_cabinet.stl")
    write_stl(marquee, OUT / "arcade_marquee.stl", Matrix.Rotation(math.pi, 4, "X"))
    print(f"{cab.name}: {volume(cab) / 1000:.1f} cm^3 of plastic")
    print(f"{marquee.name}: {volume(marquee) / 1000:.1f} cm^3 of plastic")

    render_preview(scene, parts, OUT / "arcade_preview.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hologram_arcade.blend"))
    print("wrote hologram_arcade.blend")


if __name__ == "__main__":
    main()
