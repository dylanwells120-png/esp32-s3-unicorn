#!/usr/bin/env python3
"""Build the unicorn mesh, write a C header, and save turntable previews."""

import math
import os
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "animals.h"
PREVIEW_DIR = Path("/tmp/unicorn_preview")

SCREEN_W = 800
SCREEN_H = 480
FOCAL = 980.0
CAM = np.array([0.0, 1.18, 4.35], dtype=np.float32)
TARGET = np.array([0.0, 0.92, 0.05], dtype=np.float32)
KEY_LIGHT = np.array([-0.35, 1.0, 0.45], dtype=np.float32)
FILL_LIGHT = np.array([0.85, 0.25, 0.35], dtype=np.float32)

FLAG_DOUBLE = 1
FLAG_SPECULAR = 2


def vnorm(v):
    n = np.linalg.norm(v)
    if n < 1e-8:
        return np.zeros(3, dtype=np.float32)
    return (v / n).astype(np.float32)


KEY_LIGHT = vnorm(KEY_LIGHT)
FILL_LIGHT = vnorm(FILL_LIGHT)


class Mesh:
    def __init__(self):
        self.verts = []
        self.tris = []  # a, b, c, r, g, b, flags

    def add_vert(self, p):
        self.verts.append(np.asarray(p, dtype=np.float32))
        return len(self.verts) - 1

    def _orient(self, ia, ib, ic, outward):
        a, b, c = self.verts[ia], self.verts[ib], self.verts[ic]
        n = np.cross(b - a, c - a)
        mid = (a + b + c) / 3.0
        if np.dot(n, mid - outward) < 0:
            return ia, ic, ib
        return ia, ib, ic

    def add_tri(self, ia, ib, ic, color, outward=None, flags=0):
        if outward is not None and not (flags & FLAG_DOUBLE):
            ia, ib, ic = self._orient(ia, ib, ic, np.asarray(outward, dtype=np.float32))
        r, g, b = color
        self.tris.append((ia, ib, ic, int(r), int(g), int(b), int(flags)))

    def add_quad(self, ia, ib, ic, id_, color, outward, flags=0):
        self.add_tri(ia, ib, ic, color, outward, flags)
        self.add_tri(ia, ic, id_, color, outward, flags)


def ring_points(center, tangent, rx, ry, sides, twist=0.0):
    t = vnorm(tangent)
    up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    if abs(float(np.dot(t, up))) > 0.92:
        up = np.array([0.0, 0.0, 1.0], dtype=np.float32)
    side = vnorm(np.cross(up, t))
    up = vnorm(np.cross(t, side))
    pts = []
    for i in range(sides):
        a = twist + (i / sides) * math.tau
        pts.append(center + side * (math.cos(a) * rx) + up * (math.sin(a) * ry))
    return pts


def add_loft(mesh, points, radii, colors, sides=8, cap=True, twist=0.0, flags=0):
    points = [np.asarray(p, dtype=np.float32) for p in points]
    rings = []
    for i, p in enumerate(points):
        if i == 0:
            tangent = points[1] - points[0]
        elif i == len(points) - 1:
            tangent = points[-1] - points[-2]
        else:
            tangent = points[i + 1] - points[i - 1]
        rx, ry = radii[i]
        rings.append(ring_points(p, tangent, rx, ry, sides, twist * i))

    ids = []
    for ring in rings:
        ids.append([mesh.add_vert(p) for p in ring])

    for i in range(len(ids) - 1):
        color = colors[i] if i < len(colors) else colors[-1]
        outward_hint = points[i]
        for s in range(sides):
            s2 = (s + 1) % sides
            a, b = ids[i][s], ids[i][s2]
            d, c = ids[i + 1][s], ids[i + 1][s2]
            mesh.add_quad(a, b, c, d, color, outward_hint, flags)

    if cap and len(ids) >= 2:
        def cap_fan(ring_ids, center, outward, color):
            hub = mesh.add_vert(center)
            for s in range(sides):
                s2 = (s + 1) % sides
                mesh.add_tri(hub, ring_ids[s], ring_ids[s2], color, outward, flags)

        # The third argument is an interior reference so the cap normal points outward.
        cap_fan(ids[0], points[0], points[1], colors[0])
        cap_fan(ids[-1], points[-1], points[-2], colors[-1])
    return ids


