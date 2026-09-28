"""Pepper's ghost hologram for the MaTouch ESP32-S3 board, built in Blender.

One body and a lid. The hood is the whole shell: thick walls, a low front
sill, and a 45 degree rail on each side for the Lexan. The sheet's low edge
is toward you and its high edge leans back. The lid drops on top and the
screen sits in it, glass down, so the picture bounces off the sheet and out
the open front.

Walls are thick enough to print, and nothing overhangs past 45 degrees, so
the hood prints upright with no supports. The lid prints with its window on
the bed. A separate lightweight table stands under it.

Run from the repo root:
    /Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_box_blender.py

Writes, next to this file:
    hologram_box.blend
    hood.stl lid.stl
    preview.png

All sizes are millimetres (1 Blender unit = 1 mm).
"""

import math
import struct
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

IN = 25.4
OUT = Path(__file__).resolve().parent

# --- Parts you supply ------------------------------------------------------
BOARD_W = 105.0          # screen/board edge that runs left to right (USB-C edge)
BOARD_D = 70.0           # edge that runs front to back (SD card edge)
BOARD_T = 12.0           # glass top to lowest part underneath. Measure this!
VISIBLE_W = 95.0         # lit picture, left to right
VISIBLE_D = 55.0         # lit picture, front to back, centered on the board
GLASS_W = 101.0          # Lexan sheet, left to right
GLASS_L = 70.0           # Lexan sheet, along the 45 degree slope
GLASS_T = 2.0            # sheet thickness; the grooves are this + GLASS_PLAY

# --- Fit -------------------------------------------------------------------
# Measured parts: screen/board 105 x 70 mm, Lexan 101 x 70 mm.
FIT = 1.0                # gap around the board on each side
GLASS_PLAY = 0.6         # gap between the sheet and the rail it sits on
RAIL_UNDER = 2.5         # how far each rail reaches under the sheet
RAIL_T = 1.6             # thickness of the rail, measured square to the slope
STOP = 2.4               # front lip of the rail, so the sheet cannot slide out
PIN_D = 2.0              # lid locating pins
PIN_H = 2.4
PIN_HOLE = 0.6

# --- Walls -----------------------------------------------------------------
WALL = 3.2               # hood walls, all the way around
FLOOR = 1.6
SILL_H = 8.0             # front wall, ties the sides together
SILL_D = 6.0
LID_PLATE = 2.0          # lid floor the glass sits on
LID_H = LID_PLATE + BOARD_T + 0.8

# --- Cutouts ---------------------------------------------------------------
# USB-C ports and the two buttons are on the long edge, which faces the back.
# From the photo they sit about 26 to 62 mm along that edge. The slot runs
# 62 mm, centred, so both ports, their plugs and both buttons clear it
# whichever way round the board goes in.
USB_SLOT_W = 62.0
USB_SLOT_OFFSET = 0.0    # slot centre relative to the middle of the edge
USB_SLOT_BOTTOM = 1.5    # slot starts this far above the pocket floor
# The SD card slot is on a short edge, about 13 mm off centre. Both side walls
# get a slot wide enough for either position, so orientation doesn't matter.
SD_SLOT_W = 38.0
SD_SLOT_OFFSET = 0.0
SD_SLOT_BOTTOM = 1.5

SEG = 48

