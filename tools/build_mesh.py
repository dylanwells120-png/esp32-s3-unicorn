#!/usr/bin/env python3
"""Build the animal meshes, write the SD card model file, and save turntable previews."""

import math
import random
import struct
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
# Copied to the SD card by tools/sd_upload.py.
MODELS = ROOT / "sd" / "unicorn" / "animals.bin"
MODEL_VERSION = 1
NAME_BYTES = 16
PREVIEW_DIR = Path("/tmp/unicorn_preview")

SCREEN_W = 800
SCREEN_H = 480
# Camera offset from the look-at point. Each animal gets its own target and focal.
CAM_OFFSET = np.array([0.0, 0.26, 4.30], dtype=np.float32)
KEY_LIGHT = np.array([-0.35, 1.0, 0.45], dtype=np.float32)
FILL_LIGHT = np.array([0.85, 0.25, 0.35], dtype=np.float32)
# Must match the near clip and depth scale in the sketch.
NEAR = 0.25
DEPTH_SCALE = 16000.0

FLAG_DOUBLE = 1
FLAG_SPECULAR = 2

# Per-face brightness wobble so large surfaces read as faceted rather than flat.
JITTER = 0.035


def vnorm(v):
    v = np.asarray(v, dtype=np.float32)
    n = np.linalg.norm(v)
    if n < 1e-8:
        return np.zeros(3, dtype=np.float32)
    return (v / n).astype(np.float32)


def v3(x, y, z):
    return np.array([x, y, z], dtype=np.float32)


KEY_LIGHT = vnorm(KEY_LIGHT)
FILL_LIGHT = vnorm(FILL_LIGHT)


def rot_matrix(pitch=0.0, yaw=0.0, roll=0.0):
    """Rotation about x (pitch), then z (roll), then y (yaw), in degrees."""
    p, y, r = (math.radians(a) for a in (pitch, yaw, roll))
    rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]], dtype=np.float32)
    rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]], dtype=np.float32)
    ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]], dtype=np.float32)
    return ry @ rz @ rx


class Mesh:
    def __init__(self, seed=7):
        self.verts = []
        self.tris = []  # a, b, c, r, g, b, flags
        self.rng = random.Random(seed)

    def add_vert(self, p):
        self.verts.append(np.asarray(p, dtype=np.float32).copy())
        return len(self.verts) - 1

    def add_tri(self, ia, ib, ic, color, inside=None, flags=0, paint=None):
        """`inside` is a point behind the face; the winding is flipped so the normal points away from it."""
        a, b, c = self.verts[ia], self.verts[ib], self.verts[ic]
        n = np.cross(b - a, c - a)
        if np.linalg.norm(n) < 1e-9:
            return
        mid = (a + b + c) / 3.0
        if inside is not None and not (flags & FLAG_DOUBLE):
            if np.dot(n, mid - np.asarray(inside, dtype=np.float32)) < 0:
                ib, ic = ic, ib
                n = -n
        if paint is not None:
            color = paint(mid, vnorm(n), color)
        k = 1.0 + self.rng.uniform(-JITTER, JITTER)
        r, g, bl = (max(0, min(255, int(round(ch * k)))) for ch in color)
        self.tris.append((ia, ib, ic, r, g, bl, int(flags)))

    def add_quad(self, ia, ib, ic, id_, color, inside, flags=0, paint=None):
        if paint is not None:
            a, b, c, d = (self.verts[i] for i in (ia, ib, ic, id_))
            mid = (a + b + c + d) / 4.0
            n = vnorm(np.cross(c - a, d - b))
            if inside is not None and np.dot(n, mid - np.asarray(inside, dtype=np.float32)) < 0:
                n = -n
            color = paint(mid, n, color)
        self.add_tri(ia, ib, ic, color, inside, flags)
        self.add_tri(ia, ic, id_, color, inside, flags)


def loft(mesh, points, radii, colors, sides=8, cap=(True, True), bulge=(0.0, 0.0), twist=0.0,
         flags=0, paint=None, up=None):
    """Tube through `points` with elliptical rings (rx, ry) framed by parallel transport."""
    points = [np.asarray(p, dtype=np.float32) for p in points]
    if not isinstance(colors, list):
        colors = [colors]
    if isinstance(cap, bool):
        cap = (cap, cap)
    tangents = []
    for i in range(len(points)):
        if i == 0:
            t = points[1] - points[0]
        elif i == len(points) - 1:
            t = points[-1] - points[-2]
        else:
            t = vnorm(points[i + 1] - points[i]) + vnorm(points[i] - points[i - 1])
        tangents.append(vnorm(t))

    hint = v3(0, 1, 0) if up is None else vnorm(up)
    if abs(float(np.dot(hint, tangents[0]))) > 0.92:
        hint = v3(0, 0, 1)
    side = vnorm(np.cross(hint, tangents[0]))

    ids = []
    for i, (p, t) in enumerate(zip(points, tangents)):
        side = vnorm(side - t * float(np.dot(side, t)))
        upv = vnorm(np.cross(t, side))
        rx, ry = radii[i]
        ring = []
        for s in range(sides):
            a = twist * i + (s / sides) * math.tau
            ring.append(mesh.add_vert(p + side * (math.cos(a) * rx) + upv * (math.sin(a) * ry)))
        ids.append(ring)

    for i in range(len(ids) - 1):
        color = colors[min(i, len(colors) - 1)]
        inside = (points[i] + points[i + 1]) * 0.5
        for s in range(sides):
            s2 = (s + 1) % sides
            mesh.add_quad(ids[i][s], ids[i][s2], ids[i + 1][s2], ids[i + 1][s], color, inside, flags, paint)

    def cap_fan(ring, center, tangent, amount, radius, color):
        hub = mesh.add_vert(center + tangent * (amount * radius))
        inside = center - tangent * max(radius, 1e-3)
        for s in range(sides):
            mesh.add_tri(hub, ring[s], ring[(s + 1) % sides], color, inside, flags, paint)

    if cap[0]:
        cap_fan(ids[0], points[0], -tangents[0], bulge[0], max(radii[0]), colors[0])
    if cap[1]:
        cap_fan(ids[-1], points[-1], tangents[-1], bulge[1], max(radii[-1]), colors[-1])
    return ids