def add_sphere(mesh, center, radius, color, lat=4, lon=6, flags=0):
    center = np.asarray(center, dtype=np.float32)
    bands = []
    for i in range(lat + 1):
        v = i / lat
        phi = v * math.pi
        y = math.cos(phi) * radius
        r = math.sin(phi) * radius
        ring = []
        for j in range(lon):
            th = (j / lon) * math.tau
            ring.append(mesh.add_vert(center + np.array([math.cos(th) * r, y, math.sin(th) * r], dtype=np.float32)))
        bands.append(ring)
    for i in range(lat):
        for j in range(lon):
            j2 = (j + 1) % lon
            mesh.add_quad(bands[i][j], bands[i][j2], bands[i + 1][j2], bands[i + 1][j], color, center, flags)


def add_fin(mesh, base, tip, width, color):
    base = np.asarray(base, dtype=np.float32)
    tip = np.asarray(tip, dtype=np.float32)
    width = np.asarray(width, dtype=np.float32)
    a = mesh.add_vert(base - width)
    b = mesh.add_vert(base + width)
    c = mesh.add_vert(tip)
    mesh.add_tri(a, b, c, color, flags=FLAG_DOUBLE)


COAT = (244, 236, 226)
COAT_SHADE = (228, 216, 206)
HOOF = (62, 50, 48)
HORN_A = (255, 214, 96)
HORN_B = (255, 244, 196)
EAR = (255, 228, 232)
EAR_IN = (255, 160, 186)
NOSE = (255, 176, 190)
EYE_WHITE = (248, 248, 252)
PUPIL = (28, 18, 36)
MANE = [
    (236, 102, 156),
    (186, 112, 232),
    (120, 156, 245),
    (245, 196, 96),
]


