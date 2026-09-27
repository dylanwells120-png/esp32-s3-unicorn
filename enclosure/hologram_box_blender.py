"""Pepper's ghost hologram box for the MaTouch ESP32-S3 board, built in Blender.

The screen lies face up in the base. A polycarbonate sheet sits above it at
45 degrees, its bottom edge at the back and its top edge leaning toward you.
The screen's light bounces off the underside of the sheet toward the open
front, so the picture appears standing upright at the back of the box.

Run from the repo root:
    /Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_box_blender.py

Writes, next to this file:
    hologram_box.blend   every part with live Boolean cutouts (the "Cutters" collection)
    base.stl hood.stl lid.stl   print-ready, already in print orientation
    preview.png

All sizes are millimetres (1 Blender unit = 1 mm). Change the numbers below and
run it again, or edit the cutter objects in the .blend by hand.
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
BOARD_W = 3.75 * IN      # screen/board edge that runs left to right (USB-C edge)
BOARD_D = 3.125 * IN     # edge that runs front to back (SD card edge)
BOARD_T = 12.0           # glass top to lowest part underneath. Measure this!
GLASS_W = 4.0 * IN       # polycarbonate sheet, left to right
GLASS_L = 2.75 * IN      # polycarbonate sheet, along the 45 degree slope
GLASS_T = 2.0            # sheet thickness; the grooves are this + GLASS_PLAY

# --- Fit -------------------------------------------------------------------
FIT = 0.5                # gap around the board on each side
GLASS_PLAY = 0.5         # extra groove width so the sheet slides in
GROOVE_DEPTH = 2.2       # how far the sheet sits into each side wall
SCREEN_LIP = 2.5         # hood plate overlap onto the board edge, holds it down
PIN_D = 3.0              # alignment pins between base and hood
PIN_H = 3.0
PIN_HOLE = 0.4           # extra hole diameter for the pins

# --- Walls -----------------------------------------------------------------
WALL = 4.0               # base front/back walls and hood back wall
HOOD_WALL = 4.0          # hood side walls (must be thicker than GROOVE_DEPTH)
FLOOR = 2.0
PLATE = 2.0              # hood bottom plate over the screen
LID = 2.0
LID_LIP = 1.5

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
POCKET_H = BOARD_T + 0.3
HOOD_INNER_W = GLASS_W + 2 * 0.3 - 2 * GROOVE_DEPTH
W = max(POCKET_W + 2 * WALL, HOOD_INNER_W + 2 * HOOD_WALL)
D = POCKET_D + 2 * WALL
BASE_H = FLOOR + POCKET_H
PX0 = (W - POCKET_W) / 2
PY0 = WALL
GLASS_RISE = GLASS_L * math.sqrt(0.5)
SLOT = GLASS_T + GLASS_PLAY
# The sheet's bottom edge sits on the plate near the back of the screen opening
# and its top edge leans toward the viewer. It has to lean this way: a sheet
# leaning away would bounce the screen's light into the back wall.
GLASS_Y1 = PY0 + POCKET_D - SCREEN_LIP - 1.0
# Unit vector through the sheet's thickness, from its lower face to its upper face.
SHEET_NORMAL = (0.0, math.sqrt(0.5), math.sqrt(0.5))
HOOD_H = PLATE + GLASS_RISE + SLOT + LID_LIP + 1.0
HOOD_Z = BASE_H                    # hood sits on the base in the assembly
LID_Z = BASE_H + HOOD_H
PIN_XY = [(PX0 / 2, WALL / 2), (W - PX0 / 2, WALL / 2), (PX0 / 2, D - WALL / 2), (W - PX0 / 2, D - WALL / 2)]
assert HOOD_WALL > GROOVE_DEPTH + 1.0, "hood side walls too thin for the grooves"
assert HOOD_INNER_W > POCKET_W - 2 * SCREEN_LIP, "sheet is too narrow to span the screen opening"
assert GLASS_Y1 - GLASS_RISE - SLOT > 0, "sheet would stick out of the front"
assert GLASS_Y1 + SLOT * SHEET_NORMAL[1] < D - WALL, "sheet would hit the hood back wall"


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


# ---------------------------------------------------------------------------
# Parts

def build(print_coll, cutters, ref):
    dark = material("Black PLA", (0.07, 0.07, 0.08, 1))
    grey = material("Lid PLA", (0.16, 0.16, 0.18, 1))
    board_mat = material("Board", (0.12, 0.43, 0.85, 1))
    sheet_mat = material("Polycarbonate", (0.6, 0.85, 1.0, 0.35))
    sheet_mat.surface_render_method = "BLENDED"

    # Base: tray that holds the board.
    base = box("Base", print_coll, 0, 0, 0, W, D, BASE_H, dark)
    pocket = box("Base pocket", cutters, PX0, PY0, FLOOR, PX0 + POCKET_W, PY0 + POCKET_D, BASE_H + 1)
    cx = W / 2 + USB_SLOT_OFFSET
    usb = box("USB-C and button slot", cutters, cx - USB_SLOT_W / 2, D - WALL - 1, FLOOR + USB_SLOT_BOTTOM,
              cx + USB_SLOT_W / 2, D + 1, BASE_H + 1)
    cy = PY0 + POCKET_D / 2 + SD_SLOT_OFFSET
    sd_left = box("SD slot left", cutters, -1, cy - SD_SLOT_W / 2, FLOOR + SD_SLOT_BOTTOM, PX0 + 1,
                  cy + SD_SLOT_W / 2, BASE_H + 1)
    sd_right = box("SD slot right", cutters, PX0 + POCKET_W - 1, cy - SD_SLOT_W / 2, FLOOR + SD_SLOT_BOTTOM, W + 1,
                   cy + SD_SLOT_W / 2, BASE_H + 1)
    thumb = cylinder("Thumb notch", cutters, W / 2, 0, FLOOR + 3, BASE_H, 22)
    for c in (pocket, usb, sd_left, sd_right, thumb):
        boolean(base, c)
    for i, (x, y) in enumerate(PIN_XY):
        pin = cylinder(f"Pin {i + 1}", cutters, x, y, BASE_H - 0.5, PIN_H + 0.5, PIN_D)
        boolean(base, pin, "UNION")

    # Hood: frames the screen and holds the sheet at 45 degrees.
    hood = box("Hood", print_coll, 0, 0, HOOD_Z, W, D, HOOD_Z + HOOD_H, dark)
    hollow = box("Hood hollow", cutters, HOOD_WALL, -1, HOOD_Z + PLATE, W - HOOD_WALL, D - WALL, HOOD_Z + HOOD_H + 1)
    window = box("Screen window", cutters, PX0 + SCREEN_LIP, PY0 + SCREEN_LIP, HOOD_Z - 1,
                 PX0 + POCKET_W - SCREEN_LIP, PY0 + POCKET_D - SCREEN_LIP, HOOD_Z + PLATE + 1)
    # Groove: a slab tilted 45 degrees up and forward from its bottom-back edge, kept to the side walls.
    length = GLASS_L + 40
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(W + 2, length + 5, SLOT), verts=bm.verts)
    bmesh.ops.translate(bm, vec=(0, 5 - (length + 5) / 2, SLOT / 2), verts=bm.verts)
    groove = mesh_object("Sheet grooves", bm, cutters, (W / 2, GLASS_Y1, HOOD_Z + PLATE))
    groove.rotation_euler = (math.radians(-45), 0, 0)
    walls_zone = box("Groove zone (side walls only)", cutters, HOOD_WALL - GROOVE_DEPTH, -1, HOOD_Z + PLATE,
                     W - HOOD_WALL + GROOVE_DEPTH, D + 1, HOOD_Z + HOOD_H + 1)
    boolean(groove, walls_zone, "INTERSECT")
    for c in (hollow, window, groove):
        boolean(hood, c)
    for i, (x, y) in enumerate(PIN_XY):
        hole = cylinder(f"Pin hole {i + 1}", cutters, x, y, HOOD_Z - 1, PIN_H + 1.5, PIN_D + PIN_HOLE)
        boolean(hood, hole)

    # Lid: modelled in place on top of the hood, lip pointing down.
    g = 0.3
    lid = box("Lid", print_coll, 0, 0, LID_Z, W, D, LID_Z + LID, grey)
    rim = box("Lid rim", cutters, HOOD_WALL + g, WALL + g, LID_Z - LID_LIP, W - HOOD_WALL - g, D - WALL - g, LID_Z + 0.5)
    rim_hole = box("Lid rim hollow", cutters, HOOD_WALL + g + 1.6, WALL + g + 1.6, LID_Z - LID_LIP - 1,
                   W - HOOD_WALL - g - 1.6, D - WALL - g - 1.6, LID_Z + 1)
    boolean(rim, rim_hole)
    boolean(lid, rim, "UNION")

    for obj in cutters.objects:
        cutter_style(obj)

    # Reference parts, not printed.
    board = box("Board (reference)", ref, PX0 + FIT, PY0 + FIT, FLOOR, PX0 + FIT + BOARD_W, PY0 + FIT + BOARD_D,
                FLOOR + BOARD_T, board_mat)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    sheet_w = GLASS_W
    bmesh.ops.scale(bm, vec=(sheet_w, GLASS_L, GLASS_T), verts=bm.verts)
    bmesh.ops.translate(bm, vec=(0, -GLASS_L / 2, GLASS_T / 2), verts=bm.verts)
    # Centre the sheet in its groove.
    play = Vector(SHEET_NORMAL) * (GLASS_PLAY / 2)
    sheet = mesh_object("Polycarbonate sheet (reference)", bm, ref, Vector((W / 2, GLASS_Y1, HOOD_Z + PLATE)) + play,
                        sheet_mat)
    sheet.rotation_euler = (math.radians(-45), 0, 0)
    return base, hood, lid, board, sheet


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
    base, hood, lid, board, sheet = parts
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
    target = Vector((W / 2, D / 2, (BASE_H + HOOD_H) / 2 + 10))
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
    base, hood, lid, board, sheet = parts
    print(f"box {W:.1f} x {D:.1f} x {BASE_H + HOOD_H + LID:.1f} mm (w x d x h)")

    check_fit([base, board, hood, lid, sheet])

    write_stl(base, OUT / "base.stl")
    write_stl(hood, OUT / "hood.stl")
    # The lid prints flat with its lip facing up.
    write_stl(lid, OUT / "lid.stl", Matrix.Rotation(math.pi, 4, "X"))

    render_preview(scene, parts, OUT / "preview.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hologram_box.blend"))
    print("wrote hologram_box.blend")


main()