def ellipsoid(mesh, center, radii, color, lat=6, lon=10, rot=None, flags=0, paint=None):
    center = np.asarray(center, dtype=np.float32)
    rot = np.eye(3, dtype=np.float32) if rot is None else rot
    rx, ry, rz = radii

    def point(phi, th):
        local = v3(math.sin(phi) * math.cos(th) * rx, math.cos(phi) * ry, math.sin(phi) * math.sin(th) * rz)
        return center + rot @ local

    top = mesh.add_vert(point(0.0, 0.0))
    bottom = mesh.add_vert(point(math.pi, 0.0))
    rings = []
    for i in range(1, lat):
        phi = (i / lat) * math.pi
        off = 0.5 * (i % 2)
        rings.append([mesh.add_vert(point(phi, ((j + off) / lon) * math.tau)) for j in range(lon)])
    for j in range(lon):
        j2 = (j + 1) % lon
        mesh.add_tri(top, rings[0][j], rings[0][j2], color, center, flags, paint)
        mesh.add_tri(bottom, rings[-1][j], rings[-1][j2], color, center, flags, paint)
    for i in range(len(rings) - 1):
        for j in range(lon):
            j2 = (j + 1) % lon
            mesh.add_quad(rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j], color, center, flags, paint)


def eye(mesh, center, radius, iris, facing, pupil=None):
    """Glossy eye: an iris dome, an optional pupil, and a catch light."""
    facing = vnorm(facing)
    rot = look_rot(facing)
    ellipsoid(mesh, center, (radius, radius * 1.1, radius * 0.6), iris, lat=4, lon=8, rot=rot, flags=FLAG_SPECULAR)
    if pupil is not None:
        ellipsoid(mesh, center + facing * radius * 0.42, (radius * 0.55, radius * 0.62, radius * 0.3), pupil,
                  lat=3, lon=6, rot=rot, flags=FLAG_SPECULAR)
    glint = center + facing * radius * 0.62 + v3(0, radius * 0.38, 0) + vnorm(np.cross(v3(0, 1, 0), facing)) * radius * 0.3
    ellipsoid(mesh, glint, (radius * 0.24, radius * 0.24, radius * 0.12), (255, 255, 255), lat=2, lon=5, rot=rot)


def look_rot(forward):
    """Rotation whose local +z points along `forward`."""
    f = vnorm(forward)
    upv = v3(0, 1, 0) if abs(float(f[1])) < 0.95 else v3(1, 0, 0)
    x = vnorm(np.cross(upv, f))
    y = np.cross(f, x)
    return np.stack([x, y, f], axis=1).astype(np.float32)


def ground_shadow(mesh, rx, rz, sides=18):
    hub = mesh.add_vert(v3(0, 0.004, 0))
    ring = [mesh.add_vert(v3(math.cos(a) * rx, 0.004, math.sin(a) * rz))
            for a in (i / sides * math.tau for i in range(sides))]
    for i in range(sides):
        mesh.add_tri(hub, ring[i], ring[(i + 1) % sides], (30, 30, 36), v3(0, -1, 0))


def resample(rows, step):
    """Linearly insert rows so consecutive values in column 0 are at most `step` apart."""
    out = [rows[0]]
    for a, b in zip(rows, rows[1:]):
        n = max(1, int(math.ceil(abs(b[0] - a[0]) / step)))
        for i in range(1, n + 1):
            t = i / n
            out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(len(a))))
    return out


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def settle(mesh, shadow=None):
    """Center the mesh over the spin axis, stand it on the floor, and add its shadow."""
    pts = np.array(mesh.verts)
    lo = pts.min(axis=0)
    hi = pts.max(axis=0)
    shift = v3(-(lo[0] + hi[0]) * 0.5, 0.02 - lo[1], -(lo[2] + hi[2]) * 0.5)
    for p in mesh.verts:
        p += shift
    if shadow:
        ground_shadow(mesh, *shadow)
    return mesh


# ---------------------------------------------------------------------------
# Unicorn

COAT = (246, 240, 234)
COAT_SHADE = (226, 218, 214)
MUZZLE = (255, 196, 206)
HOOF = (232, 188, 96)
HORN_A = (255, 212, 92)
HORN_B = (255, 242, 190)
EAR_IN = (255, 168, 192)
RAINBOW = [
    (244, 96, 150),
    (255, 158, 88),
    (255, 214, 96),
    (126, 214, 150),
    (110, 170, 250),
    (176, 120, 236),
]


def unicorn_leg(mesh, pts, radii, hoof_rings=2):
    colors = [COAT] * (len(pts) - 1 - hoof_rings) + [HOOF] * hoof_rings
    loft(mesh, pts, radii, colors, sides=8, bulge=(0.3, 0.0))