def build():
    mesh = Mesh()

    body_z = [-1.02, -0.72, -0.32, 0.08, 0.42, 0.68, 0.82]
    body_y = [0.92, 0.98, 0.94, 0.90, 0.94, 1.02, 1.08]
    body_rx = [0.20, 0.32, 0.36, 0.34, 0.32, 0.26, 0.18]
    body_ry = [0.24, 0.34, 0.40, 0.38, 0.38, 0.32, 0.22]
    body_pts = [np.array([0.0, body_y[i], body_z[i]], dtype=np.float32) for i in range(len(body_z))]
    add_loft(
        mesh,
        body_pts,
        list(zip(body_rx, body_ry)),
        [COAT] * (len(body_pts) - 1),
        sides=12,
        cap=True,
    )

    neck_pts = [
        np.array([0.0, 0.98, 0.48], dtype=np.float32),
        np.array([0.0, 1.22, 0.66], dtype=np.float32),
        np.array([0.0, 1.46, 0.80], dtype=np.float32),
        np.array([0.0, 1.64, 0.88], dtype=np.float32),
    ]
    add_loft(
        mesh,
        neck_pts,
        [(0.22, 0.24), (0.15, 0.17), (0.13, 0.15), (0.12, 0.14)],
        [COAT, COAT, COAT_SHADE],
        sides=8,
        cap=False,
    )
    # Fills the gap where the neck meets the chest.
    add_sphere(mesh, np.array([0.0, 1.02, 0.52], dtype=np.float32), 0.30, COAT, lat=4, lon=8)

    head_pts = [
        np.array([0.0, 1.60, 0.90], dtype=np.float32),
        np.array([0.0, 1.52, 1.10], dtype=np.float32),
        np.array([0.0, 1.38, 1.32], dtype=np.float32),
        np.array([0.0, 1.28, 1.50], dtype=np.float32),
    ]
    add_loft(
        mesh,
        head_pts,
        [(0.17, 0.18), (0.155, 0.15), (0.105, 0.105), (0.055, 0.062)],
        [COAT, COAT, NOSE],
        sides=8,
        cap=True,
    )

    horn_pts = []
    horn_r = []
    horn_colors = []
    segments = 8
    for i in range(segments + 1):
        t = i / segments
        horn_pts.append(np.array([0.0, 1.74 + t * 0.62, 0.98 + t * 0.16], dtype=np.float32))
        horn_r.append((0.055 * (1.0 - t) + 0.008, 0.055 * (1.0 - t) + 0.008))
        if i < segments:
            horn_colors.append(HORN_A if i % 2 == 0 else HORN_B)
    add_loft(mesh, horn_pts, horn_r, horn_colors, sides=5, cap=True, twist=0.85, flags=FLAG_SPECULAR)

    for sign in (-1.0, 1.0):
        ear_pts = [
            np.array([sign * 0.09, 1.64, 0.86], dtype=np.float32),
            np.array([sign * 0.11, 1.84, 0.78], dtype=np.float32),
            np.array([sign * 0.12, 2.00, 0.70], dtype=np.float32),
        ]
        add_loft(
            mesh,
            ear_pts,
            [(0.05, 0.035), (0.035, 0.025), (0.012, 0.01)],
            [EAR, EAR_IN],
            sides=5,
            cap=True,
        )

    # Mane sits on the back of the neck as a thin tube so it reads from every angle.
    mane_pts = [
        np.array([0.0, 1.70, 0.78], dtype=np.float32),
        np.array([0.0, 1.52, 0.66], dtype=np.float32),
        np.array([0.0, 1.32, 0.54], dtype=np.float32),
        np.array([0.0, 1.14, 0.42], dtype=np.float32),
        np.array([0.0, 1.00, 0.34], dtype=np.float32),
    ]
    add_loft(
        mesh,
        mane_pts,
        [(0.045, 0.06), (0.05, 0.07), (0.05, 0.065), (0.04, 0.05), (0.025, 0.03)],
        MANE,
        sides=5,
        cap=True,
    )

    tail_pts = [
        np.array([0.0, 1.02, -1.00], dtype=np.float32),
        np.array([0.0, 0.82, -1.22], dtype=np.float32),
        np.array([0.0, 0.58, -1.40], dtype=np.float32),
        np.array([0.02, 0.36, -1.52], dtype=np.float32),
    ]
    add_loft(
        mesh,
        tail_pts,
        [(0.07, 0.08), (0.055, 0.06), (0.04, 0.045), (0.02, 0.02)],
        [MANE[0], MANE[1], MANE[2]],
        sides=6,
        cap=True,
    )

    def add_leg(hip, knee, ankle, hoof, side):
        pts = [hip, knee, ankle, hoof]
        radii = [(0.075, 0.085), (0.055, 0.06), (0.045, 0.05), (0.055, 0.06)]
        colors = [COAT, COAT, HOOF]
        # Nudge the leg outward.
        pts = [p + np.array([side, 0.0, 0.0], dtype=np.float32) for p in pts]
        add_loft(mesh, pts, radii, colors, sides=6, cap=True)

    s = 0.14
    add_leg(
        np.array([s, 0.72, 0.48], dtype=np.float32),
        np.array([s, 0.38, 0.52], dtype=np.float32),
        np.array([s, 0.12, 0.50], dtype=np.float32),
        np.array([s, 0.03, 0.50], dtype=np.float32),
        0.10,
    )
    add_leg(
        np.array([-s, 0.72, 0.48], dtype=np.float32),
        np.array([-s, 0.38, 0.52], dtype=np.float32),
        np.array([-s, 0.12, 0.50], dtype=np.float32),
        np.array([-s, 0.03, 0.50], dtype=np.float32),
        -0.10,
    )
    add_leg(
        np.array([s, 0.74, -0.48], dtype=np.float32),
        np.array([s, 0.40, -0.62], dtype=np.float32),
        np.array([s, 0.12, -0.52], dtype=np.float32),
        np.array([s, 0.03, -0.50], dtype=np.float32),
        0.10,
    )
    add_leg(
        np.array([-s, 0.74, -0.48], dtype=np.float32),
        np.array([-s, 0.40, -0.62], dtype=np.float32),
        np.array([-s, 0.12, -0.52], dtype=np.float32),
        np.array([-s, 0.03, -0.50], dtype=np.float32),
        -0.10,
    )

    for sign in (-1.0, 1.0):
        eye = np.array([sign * 0.115, 1.50, 1.12], dtype=np.float32)
        pupil = eye + np.array([sign * 0.012, 0.0, 0.035], dtype=np.float32)
        add_sphere(mesh, eye, 0.048, EYE_WHITE, lat=3, lon=6)
        add_sphere(mesh, pupil, 0.026, PUPIL, lat=2, lon=5)
        add_sphere(
            mesh,
            pupil + np.array([sign * 0.006, 0.01, 0.012], dtype=np.float32),
            0.01,
            (255, 255, 255),
            lat=1,
            lon=4,
        )
        nostril = np.array([sign * 0.035, 1.30, 1.50], dtype=np.float32)
        add_sphere(mesh, nostril, 0.018, (120, 70, 84), lat=2, lon=4)

    # Sit the hooves on the ground.
    lowest = min(float(p[1]) for p in mesh.verts)
    for p in mesh.verts:
        p[1] -= lowest - 0.02
    return mesh