# --- Derived layout. x: left to right, y: front (0) to back, z: up ---------
POCKET_W = BOARD_W + 2 * FIT
POCKET_D = BOARD_D + 2 * FIT
W = POCKET_W + 2 * WALL
D = POCKET_D + 2 * WALL
BASE_H = 0.0                     # the hood is the whole body
HX0 = WALL                       # inside faces of the side walls
HX1 = W - WALL
PX0 = WALL
PY0 = WALL
BOARD_X0 = PX0 + FIT
BOARD_Y0 = PY0 + FIT
WINDOW_X0 = BOARD_X0 + (BOARD_W - VISIBLE_W) / 2
WINDOW_X1 = WINDOW_X0 + VISIBLE_W
WINDOW_Y0 = BOARD_Y0 + (BOARD_D - VISIBLE_D) / 2
WINDOW_Y1 = WINDOW_Y0 + VISIBLE_D
GLASS_RISE = GLASS_L * math.sqrt(0.5)
# Low edge of the sheet, toward the front. Centered on the lit picture.
GLASS_Y0 = (WINDOW_Y0 + WINDOW_Y1) / 2 - GLASS_RISE / 2
SHEET_NORMAL = (0.0, -math.sqrt(0.5), math.sqrt(0.5))
SLOPE_Z0 = FLOOR + STOP
SLOPE_Z1 = SLOPE_Z0 + GLASS_RISE
# Lid sits just above the sheet.
HOOD_H = SLOPE_Z1 + GLASS_T * math.sqrt(0.5) + 3.0
HOOD_Z = 0.0
LID_Z = HOOD_H
PLATE = SILL_H                   # front opening starts above the sill
SHEET_X0 = (W - GLASS_W) / 2
SHEET_X1 = SHEET_X0 + GLASS_W
PIN_XY = [(2.0, 12.0), (2.0, D - 12.0), (W - 2.0, 12.0), (W - 2.0, D - 12.0)]
assert GLASS_Y0 > SILL_D + 2.0, "sheet would run into the front sill"
assert GLASS_Y0 + GLASS_RISE + 2.0 < D - WALL, "sheet would hit the back wall"
assert WINDOW_X0 >= BOARD_X0 + 1.0 and WINDOW_Y0 >= BOARD_Y0 + 1.0, "window would not leave a ledge for the board"
assert SHEET_X0 + RAIL_UNDER > HX0 + 2.0, "rail would miss the sheet"


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
    """Axis-aligned box with its origin at its centre."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(x1 - x0, y1 - y0, z1 - z0), verts=bm.verts)
    return mesh_object(name, bm, coll, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), mat)


def cylinder(name, coll, x, y, z0, h, d, mat=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=SEG, radius1=d / 2, radius2=d / 2, depth=h)
    return mesh_object(name, bm, coll, (x, y, z0 + h / 2), mat)


def boolean(target, cutter, op="DIFFERENCE"):
    mod = target.modifiers.new(f"{op.title()} {cutter.name}", "BOOLEAN")
    mod.operation = op
    mod.object = cutter
    mod.solver = "EXACT"
    return mod


def cutter_style(obj):
    obj.display_type = "WIRE"
    obj.hide_render = True


def wedge(name, coll, x0, x1, mat=None):
    """Thin 45 degree shelf. The sheet sits on the top face."""
    y0 = GLASS_Y0
    y1 = GLASS_Y0 + GLASS_RISE
    # Shift the underside down the slope so the rail stays RAIL_T thick.
    dy = RAIL_T * math.sqrt(0.5)
    dz = RAIL_T * math.sqrt(0.5)
    verts = [
        (x0, y0 + dy, SLOPE_Z0 - dz), (x1, y0 + dy, SLOPE_Z0 - dz),
        (x1, y1 + dy, SLOPE_Z1 - dz), (x0, y1 + dy, SLOPE_Z1 - dz),
        (x0, y0, SLOPE_Z0), (x1, y0, SLOPE_Z0), (x1, y1, SLOPE_Z1), (x0, y1, SLOPE_Z1),
    ]
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in verts]
    bm.faces.new(vs[0:4])
    bm.faces.new(list(reversed(vs[4:8])))
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([vs[i], vs[j], vs[4 + j], vs[4 + i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_object(name, bm, coll, (0.0, 0.0, 0.0), mat)


# ---------------------------------------------------------------------------
# Parts

def build(print_coll, cutters, ref):
    dark = material("Black PLA", (0.07, 0.07, 0.08, 1))
    grey = material("Lid PLA", (0.16, 0.16, 0.18, 1))
    board_mat = material("Board", (0.12, 0.43, 0.85, 1))
    sheet_mat = material("Polycarbonate", (0.6, 0.85, 1.0, 0.35))
    sheet_mat.surface_render_method = "BLENDED"

    # Hood: one shell. Open front above the sill, open top for the lid.
    hood = box("Hood", print_coll, 0, 0, 0, W, D, HOOD_H, dark)
    hollow = box("Hood hollow", cutters, HX0, SILL_D, FLOOR, HX1, D - WALL, HOOD_H + 1)
    front = box("Front opening", cutters, HX0, -1, SILL_H, HX1, SILL_D + 0.1, HOOD_H + 1)
    boolean(hood, hollow)
    boolean(hood, front)
    left = wedge("Left rail", cutters, WALL - 0.4, SHEET_X0 + RAIL_UNDER)
    right = wedge("Right rail", cutters, SHEET_X1 - RAIL_UNDER, W - WALL + 0.4)
    stop = box("Sheet stop", cutters, WALL - 0.2, GLASS_Y0 - 2.4, FLOOR - 0.2,
               W - WALL + 0.2, GLASS_Y0, SLOPE_Z0)
    boolean(hood, left, "UNION")
    boolean(hood, right, "UNION")
    boolean(hood, stop, "UNION")
    floor_hole = box("Floor opening", cutters, SHEET_X0 + RAIL_UNDER + 2, GLASS_Y0 + 6, -1,
                     SHEET_X1 - RAIL_UNDER - 2, GLASS_Y0 + GLASS_RISE - 6, FLOOR + 0.4)
    boolean(hood, floor_hole)
    for i, (x, y) in enumerate(PIN_XY):
        pin = cylinder(f"Pin {i + 1}", cutters, x, y, HOOD_H - 0.3, PIN_H + 0.3, PIN_D)
        boolean(hood, pin, "UNION")

    # Lid: board drops in, glass down through the window. Prints window-down.
    lid = box("Lid", print_coll, 0, 0, LID_Z, W, D, LID_Z + LID_H, grey)
    pocket = box("Lid pocket", cutters, PX0, PY0, LID_Z + LID_PLATE, PX0 + POCKET_W, PY0 + POCKET_D, LID_Z + LID_H + 1)
    window = box("Screen window", cutters, WINDOW_X0, WINDOW_Y0, LID_Z - 1,
                 WINDOW_X1, WINDOW_Y1, LID_Z + LID_PLATE + 0.4)
    cx = W / 2 + USB_SLOT_OFFSET
    usb = box("USB-C and button slot", cutters, cx - USB_SLOT_W / 2, D - WALL - 1,
              LID_Z + LID_PLATE + USB_SLOT_BOTTOM, cx + USB_SLOT_W / 2, D + 1, LID_Z + LID_H + 1)
    cy = PY0 + POCKET_D / 2 + SD_SLOT_OFFSET
    sd_left = box("SD slot left", cutters, -1, cy - SD_SLOT_W / 2, LID_Z + LID_PLATE + SD_SLOT_BOTTOM, PX0 + 1,
                  cy + SD_SLOT_W / 2, LID_Z + LID_H + 1)
    sd_right = box("SD slot right", cutters, PX0 + POCKET_W - 1, cy - SD_SLOT_W / 2, LID_Z + LID_PLATE + SD_SLOT_BOTTOM,
                   W + 1, cy + SD_SLOT_W / 2, LID_Z + LID_H + 1)
    thumb = cylinder("Thumb notch", cutters, W / 2, PY0, LID_Z + LID_PLATE, LID_H, 16)
    for c in (pocket, window, usb, sd_left, sd_right, thumb):
        boolean(lid, c)
    for i, (x, y) in enumerate(PIN_XY):
        hole = cylinder(f"Pin hole {i + 1}", cutters, x, y, LID_Z - 0.2, LID_PLATE + 0.6, PIN_D + PIN_HOLE)
        boolean(lid, hole)

    for obj in cutters.objects:
        cutter_style(obj)

    # Reference parts, not printed.
    board = box("Board (reference)", ref, PX0 + FIT, PY0 + FIT, LID_Z + LID_PLATE + 0.2,
                PX0 + FIT + BOARD_W, PY0 + FIT + BOARD_D, LID_Z + LID_PLATE + 0.2 + BOARD_T, board_mat)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    sheet_w = GLASS_W
    bmesh.ops.scale(bm, vec=(sheet_w, GLASS_L, GLASS_T), verts=bm.verts)
    bmesh.ops.translate(bm, vec=(0, GLASS_L / 2, GLASS_T / 2), verts=bm.verts)
    # Centre the sheet in its groove.
    play = Vector(SHEET_NORMAL) * GLASS_PLAY
    sheet = mesh_object("Polycarbonate sheet (reference)", bm, ref, Vector((W / 2, GLASS_Y0, SLOPE_Z0)) + play,
                        sheet_mat)
    sheet.rotation_euler = (math.radians(45), 0, 0)
    return hood, lid, board, sheet


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
    # Put the part on the bed at the origin.
    shift = Vector((-lo.x, -lo.y, -lo.z))
    with open(path, "wb") as f:
        f.write(b"hologram box".ljust(80, b" "))
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
    """Fail loudly if any two assembled parts overlap."""
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
    hood, lid, board, sheet = parts
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    shading.show_object_outline = True
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("World")
    scene.world = world
    world.color = (0.92, 0.92, 0.94)
    scene.display.shading.background_type = "WORLD"

    # Lift the lid off so the sheet shows.
    lid.location.z += 30
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Camera", cam_data)
    scene.collection.objects.link(cam)
    target = Vector((W / 2, D / 2, HOOD_H / 2 + 8))
    cam.location = target + Vector((-150, -230, 150))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    lid.location.z -= 30
    print(f"wrote {path.name}")


def main():
    scene = reset_scene()
    print_coll = collection("Print")
    cutters = collection("Cutters", hidden=True)
    ref = collection("Reference")
    parts = build(print_coll, cutters, ref)
    hood, lid, board, sheet = parts
    print(f"box {W:.1f} x {D:.1f} x {HOOD_H + LID_H:.1f} mm (w x d x h)")
    print(f"window {WINDOW_X1 - WINDOW_X0:.1f} x {WINDOW_Y1 - WINDOW_Y0:.1f} mm")
    print(f"glass covers {GLASS_RISE:.1f} mm of the {VISIBLE_D:.1f} mm visible depth")

    check_fit([hood, lid, board, sheet])

    write_stl(hood, OUT / "hood.stl")
    # Window on the bed, pocket facing up.
    write_stl(lid, OUT / "lid.stl")
    for part in (hood, lid):
        print(f"{part.name}: {volume(part) / 1000:.1f} cm^3 of plastic")

    render_preview(scene, parts, OUT / "preview.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hologram_box.blend"))
    print("wrote hologram_box.blend")


if __name__ == "__main__":
    main()