def build_unicorn():
    mesh = Mesh(seed=11)

    body = [
        (-1.00, 1.10, 0.10, 0.12),
        (-0.93, 1.08, 0.24, 0.28),
        (-0.78, 1.05, 0.32, 0.36),
        (-0.55, 1.02, 0.35, 0.38),
        (-0.25, 0.99, 0.33, 0.37),
        (0.05, 0.99, 0.33, 0.38),
        (0.35, 1.03, 0.33, 0.39),
        (0.58, 1.08, 0.29, 0.36),
        (0.74, 1.13, 0.20, 0.27),
        (0.81, 1.15, 0.08, 0.12),
    ]
    belly_paint = lambda c, n, col: COAT_SHADE if n[1] < -0.6 else col
    loft(mesh, [v3(0, y, z) for z, y, _, _ in body], [(rx, ry) for _, _, rx, ry in body], COAT,
         sides=14, bulge=(0.4, 0.4), paint=belly_paint)

    neck = [v3(0, 1.20, 0.52), v3(0, 1.48, 0.70), v3(0, 1.74, 0.84), v3(0, 1.94, 0.92)]
    loft(mesh, neck, [(0.21, 0.29), (0.16, 0.22), (0.135, 0.18), (0.125, 0.15)], COAT, sides=10, cap=False)

    head = [v3(0, 2.02, 0.86), v3(0, 1.99, 0.97), v3(0, 1.89, 1.11), v3(0, 1.77, 1.25), v3(0, 1.66, 1.37),
            v3(0, 1.60, 1.44)]
    loft(mesh, head, [(0.10, 0.10), (0.155, 0.165), (0.145, 0.155), (0.115, 0.12), (0.105, 0.10), (0.085, 0.075)],
         [COAT, COAT, COAT, MUZZLE, MUZZLE], sides=10, bulge=(0.5, 0.6))
    for sx in (-1.0, 1.0):
        eye(mesh, v3(sx * 0.13, 1.93, 1.055), 0.05, (54, 36, 78), v3(sx * 0.9, 0.1, 0.45), pupil=(20, 12, 30))
        ellipsoid(mesh, v3(sx * 0.045, 1.635, 1.43), (0.018, 0.012, 0.02), (150, 84, 104), lat=2, lon=5)

    # Ears: pink on the forward-facing side.
    for sx in (-1.0, 1.0):
        ear_paint = lambda c, n, col: EAR_IN if n[2] > 0.35 else col
        loft(mesh, [v3(sx * 0.075, 2.06, 0.88), v3(sx * 0.10, 2.17, 0.86), v3(sx * 0.12, 2.29, 0.83)],
             [(0.048, 0.03), (0.04, 0.028), (0.006, 0.005)], COAT, sides=6, paint=ear_paint)

    # Spiral horn.
    base = v3(0, 2.07, 1.00)
    tip = v3(0, 2.56, 1.20)
    segs = 9
    horn_pts = [base + (tip - base) * (i / segs) for i in range(segs + 1)]
    horn_r = [(0.058 * (1 - i / segs) + 0.006,) * 2 for i in range(segs + 1)]
    loft(mesh, horn_pts, horn_r, [HORN_A if i % 2 == 0 else HORN_B for i in range(segs)], sides=6,
         twist=0.6, flags=FLAG_SPECULAR, bulge=(0.0, 0.8))

    # Mane: rainbow locks that fall alternately to each side of the crest.
    crest = [v3(0, 2.06, 0.84), v3(0, 1.90, 0.76), v3(0, 1.72, 0.66), v3(0, 1.54, 0.56), v3(0, 1.38, 0.46)]
    locks = 12
    for k in range(locks):
        t = k / (locks - 1) * (len(crest) - 1)
        i0 = min(int(t), len(crest) - 2)
        root = crest[i0] + (crest[i0 + 1] - crest[i0]) * (t - i0)
        sx = 1.0 if k % 2 == 0 else -1.0
        length = 0.36 + 0.07 * math.sin(k * 1.7) - 0.08 * (k / locks)
        pts = [
            root + v3(0, 0.06, -0.02),
            root + v3(sx * 0.08, 0.0, -0.08),
            root + v3(sx * 0.14, -length * 0.45, -0.12),
            root + v3(sx * 0.16, -length * 0.8, -0.10),
            root + v3(sx * 0.15, -length, -0.05),
        ]
        loft(mesh, pts, [(0.06, 0.05), (0.08, 0.06), (0.07, 0.05), (0.045, 0.035), (0.006, 0.006)],
             RAINBOW[k % len(RAINBOW)], sides=5, bulge=(0.4, 0.0))
    # Forelock between the ears.
    loft(mesh, [v3(0, 2.10, 0.90), v3(0.02, 2.08, 1.00), v3(0.05, 2.00, 1.08), v3(0.07, 1.92, 1.10)],
         [(0.05, 0.045), (0.055, 0.045), (0.04, 0.03), (0.006, 0.006)], RAINBOW[0], sides=5, bulge=(0.4, 0.0))

    # Tail: several flowing strands.
    # Tail: strands arch up off the rump, then fall and fan out.
    root = v3(0, 1.30, -0.96)
    for k, dx in enumerate((-0.09, -0.045, 0.0, 0.045, 0.09)):
        sway = 0.05 * math.sin(k * 2.1)
        pts = [
            root + v3(dx * 0.2, 0.0, 0.0),
            root + v3(dx * 0.5, 0.10, -0.14),
            root + v3(dx * 0.9, 0.06, -0.28),
            root + v3(dx * 1.4 + sway, -0.14, -0.40),
            root + v3(dx * 1.9, -0.42, -0.46 + sway * 0.5),
            root + v3(dx * 2.3 - sway, -0.70, -0.42),
            root + v3(dx * 2.5, -0.88, -0.34),
        ]
        loft(mesh, pts, [(0.05, 0.05), (0.065, 0.06), (0.075, 0.07), (0.08, 0.075), (0.065, 0.06), (0.04, 0.035),
                         (0.006, 0.006)],
             RAINBOW[(k * 2 + 1) % len(RAINBOW)], sides=6, bulge=(0.3, 0.0))

    # Legs. The near foreleg is lifted mid-prance.
    for sx in (-1.0, 1.0):
        x = sx * 0.19
        if sx > 0:
            shoulder = v3(x, 1.00, 0.52)
            knee = shoulder + v3(0, -0.23, 0.32)
            fetlock = knee + v3(0, -0.28, -0.10)
            unicorn_leg(mesh, [shoulder, shoulder + v3(0, -0.14, 0.14), knee, fetlock + v3(0, 0.12, 0.05), fetlock,
                               fetlock + v3(0, -0.05, -0.04), fetlock + v3(0, -0.12, -0.08)],
                        [(0.12, 0.13), (0.085, 0.09), (0.064, 0.07), (0.054, 0.056), (0.062, 0.064), (0.062, 0.062),
                         (0.072, 0.072)])
        else:
            unicorn_leg(mesh, [v3(x, 1.00, 0.52), v3(x, 0.70, 0.55), v3(x, 0.44, 0.56), v3(x, 0.28, 0.56),
                               v3(x, 0.14, 0.57), v3(x, 0.08, 0.59), v3(x, 0.00, 0.60)],
                        [(0.12, 0.13), (0.085, 0.095), (0.064, 0.07), (0.054, 0.056), (0.062, 0.064), (0.062, 0.062),
                         (0.072, 0.072)])
        unicorn_leg(mesh, [v3(x, 1.04, -0.62), v3(x * 1.02, 0.74, -0.52), v3(x, 0.44, -0.72), v3(x, 0.28, -0.70),
                           v3(x, 0.14, -0.67), v3(x, 0.08, -0.65), v3(x, 0.00, -0.64)],
                    [(0.15, 0.17), (0.10, 0.13), (0.068, 0.072), (0.054, 0.056), (0.062, 0.064), (0.062, 0.062),
                     (0.072, 0.072)])

    return settle(mesh, shadow=(0.62, 1.15))