def camera_basis():
    forward = vnorm(TARGET - CAM)
    right = vnorm(np.cross(forward, np.array([0.0, 1.0, 0.0], dtype=np.float32)))
    up = vnorm(np.cross(right, forward))
    return right, up, forward


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


def render(mesh, yaw, path):
    right, up, forward = camera_basis()
    verts = [rot_y(p, yaw) for p in mesh.verts]
    screen = []
    for p in verts:
        d = p - CAM
        x = float(np.dot(d, right))
        y = float(np.dot(d, up))
        z = float(np.dot(d, forward))
        if z < 0.08:
            screen.append(None)
            continue
        sx = SCREEN_W * 0.5 + (x / z) * FOCAL
        sy = SCREEN_H * 0.5 - (y / z) * FOCAL
        screen.append((sx, sy, z))

    color = np.zeros((SCREEN_H, SCREEN_W, 3), dtype=np.uint8)
    depth = np.zeros((SCREEN_H, SCREEN_W), dtype=np.float32)

    for ia, ib, ic, r, g, b, flags in mesh.tris:
        s0, s1, s2 = screen[ia], screen[ib], screen[ic]
        if s0 is None or s1 is None or s2 is None:
            continue
        p0, p1, p2 = verts[ia], verts[ib], verts[ic]
        n = np.cross(p1 - p0, p2 - p0)
        mid = (p0 + p1 + p2) / 3.0
        view_dir = vnorm(CAM - mid)
        if not (flags & FLAG_DOUBLE) and float(np.dot(n, view_dir)) <= 0:
            continue
        col = shade((r, g, b), n, flags, view_dir)
        raster(color, depth, s0, s1, s2, col)

    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    Image.fromarray(color, "RGB").save(path)
    print(f"wrote {path}")


def raster(color, depth, s0, s1, s2, col):
    pts = sorted((s0, s1, s2), key=lambda p: p[1])
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts
    min_y = max(0, int(math.ceil(y0)))
    max_y = min(SCREEN_H - 1, int(math.floor(y2)))
    if max_y < min_y or y2 - y0 < 0.5:
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
            xa, xb = xb, xa
            za, zb = zb, za
        x_start = max(0, int(math.ceil(xa)))
        x_end = min(SCREEN_W - 1, int(math.floor(xb)))
        span = xb - xa
        if x_end < x_start or span < 1e-4:
            continue
        dz = (zb - za) / span
        z = za + dz * ((x_start + 0.5) - xa)
        row_c = color[y]
        row_d = depth[y]
        for x in range(x_start, x_end + 1):
            if z > row_d[x]:
                row_d[x] = z
                row_c[x, 0] = col[0]
                row_c[x, 1] = col[1]
                row_c[x, 2] = col[2]
            z += dz


def settle(mesh):
    lowest = min(float(p[1]) for p in mesh.verts)
    for p in mesh.verts:
        p[1] -= lowest - 0.02