# ---------------------------------------------------------------------------
# Fox

FOX = (222, 110, 42)
FOX_DARK = (168, 74, 30)
FOX_CREAM = (244, 234, 218)
FOX_BLACK = (36, 28, 30)


def build_fox():
    mesh = Mesh(seed=23)

    def under(c, n, col):
        if n[1] < -0.55:
            return FOX_CREAM
        return col

    body = [
        (-0.52, 0.60, 0.08, 0.09),
        (-0.46, 0.60, 0.15, 0.17),
        (-0.30, 0.59, 0.18, 0.19),
        (-0.06, 0.57, 0.165, 0.175),
        (0.18, 0.60, 0.17, 0.19),
        (0.34, 0.64, 0.155, 0.175),
        (0.44, 0.68, 0.09, 0.11),
    ]
    loft(mesh, [v3(0, y, z) for z, y, _, _ in body], [(rx, ry) for _, _, rx, ry in body], FOX, sides=12,
         bulge=(0.4, 0.3), paint=under)

    def throat(c, n, col):
        if n[2] > 0.25 and n[1] < 0.35:
            return FOX_CREAM
        return col

    loft(mesh, [v3(0, 0.64, 0.34), v3(0, 0.77, 0.47), v3(0, 0.86, 0.56)], [(0.13, 0.15), (0.105, 0.12), (0.10, 0.11)],
         FOX, sides=10, cap=False, paint=throat)

    def face(c, n, col):
        if c[1] < 0.865 and n[2] > -0.3:
            return FOX_CREAM
        return col

    ellipsoid(mesh, v3(0, 0.91, 0.62), (0.135, 0.115, 0.125), FOX, lat=6, lon=12, paint=face)
    # Cheek ruffs.
    for sx in (-1.0, 1.0):
        ellipsoid(mesh, v3(sx * 0.10, 0.85, 0.60), (0.07, 0.06, 0.08), FOX_CREAM, lat=4, lon=7,
                  rot=rot_matrix(yaw=sx * 20))

    def snout(c, n, col):
        if n[1] < -0.15:
            return FOX_CREAM
        return col

    loft(mesh, [v3(0, 0.89, 0.68), v3(0, 0.865, 0.80), v3(0, 0.845, 0.90), v3(0, 0.835, 0.95)],
         [(0.085, 0.075), (0.055, 0.048), (0.032, 0.028), (0.018, 0.016)], FOX, sides=8, cap=(False, True),
         paint=snout)
    ellipsoid(mesh, v3(0, 0.842, 0.955), (0.026, 0.02, 0.02), FOX_BLACK, lat=3, lon=6, flags=FLAG_SPECULAR)

    for sx in (-1.0, 1.0):
        eye(mesh, v3(sx * 0.07, 0.945, 0.715), 0.026, (190, 120, 30), v3(sx * 0.55, 0.1, 0.85), pupil=FOX_BLACK)

        # Pale inside, dark on the back, black tips.
        def ear_paint(c, n, col):
            if col != FOX:
                return col
            if n[2] > 0.45 and c[1] > 1.03:
                return (226, 184, 150)
            if n[2] < -0.45:
                return (92, 50, 34)
            return col

        loft(mesh, [v3(sx * 0.07, 0.98, 0.60), v3(sx * 0.10, 1.07, 0.585), v3(sx * 0.125, 1.14, 0.57),
                    v3(sx * 0.14, 1.20, 0.56)],
             [(0.056, 0.022), (0.044, 0.02), (0.026, 0.014), (0.004, 0.004)], [FOX, FOX, FOX_BLACK], sides=6,
             paint=ear_paint, up=v3(0, 0, 1))

    # Legs with black socks.
    for sx in (-1.0, 1.0):
        x = sx * 0.10
        loft(mesh, [v3(x, 0.60, 0.28), v3(x, 0.40, 0.30), v3(x, 0.22, 0.31), v3(x, 0.07, 0.31), v3(x, 0.03, 0.34),
                    v3(x, 0.0, 0.37)],
             [(0.07, 0.08), (0.045, 0.05), (0.035, 0.038), (0.032, 0.034), (0.038, 0.036), (0.034, 0.03)],
             [FOX, FOX_DARK, FOX_BLACK, FOX_BLACK, FOX_BLACK], sides=7, bulge=(0.3, 0.3))
        loft(mesh, [v3(x, 0.62, -0.36), v3(x * 1.05, 0.40, -0.28), v3(x, 0.22, -0.42), v3(x, 0.07, -0.40),
                    v3(x, 0.03, -0.37), v3(x, 0.0, -0.34)],
             [(0.10, 0.12), (0.065, 0.075), (0.036, 0.04), (0.032, 0.034), (0.038, 0.036), (0.034, 0.03)],
             [FOX, FOX_DARK, FOX_BLACK, FOX_BLACK, FOX_BLACK], sides=7, bulge=(0.3, 0.3))

    # Bushy tail with a white tip.
    tail = [v3(0, 0.66, -0.48), v3(0, 0.58, -0.64), v3(0.02, 0.46, -0.82), v3(0.05, 0.37, -1.00),
            v3(0.08, 0.33, -1.14), v3(0.10, 0.34, -1.24), v3(0.11, 0.37, -1.30)]
    loft(mesh, tail, [(0.06, 0.06), (0.10, 0.10), (0.15, 0.14), (0.16, 0.15), (0.13, 0.12), (0.08, 0.07), (0.01, 0.01)],
         [FOX, FOX, FOX, FOX, FOX_CREAM, FOX_CREAM], sides=10)

    return settle(mesh, shadow=(0.40, 0.85))