def build_fox():
    mesh = Mesh()
    orange = (214, 106, 40)
    dark = (150, 68, 26)
    cream = (236, 226, 210)
    black = (28, 22, 24)
    body_z = [-0.72, -0.32, 0.08, 0.40, 0.58]
    body_y = [0.50, 0.52, 0.50, 0.54, 0.58]
    body_rx = [0.15, 0.20, 0.20, 0.16, 0.11]
    body_ry = [0.15, 0.19, 0.19, 0.16, 0.11]
    pts = [np.array([0.0, body_y[i], body_z[i]], dtype=np.float32) for i in range(len(body_z))]
    add_loft(mesh, pts, list(zip(body_rx, body_ry)), [orange] * (len(pts) - 1), sides=8)
    add_sphere(mesh, np.array([0.0, 0.42, 0.32], dtype=np.float32), 0.13, cream, lat=3, lon=6)
    neck = [
        np.array([0.0, 0.60, 0.46], dtype=np.float32),
        np.array([0.0, 0.74, 0.62], dtype=np.float32),
        np.array([0.0, 0.80, 0.76], dtype=np.float32),
    ]
    add_loft(mesh, neck, [(0.11, 0.12), (0.09, 0.10), (0.12, 0.11)], [orange, orange], sides=6, cap=False)
    head = [
        np.array([0.0, 0.80, 0.74], dtype=np.float32),
        np.array([0.0, 0.76, 0.96], dtype=np.float32),
        np.array([0.0, 0.70, 1.14], dtype=np.float32),
    ]
    add_loft(mesh, head, [(0.14, 0.13), (0.08, 0.07), (0.032, 0.028)], [orange, cream], sides=6)
    add_sphere(mesh, np.array([0.0, 0.68, 1.16], dtype=np.float32), 0.026, black, lat=2, lon=4)
    for sign in (-1.0, 1.0):
        ear = [
            np.array([sign * 0.07, 0.88, 0.74], dtype=np.float32),
            np.array([sign * 0.08, 1.12, 0.68], dtype=np.float32),
        ]
        add_loft(mesh, ear, [(0.05, 0.03), (0.01, 0.008)], [orange], sides=4)
        eye = np.array([sign * 0.075, 0.80, 0.92], dtype=np.float32)
        add_sphere(mesh, eye, 0.03, (245, 245, 248), lat=2, lon=5)
        add_sphere(mesh, eye + np.array([sign * 0.008, 0.0, 0.016], dtype=np.float32), 0.014, black, lat=1, lon=4)
    tail = [
        np.array([0.0, 0.55, -0.70], dtype=np.float32),
        np.array([0.0, 0.78, -0.98], dtype=np.float32),
        np.array([0.0, 1.02, -1.08], dtype=np.float32),
        np.array([0.0, 1.12, -0.92], dtype=np.float32),
    ]
    add_loft(mesh, tail, [(0.08, 0.08), (0.13, 0.13), (0.14, 0.14), (0.06, 0.06)], [orange, orange, cream], sides=7)
    def leg(x, z):
        add_loft(
            mesh,
            [
                np.array([x, 0.40, z], dtype=np.float32),
                np.array([x, 0.18, z], dtype=np.float32),
                np.array([x, 0.04, z], dtype=np.float32),
            ],
            [(0.045, 0.05), (0.034, 0.038), (0.04, 0.044)],
            [dark, black],
            sides=5,
        )
    for sign in (-1.0, 1.0):
        leg(sign * 0.12, 0.32)
        leg(sign * 0.12, -0.40)
    settle(mesh)
    return mesh


def build_penguin():
    mesh = Mesh()
    black = (32, 34, 40)
    white = (242, 244, 247)
    beak = (232, 142, 46)
    body = [
        np.array([0.0, 0.06, 0.02], dtype=np.float32),
        np.array([0.0, 0.45, 0.02], dtype=np.float32),
        np.array([0.0, 0.95, 0.0], dtype=np.float32),
        np.array([0.0, 1.18, -0.02], dtype=np.float32),
    ]
    add_loft(mesh, body, [(0.22, 0.20), (0.30, 0.26), (0.24, 0.22), (0.14, 0.13)], [black, black, black], sides=10)
    belly = [
        np.array([0.0, 0.32, 0.12], dtype=np.float32),
        np.array([0.0, 0.62, 0.18], dtype=np.float32),
        np.array([0.0, 0.92, 0.12], dtype=np.float32),
    ]
    add_loft(mesh, belly, [(0.12, 0.16), (0.14, 0.18), (0.08, 0.10)], [white, white], sides=8)
    add_sphere(mesh, np.array([0.0, 1.36, 0.0], dtype=np.float32), 0.22, black, lat=4, lon=8)
    add_sphere(mesh, np.array([0.0, 1.32, 0.10], dtype=np.float32), 0.12, white, lat=3, lon=6)
    add_loft(
        mesh,
        [
            np.array([0.0, 1.28, 0.16], dtype=np.float32),
            np.array([0.0, 1.24, 0.36], dtype=np.float32),
            np.array([0.0, 1.22, 0.46], dtype=np.float32),
        ],
        [(0.06, 0.045), (0.045, 0.03), (0.015, 0.012)],
        [beak, beak],
        sides=5,
    )
    for sign in (-1.0, 1.0):
        add_loft(
            mesh,
            [
                np.array([sign * 0.22, 0.95, 0.0], dtype=np.float32),
                np.array([sign * 0.42, 0.72, 0.02], dtype=np.float32),
                np.array([sign * 0.48, 0.48, 0.04], dtype=np.float32),
            ],
            [(0.06, 0.10), (0.04, 0.08), (0.025, 0.04)],
            [black, black],
            sides=5,
        )
        add_loft(
            mesh,
            [
                np.array([sign * 0.10, 0.10, 0.06], dtype=np.float32),
                np.array([sign * 0.14, 0.04, 0.20], dtype=np.float32),
            ],
            [(0.08, 0.04), (0.10, 0.035)],
            [beak],
            sides=5,
        )
        eye = np.array([sign * 0.09, 1.40, 0.16], dtype=np.float32)
        add_sphere(mesh, eye, 0.045, white, lat=3, lon=6)
        add_sphere(mesh, eye + np.array([sign * 0.01, 0.0, 0.03], dtype=np.float32), 0.022, black, lat=2, lon=4)
    settle(mesh)
    return mesh


def build_turtle():
    mesh = Mesh()
    skin = (96, 156, 82)
    shell = (62, 112, 58)
    scute = (44, 82, 46)
    belly = (214, 186, 120)
    body = [
        np.array([0.0, 0.22, -0.35], dtype=np.float32),
        np.array([0.0, 0.24, 0.0], dtype=np.float32),
        np.array([0.0, 0.22, 0.38], dtype=np.float32),
    ]
    add_loft(mesh, body, [(0.22, 0.12), (0.28, 0.14), (0.18, 0.11)], [skin, skin], sides=8)
    add_sphere(mesh, np.array([0.0, 0.16, 0.0], dtype=np.float32), 0.26, belly, lat=3, lon=8)
    add_sphere(mesh, np.array([0.0, 0.38, 0.0], dtype=np.float32), 0.42, shell, lat=5, lon=10)
    add_sphere(mesh, np.array([0.0, 0.55, 0.0], dtype=np.float32), 0.16, scute, lat=3, lon=6)
    head = [
        np.array([0.0, 0.28, 0.42], dtype=np.float32),
        np.array([0.0, 0.36, 0.62], dtype=np.float32),
        np.array([0.0, 0.34, 0.78], dtype=np.float32),
    ]
    add_loft(mesh, head, [(0.08, 0.07), (0.10, 0.09), (0.07, 0.06)], [skin, skin], sides=6)
    for sign in (-1.0, 1.0):
        eye = np.array([sign * 0.06, 0.40, 0.72], dtype=np.float32)
        add_sphere(mesh, eye, 0.022, (240, 240, 236), lat=2, lon=4)
        add_sphere(mesh, eye + np.array([0.0, 0.0, 0.012], dtype=np.float32), 0.01, (20, 20, 20), lat=1, lon=4)
        for z, y in ((0.22, 0.16), (-0.22, 0.16)):
            add_loft(
                mesh,
                [
                    np.array([sign * 0.24, y + 0.08, z], dtype=np.float32),
                    np.array([sign * 0.42, 0.06, z], dtype=np.float32),
                ],
                [(0.07, 0.05), (0.08, 0.04)],
                [skin],
                sides=5,
            )
    add_loft(
        mesh,
        [
            np.array([0.0, 0.24, -0.40], dtype=np.float32),
            np.array([0.0, 0.16, -0.58], dtype=np.float32),
        ],
        [(0.04, 0.03), (0.02, 0.015)],
        [skin],
        sides=4,
    )
    settle(mesh)
    return mesh