# ---------------------------------------------------------------------------
# Penguin

PEN_BLACK = (36, 40, 52)
PEN_WHITE = (244, 246, 250)
PEN_ORANGE = (240, 146, 44)
PEN_YELLOW = (255, 206, 80)


def build_penguin():
    mesh = Mesh(seed=31)

    def coat(c, n, col):
        horiz = math.hypot(float(n[0]), float(n[2]))
        front = float(n[2]) / horiz if horiz > 1e-3 else 0.0
        y = float(c[1])
        # Belly: a broad bib that narrows toward the chin.
        if y < 1.10 and front > 0.25 + 0.35 * max(0.0, y - 0.7):
            return PEN_WHITE
        # Face mask around the eyes and beak.
        if 1.10 <= y < 1.34 and front > 0.62:
            return PEN_WHITE
        # Warm patch at the top of the chest.
        if 0.92 <= y < 1.10 and 0.1 < front <= 0.55:
            return PEN_YELLOW
        return col

    body = [
        (0.04, 0.012, 0.18, 0.16),
        (0.10, 0.02, 0.27, 0.24),
        (0.30, 0.03, 0.34, 0.31),
        (0.56, 0.03, 0.345, 0.31),
        (0.82, 0.01, 0.29, 0.26),
        (1.00, 0.01, 0.235, 0.215),
        (1.12, 0.03, 0.235, 0.225),
        (1.22, 0.04, 0.235, 0.225),
        (1.32, 0.04, 0.222, 0.212),
        (1.43, 0.03, 0.17, 0.16),
        (1.51, 0.01, 0.07, 0.07),
    ]
    loft(mesh, [v3(0, y, z) for y, z, _, _ in body], [(rx, rz) for _, _, rx, rz in body], PEN_BLACK, sides=18,
         bulge=(0.2, 0.5), paint=coat, up=v3(0, 0, 1))

    # Beak: orange lower half, dark tip on top.
    loft(mesh, [v3(0, 1.20, 0.22), v3(0, 1.19, 0.31), v3(0, 1.175, 0.39)], [(0.055, 0.038), (0.034, 0.024), (0.006, 0.005)],
         PEN_ORANGE, sides=6, bulge=(0.0, 0.5))
    for sx in (-1.0, 1.0):
        eye(mesh, v3(sx * 0.088, 1.275, 0.245), 0.034, (20, 22, 30), v3(sx * 0.4, 0.05, 0.92))

        def flipper(c, n, col, sx=sx):
            return PEN_WHITE if n[0] * sx < -0.5 else col

        loft(mesh, [v3(sx * 0.24, 1.00, 0.00), v3(sx * 0.33, 0.82, 0.02), v3(sx * 0.39, 0.58, 0.06),
                    v3(sx * 0.41, 0.44, 0.09)],
             [(0.04, 0.10), (0.035, 0.11), (0.025, 0.08), (0.006, 0.02)], PEN_BLACK, sides=6, paint=flipper,
             up=v3(0, 0, 1))
        # Webbed feet: three toes.
        for k, spread in enumerate((-0.35, 0.0, 0.35)):
            d = v3(math.sin(spread + sx * 0.25), 0, math.cos(spread + sx * 0.25))
            start = v3(sx * 0.12, 0.045, 0.10)
            loft(mesh, [start, start + d * 0.10 + v3(0, -0.01, 0), start + d * 0.17 + v3(0, -0.02, 0)],
                 [(0.035, 0.025), (0.03, 0.02), (0.012, 0.01)], PEN_ORANGE, sides=5)
    loft(mesh, [v3(0, 0.16, -0.22), v3(0, 0.08, -0.34), v3(0, 0.04, -0.40)], [(0.10, 0.03), (0.07, 0.02), (0.01, 0.005)],
         PEN_BLACK, sides=5, up=v3(0, 1, 0))

    return settle(mesh, shadow=(0.50, 0.46))


# ---------------------------------------------------------------------------
# Turtle

SKIN = (120, 176, 92)
SKIN_LIGHT = (186, 206, 124)
SHELL_A = (74, 128, 64)
SHELL_B = (98, 150, 72)
SHELL_TOP = (124, 162, 76)
RIM = (160, 140, 72)
PLASTRON = (222, 196, 128)


def shell_dome(mesh, center, radii, rows=(90, 68, 44, 20), lon=10, raise_=0.05):
    """Upper half-ellipsoid tiled into slightly raised, alternating scutes."""
    center = np.asarray(center, dtype=np.float32)
    rx, ry, rz = radii

    def point(phi_deg, th):
        phi = math.radians(phi_deg)
        return center + v3(math.sin(phi) * math.cos(th) * rx, math.cos(phi) * ry, math.sin(phi) * math.sin(th) * rz)

    rings = []
    for r, phi in enumerate(rows):
        off = 0.5 * (r % 2)
        rings.append([point(phi, ((j + off) / lon) * math.tau) for j in range(lon)])
    top = point(0, 0)

    def plate(corners, color):
        mid = sum(corners) / len(corners)
        normal = vnorm(mid - center)
        hub = mesh.add_vert(mid + normal * raise_)
        ids = [mesh.add_vert(p) for p in corners]
        for i in range(len(ids)):
            mesh.add_tri(hub, ids[i], ids[(i + 1) % len(ids)], color, center)

    for r in range(len(rings) - 1):
        a, b = rings[r], rings[r + 1]
        for j in range(lon):
            j2 = (j + 1) % lon
            color = SHELL_A if (j + r) % 2 == 0 else SHELL_B
            # Rows are staggered by half a step, so each plate is a quad with a shifted top edge.
            if r % 2 == 0:
                plate([a[j], a[j2], b[j], b[(j - 1) % lon]], color)
            else:
                plate([a[j], a[j2], b[j2], b[j]], color)
    plate(rings[-1], SHELL_TOP)
    return rings[0]