def build_owl():
    mesh = Mesh()
    brown = (122, 78, 44)
    dark = (68, 42, 28)
    cream = (232, 214, 176)
    gold = (214, 156, 48)
    black = (22, 18, 16)
    body = [
        np.array([0.0, 0.08, 0.02], dtype=np.float32),
        np.array([0.0, 0.55, 0.02], dtype=np.float32),
        np.array([0.0, 0.95, 0.0], dtype=np.float32),
    ]
    add_loft(mesh, body, [(0.22, 0.20), (0.32, 0.28), (0.24, 0.22)], [brown, brown], sides=10)
    add_sphere(mesh, np.array([0.0, 1.18, 0.02], dtype=np.float32), 0.26, brown, lat=4, lon=8)
    add_sphere(mesh, np.array([0.0, 1.12, 0.16], dtype=np.float32), 0.16, cream, lat=3, lon=6)
    for sign in (-1.0, 1.0):
        tuft = [
            np.array([sign * 0.10, 1.32, -0.02], dtype=np.float32),
            np.array([sign * 0.14, 1.58, -0.08], dtype=np.float32),
        ]
        add_loft(mesh, tuft, [(0.045, 0.03), (0.012, 0.01)], [dark], sides=4)
        eye = np.array([sign * 0.10, 1.20, 0.20], dtype=np.float32)
        add_sphere(mesh, eye, 0.075, (248, 244, 230), lat=3, lon=7)
        add_sphere(mesh, eye + np.array([0.0, 0.0, 0.04], dtype=np.float32), 0.038, gold, lat=2, lon=6)
        add_sphere(mesh, eye + np.array([0.0, 0.0, 0.06], dtype=np.float32), 0.018, black, lat=2, lon=4)
        add_loft(
            mesh,
            [
                np.array([sign * 0.18, 0.85, -0.02], dtype=np.float32),
                np.array([sign * 0.48, 0.55, 0.02], dtype=np.float32),
                np.array([sign * 0.42, 0.28, 0.0], dtype=np.float32),
            ],
            [(0.08, 0.16), (0.05, 0.14), (0.03, 0.06)],
            [dark, brown],
            sides=5,
        )
        add_loft(
            mesh,
            [
                np.array([sign * 0.07, 0.12, 0.04], dtype=np.float32),
                np.array([sign * 0.09, 0.04, 0.12], dtype=np.float32),
            ],
            [(0.045, 0.035), (0.06, 0.03)],
            [gold],
            sides=4,
        )
    add_loft(
        mesh,
        [
            np.array([0.0, 1.12, 0.22], dtype=np.float32),
            np.array([0.0, 1.06, 0.34], dtype=np.float32),
        ],
        [(0.035, 0.028), (0.015, 0.012)],
        [gold],
        sides=4,
    )
    add_loft(
        mesh,
        [
            np.array([0.0, 0.45, -0.22], dtype=np.float32),
            np.array([0.0, 0.32, -0.40], dtype=np.float32),
        ],
        [(0.10, 0.06), (0.04, 0.025)],
        [dark],
        sides=5,
    )
    settle(mesh)
    return mesh