def build_turtle():
    mesh = Mesh(seed=41)
    c = v3(0, 0.30, 0)
    rim = shell_dome(mesh, c, (0.50, 0.34, 0.60))
    # Marginal rim that flares slightly below the dome.
    lon = len(rim)
    lower = [mesh.add_vert(c + (p - c) * 1.07 + v3(0, -0.05, 0)) for p in rim]
    base = [mesh.add_vert(c + (p - c) * 0.96 + v3(0, -0.09, 0)) for p in rim]
    top_ids = [mesh.add_vert(p) for p in rim]
    for j in range(lon):
        j2 = (j + 1) % lon
        mesh.add_quad(top_ids[j], top_ids[j2], lower[j2], lower[j], RIM, c)
        mesh.add_quad(lower[j], lower[j2], base[j2], base[j], mix(RIM, (0, 0, 0), 0.25), c)
    ellipsoid(mesh, v3(0, 0.22, 0), (0.46, 0.07, 0.56), PLASTRON, lat=4, lon=12)

    def skin(c_, n, col):
        return SKIN_LIGHT if n[1] < -0.35 else col

    # Neck and head, peeking out and slightly raised.
    loft(mesh, [v3(0, 0.24, 0.40), v3(0, 0.30, 0.58), v3(0, 0.36, 0.70)], [(0.10, 0.09), (0.085, 0.08), (0.08, 0.075)],
         SKIN, sides=9, cap=False, paint=skin)
    ellipsoid(mesh, v3(0, 0.39, 0.76), (0.115, 0.10, 0.13), SKIN, lat=6, lon=10, paint=skin)
    for sx in (-1.0, 1.0):
        eye(mesh, v3(sx * 0.075, 0.43, 0.84), 0.028, (26, 24, 20), v3(sx * 0.6, 0.15, 0.8))
        ellipsoid(mesh, v3(sx * 0.022, 0.40, 0.886), (0.008, 0.006, 0.006), (40, 60, 30), lat=2, lon=4)

    # Stumpy legs with pale claws.
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            hip = v3(sx * 0.32, 0.24, sz * 0.36)
            foot = v3(sx * 0.44, 0.0, sz * 0.46)
            loft(mesh, [hip, hip + (foot - hip) * 0.55, foot + v3(0, 0.03, 0), foot],
                 [(0.10, 0.09), (0.085, 0.08), (0.095, 0.09), (0.09, 0.085)], SKIN, sides=8, paint=skin)
            fwd = vnorm(v3(sx * 0.3, 0, sz))
            side = vnorm(np.cross(v3(0, 1, 0), fwd))
            for k in (-1, 0, 1):
                ellipsoid(mesh, foot + fwd * 0.085 + side * (k * 0.04) + v3(0, 0.02, 0), (0.016, 0.014, 0.022),
                          (236, 226, 196), lat=2, lon=5, rot=look_rot(fwd))
    loft(mesh, [v3(0, 0.22, -0.56), v3(0, 0.18, -0.66), v3(0.03, 0.13, -0.76)], [(0.05, 0.04), (0.035, 0.03), (0.005, 0.005)],
         SKIN, sides=6)

    return settle(mesh, shadow=(0.62, 0.76))


# ---------------------------------------------------------------------------
# Owl

OWL = (132, 88, 52)
OWL_DARK = (84, 54, 34)
OWL_LIGHT = (176, 128, 80)
OWL_CREAM = (238, 222, 188)
OWL_FACE = (222, 196, 156)
OWL_GOLD = (250, 176, 40)
BARK = (104, 70, 44)
LEAF = (96, 158, 72)


def build_owl():
    mesh = Mesh(seed=53)

    # Branch the owl perches on, with a couple of leaves.
    branch = [v3(-0.62, 0.10, 0.06), v3(-0.30, 0.09, 0.03), v3(0.0, 0.10, 0.02), v3(0.30, 0.12, 0.0),
              v3(0.64, 0.16, -0.03)]
    loft(mesh, branch, [(0.075, 0.07), (0.07, 0.065), (0.068, 0.064), (0.064, 0.06), (0.05, 0.048)],
         [BARK, mix(BARK, (0, 0, 0), 0.15), BARK, mix(BARK, (0, 0, 0), 0.15)], sides=7, bulge=(0.1, 0.1))
    for base, d in ((v3(0.46, 0.18, -0.02), v3(0.6, 0.5, 0.35)), (v3(-0.44, 0.13, 0.05), v3(-0.5, 0.35, -0.5))):
        d = vnorm(d)
        loft(mesh, [base, base + d * 0.10, base + d * 0.20], [(0.05, 0.008), (0.045, 0.008), (0.004, 0.004)], LEAF,
             sides=4, flags=FLAG_DOUBLE, up=v3(0, 1, 0))

    def plumage(c, n, col):
        horiz = math.hypot(float(n[0]), float(n[2]))
        front = float(n[2]) / horiz if horiz > 1e-3 else 0.0
        y = float(c[1])
        # Barred breast: alternate rings between cream and tan.
        if 0.2 < y < 1.00 and front > 0.3:
            return OWL_CREAM if int(round(y / 0.06)) % 2 == 0 else OWL_LIGHT
        return col

    body = [
        (0.16, -0.02, 0.10, 0.09),
        (0.22, -0.01, 0.24, 0.22),
        (0.40, 0.0, 0.32, 0.29),
        (0.62, 0.0, 0.33, 0.30),
        (0.84, 0.0, 0.29, 0.27),
        (1.00, 0.01, 0.26, 0.245),
        (1.06, 0.01, 0.24, 0.225),
    ]
    body = body[:2] + resample(body[1:], 0.06)[1:]
    loft(mesh, [v3(0, y, z) for y, z, _, _ in body], [(rx, rz) for _, _, rx, rz in body], OWL, sides=16, cap=(True, False),
         bulge=(0.3, 0.0), paint=plumage, up=v3(0, 0, 1))

    def head_paint(c, n, col):
        if n[1] > 0.55:
            return OWL_DARK
        return col

    ellipsoid(mesh, v3(0, 1.20, 0.02), (0.31, 0.27, 0.27), OWL, lat=7, lon=14, paint=head_paint)
    for sx in (-1.0, 1.0):
        # Facial disc around each eye.
        ellipsoid(mesh, v3(sx * 0.115, 1.19, 0.20), (0.13, 0.14, 0.06), OWL_FACE, lat=4, lon=10,
                  rot=rot_matrix(yaw=sx * 22))
        eye(mesh, v3(sx * 0.115, 1.205, 0.245), 0.068, OWL_GOLD, v3(sx * 0.35, 0.0, 0.94), pupil=(18, 14, 12))
        # Brow feathers above each eye.
        loft(mesh, [v3(sx * 0.02, 1.30, 0.27), v3(sx * 0.12, 1.33, 0.25), v3(sx * 0.23, 1.32, 0.17)],
             [(0.03, 0.02), (0.035, 0.022), (0.01, 0.008)], OWL_DARK, sides=5)
        # Ear tufts.
        loft(mesh, [v3(sx * 0.15, 1.38, 0.03), v3(sx * 0.21, 1.50, 0.0), v3(sx * 0.26, 1.60, -0.04)],
             [(0.06, 0.03), (0.04, 0.022), (0.006, 0.005)], OWL_DARK, sides=5, up=v3(0, 0, 1))

        # Folded wings with light bars.
        def wing(c, n, col):
            return OWL_LIGHT if int(float(c[1]) * 11) % 3 == 0 else col

        loft(mesh, [v3(sx * 0.25, 1.00, 0.0), v3(sx * 0.32, 0.78, -0.02), v3(sx * 0.31, 0.50, -0.06),
                    v3(sx * 0.22, 0.26, -0.14), v3(sx * 0.12, 0.12, -0.22)],
             [(0.06, 0.16), (0.07, 0.20), (0.065, 0.20), (0.05, 0.14), (0.01, 0.03)], OWL_DARK, sides=6, paint=wing,
             bulge=(0.5, 0.0), up=v3(0, 0, 1))

        # Talons curled over the branch.
        for k in (-1, 0, 1):
            x = sx * 0.10 + k * 0.035
            loft(mesh, [v3(x, 0.22, 0.02), v3(x, 0.19, 0.08), v3(x, 0.15, 0.10), v3(x, 0.10, 0.10)],
                 [(0.022, 0.022), (0.02, 0.02), (0.016, 0.016), (0.004, 0.004)], [OWL_GOLD, OWL_GOLD, (40, 34, 30)],
                 sides=5)
    # Hooked beak.
    loft(mesh, [v3(0, 1.17, 0.27), v3(0, 1.14, 0.32), v3(0, 1.08, 0.33)], [(0.032, 0.028), (0.02, 0.018), (0.004, 0.004)],
         (84, 76, 70), sides=6, flags=FLAG_SPECULAR)
    # Tail feathers.
    loft(mesh, [v3(0, 0.34, -0.22), v3(0, 0.16, -0.32), v3(0, 0.02, -0.36)], [(0.12, 0.03), (0.11, 0.025), (0.06, 0.012)],
         OWL_DARK, sides=6, up=v3(0, 0, -1))

    return settle(mesh, shadow=(0.70, 0.40))


# ---------------------------------------------------------------------------
# Preview renderer. Mirrors the rasterizer in the sketch.

class Camera:
    def __init__(self, target, focal):
        self.target = np.asarray(target, dtype=np.float32)
        self.pos = self.target + CAM_OFFSET
        self.focal = focal
        self.forward = vnorm(self.target - self.pos)
        self.right = vnorm(np.cross(self.forward, v3(0, 1, 0)))
        self.up = vnorm(np.cross(self.right, self.forward))

    def to_view(self, p):
        d = p - self.pos
        return float(np.dot(d, self.right)), float(np.dot(d, self.up)), float(np.dot(d, self.forward))


def rot_y(p, yaw):
    c = math.cos(yaw)
    s = math.sin(yaw)
    x, y, z = float(p[0]), float(p[1]), float(p[2])
    return np.array([c * x + s * z, y, -s * x + c * z], dtype=np.float32)


def shade(color, normal, flags, view_dir):
    n = vnorm(normal)
    if np.dot(n, view_dir) < 0 and (flags & FLAG_DOUBLE):
        n = -n
    lambert = 0.30 + 0.72 * max(float(np.dot(n, KEY_LIGHT)), 0.0)
    lambert += 0.22 * max(float(np.dot(n, FILL_LIGHT)), 0.0)
    if flags & FLAG_SPECULAR:
        half = vnorm(KEY_LIGHT + view_dir)
        lambert += 0.55 * (max(float(np.dot(n, half)), 0.0) ** 16)
    lambert = max(0.0, min(lambert, 1.35))
    return tuple(max(0, min(255, int(ch * lambert))) for ch in color)


def render(mesh, cam, yaw):
    verts = [rot_y(p, yaw) for p in mesh.verts]
    screen = []
    for p in verts:
        x, y, z = cam.to_view(p)
        if z < NEAR:
            screen.append(None)
            continue
        screen.append((SCREEN_W * 0.5 + (x / z) * cam.focal, SCREEN_H * 0.5 - (y / z) * cam.focal, 1.0 / z))

    color = np.zeros((SCREEN_H, SCREEN_W, 3), dtype=np.uint8)
    depth = np.zeros((SCREEN_H, SCREEN_W), dtype=np.float32)
    for ia, ib, ic, r, g, b, flags in mesh.tris:
        s0, s1, s2 = screen[ia], screen[ib], screen[ic]
        if s0 is None or s1 is None or s2 is None:
            continue
        p0, p1, p2 = verts[ia], verts[ib], verts[ic]
        n = np.cross(p1 - p0, p2 - p0)
        view_dir = vnorm(cam.pos - (p0 + p1 + p2) / 3.0)
        if not (flags & FLAG_DOUBLE) and float(np.dot(n, view_dir)) <= 0:
            continue
        raster(color, depth, s0, s1, s2, shade((r, g, b), n, flags, view_dir))
    return color