def write_header(entries):
    """entries: list of (symbol, label, mesh, focal)."""
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct AnimalVertex {",
        "  float x, y, z;",
        "};",
        "",
        "struct AnimalTriangle {",
        "  uint16_t a, b, c;",
        "  uint8_t red, green, blue;",
        "  uint8_t flags;",
        "};",
        "",
        "struct AnimalMesh {",
        "  const char *name;",
        "  const AnimalVertex *vertices;",
        "  int vertexCount;",
        "  const AnimalTriangle *triangles;",
        "  int triangleCount;",
        "  float focal;",
        "  float camX, camY, camZ;",
        "  float targetX, targetY, targetZ;",
        "};",
        "",
        "static constexpr float kTurnSeconds = 18.0f;",
        "",
    ]
    max_verts = 1
    for symbol, _label, mesh, _focal in entries:
        max_verts = max(max_verts, len(mesh.verts))
        lines.append(f"static const AnimalVertex k{symbol}Vertices[] = {{")
        for p in mesh.verts:
            lines.append(f"  {{ {p[0]:.5f}f, {p[1]:.5f}f, {p[2]:.5f}f }},")
        lines.append("};")
        lines.append(f"static const AnimalTriangle k{symbol}Triangles[] = {{")
        for a, b, c, r, g, bl, flags in mesh.tris:
            lines.append(f"  {{ {a}, {b}, {c}, {r}, {g}, {bl}, {flags} }},")
        lines.append("};")
        lines.append("")
    lines.append(f"static constexpr int kMaxAnimalVertices = {max_verts};")
    lines.append(f"static constexpr int kAnimalCount = {len(entries)};")
    lines.append("static const AnimalMesh kAnimals[] = {")
    for symbol, label, mesh, focal in entries:
        lines.append(
            "  { "
            f"\"{label}\", k{symbol}Vertices, {len(mesh.verts)}, "
            f"k{symbol}Triangles, {len(mesh.tris)}, {focal:.4f}f, "
            f"{float(CAM[0]):.4f}f, {float(CAM[1]):.4f}f, {float(CAM[2]):.4f}f, "
            f"{float(TARGET[0]):.4f}f, {float(TARGET[1]):.4f}f, {float(TARGET[2]):.4f}f "
            "},"
        )
    lines.append("};")
    lines.append("")
    HEADER.write_text("\n".join(lines) + "\n")
    print(f"wrote {HEADER} animals={len(entries)} max_verts={max_verts}")


def fit_focal(mesh):
    """Shrink the focal length until every yaw keeps the mesh on screen."""
    right, up, forward = camera_basis()
    peak = 0.0
    for step in range(24):
        yaw = (step / 24) * math.tau
        for p in mesh.verts:
            pr = rot_y(p, yaw)
            d = pr - CAM
            z = float(np.dot(d, forward))
            if z < 0.2:
                continue
            x = abs(float(np.dot(d, right))) / z * FOCAL
            y = abs(float(np.dot(d, up))) / z * FOCAL
            peak = max(peak, x / (SCREEN_W * 0.5), y / (SCREEN_H * 0.5))
    if peak < 1e-4:
        return FOCAL
    return FOCAL * 0.86 / peak


def main():
    global FOCAL
    built = []
    for symbol, label, maker in (
        ("Unicorn", "Unicorn", build),
        ("Fox", "Fox", build_fox),
        ("Penguin", "Penguin", build_penguin),
        ("Turtle", "Turtle", build_turtle),
        ("Owl", "Owl", build_owl),
    ):
        mesh = maker()
        focal = fit_focal(mesh)
        print(f"{label} verts={len(mesh.verts)} tris={len(mesh.tris)} focal={focal:.1f}")
        built.append((symbol, label, mesh, focal))
        render(mesh, math.radians(40), PREVIEW_DIR / f"{symbol.lower()}_40.png")
    # Previews use the last fitted focal. Render each with its own focal.
    for symbol, _label, mesh, focal in built:
        FOCAL = focal
        render(mesh, math.radians(40), PREVIEW_DIR / f"{symbol.lower()}_40.png")
        render(mesh, math.radians(100), PREVIEW_DIR / f"{symbol.lower()}_100.png")
    write_header(built)


if __name__ == "__main__":
    main()