def raster(color, depth, s0, s1, s2, col):
    """Scanline fill; depth holds 1/z, so larger is nearer."""
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = sorted((s0, s1, s2), key=lambda p: p[1])
    # A pixel is covered when its center (x + 0.5, y + 0.5) lies inside the triangle.
    min_y = max(0, int(math.ceil(y0 - 0.5)))
    max_y = min(SCREEN_H - 1, int(math.ceil(y2 - 0.5)) - 1)
    if max_y < min_y:
        return

    def edge(y, ya, yb, xa, xb, za, zb):
        t = 0.0 if yb == ya else (y - ya) / (yb - ya)
        t = max(0.0, min(1.0, t))
        return xa + (xb - xa) * t, za + (zb - za) * t

    for y in range(min_y, max_y + 1):
        ys = y + 0.5
        if ys < y1:
            xa, za = edge(ys, y0, y1, x0, x1, z0, z1)
        else:
            xa, za = edge(ys, y1, y2, x1, x2, z1, z2)
        xb, zb = edge(ys, y0, y2, x0, x2, z0, z2)
        if xa > xb:
            xa, xb, za, zb = xb, xa, zb, za
        x_start = max(0, int(math.ceil(xa - 0.5)))
        x_end = min(SCREEN_W - 1, int(math.ceil(xb - 0.5)) - 1)
        span = xb - xa
        if x_end < x_start or span < 1e-4:
            continue
        dz = (zb - za) / span
        z0s = za + dz * ((x_start + 0.5) - xa)
        zs = z0s + dz * np.arange(x_end - x_start + 1, dtype=np.float32)
        row_d = depth[y, x_start:x_end + 1]
        win = zs > row_d
        row_d[win] = zs[win]
        color[y, x_start:x_end + 1][win] = col


def frame(mesh):
    """Pick a look-at point and focal length that keep the animal centered and on screen at every yaw."""
    pts = np.array(mesh.verts)
    target = v3(0, (pts[:, 1].min() + pts[:, 1].max()) * 0.5, 0)
    yaws = [(i / 36) * math.tau for i in range(36)]
    rotated = [np.array([rot_y(p, yaw) for p in pts]) for yaw in yaws]
    for _ in range(4):
        cam = Camera(target, 1.0)
        us, vs = [], []
        for rp in rotated:
            d = rp - cam.pos
            z = d @ cam.forward
            us.append(np.abs(d @ cam.right) / z)
            vs.append((d @ cam.up) / z)
        u = max(float(a.max()) for a in us)
        v_top = max(float(a.max()) for a in vs)
        v_bot = min(float(a.min()) for a in vs)
        # Shift the target so the vertical extent is balanced around screen center.
        target = target + v3(0, (v_top + v_bot) * 0.5 * float(np.linalg.norm(CAM_OFFSET)), 0)
    focal = min(SCREEN_W * 0.5 * 0.90 / u, SCREEN_H * 0.5 * 0.84 / ((v_top - v_bot) * 0.5))
    return Camera(target, focal)


def write_models(entries):
    """entries: list of (label, mesh, camera). Layout is read by loadAnimals() in sd_store.h."""
    out = bytearray(b"ANIM")
    out += struct.pack("<HH", MODEL_VERSION, len(entries))
    for label, mesh, cam in entries:
        name = label.encode("ascii")[:NAME_BYTES - 1]
        out += name.ljust(NAME_BYTES, b"\0")
        out += struct.pack("<7f", cam.focal, *(float(v) for v in cam.pos), *(float(v) for v in cam.target))
        out += struct.pack("<II", len(mesh.verts), len(mesh.tris))
        for p in mesh.verts:
            out += struct.pack("<3f", float(p[0]), float(p[1]), float(p[2]))
        for tri in mesh.tris:
            out += struct.pack("<3H4B", *tri)
    MODELS.parent.mkdir(parents=True, exist_ok=True)
    MODELS.write_bytes(bytes(out))
    print(f"wrote {MODELS} animals={len(entries)} bytes={len(out)}")


ANIMALS = (
    ("Unicorn", "Unicorn", build_unicorn),
    ("Fox", "Fox", build_fox),
    ("Penguin", "Penguin", build_penguin),
    ("Turtle", "Turtle", build_turtle),
    ("Owl", "Owl", build_owl),
)
PREVIEW_YAWS = (0, 40, 100, 200)


def main():
    built = []
    sheet = Image.new("RGB", (SCREEN_W // 2 * len(PREVIEW_YAWS), SCREEN_H // 2 * len(ANIMALS)))
    for row, (symbol, label, maker) in enumerate(ANIMALS):
        mesh = maker()
        if len(mesh.verts) > 65535:
            raise SystemExit(f"{label}: too many vertices for uint16 indices")
        cam = frame(mesh)
        print(f"{label} verts={len(mesh.verts)} tris={len(mesh.tris)} focal={cam.focal:.1f}")
        built.append((label, mesh, cam))
        for col, deg in enumerate(PREVIEW_YAWS):
            img = Image.fromarray(render(mesh, cam, math.radians(deg)), "RGB")
            PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
            img.save(PREVIEW_DIR / f"{symbol.lower()}_{deg}.png")
            sheet.paste(img.resize((SCREEN_W // 2, SCREEN_H // 2), Image.LANCZOS),
                        (col * SCREEN_W // 2, row * SCREEN_H // 2))
    sheet.save(PREVIEW_DIR / "sheet.png")
    print(f"wrote {PREVIEW_DIR / 'sheet.png'}")
    write_models(built)


if __name__ == "__main__":
    main()
