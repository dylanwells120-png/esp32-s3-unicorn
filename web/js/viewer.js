// ---------------------------------------------------------------- 3D viewer
// Mirrors the renderer in the sketch: same file, camera, lights and shading.
const KEY = norm([-0.35, 1.0, 0.45]), FILL = norm([0.85, 0.25, 0.35]);
const FLAG_DOUBLE = 1, FLAG_SPECULAR = 2, TURN_SECONDS = 18;
let models = [], current = 0, dragYaw = 0, dragging = null;

function norm(v) { const n = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / n, v[1] / n, v[2] / n]; }
function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

function parseModels(buf) {
  const dv = new DataView(buf);
  const version = dv.getUint16(4, true);
  if (new TextDecoder().decode(new Uint8Array(buf, 0, 4)) !== "ANIM" || version < 1 || version > 2) throw new Error("not a model file");
  const count = dv.getUint16(6, true), out = [];
  let o = 8;
  for (let i = 0; i < count; i++) {
    const name = new TextDecoder().decode(new Uint8Array(buf, o, 16)).replace(/\0.*$/s, ""); o += 16;
    const f = []; for (let k = 0; k < 7; k++, o += 4) f.push(dv.getFloat32(o, true));
    const nv = dv.getUint32(o, true), nt = dv.getUint32(o + 4, true); o += 8;
    // Moving parts (version 2): pivot, axis, amplitude (radians), cycles per second, phase.
    const parts = [];
    if (version >= 2) {
      const count = dv.getUint8(o); o += 1;
      for (let p = 0; p < count; p++, o += 25) {
        parts.push({ pivot: [dv.getFloat32(o, true), dv.getFloat32(o + 4, true), dv.getFloat32(o + 8, true)],
                     axis: dv.getUint8(o + 12), amp: dv.getFloat32(o + 13, true), hz: dv.getFloat32(o + 17, true),
                     phase: dv.getFloat32(o + 21, true) });
      }
    }
    const verts = new Float32Array(nv * 3);
    for (let k = 0; k < nv * 3; k++, o += 4) verts[k] = dv.getFloat32(o, true);
    const tris = [];
    for (let k = 0; k < nt; k++, o += 10) {
      tris.push([dv.getUint16(o, true), dv.getUint16(o + 2, true), dv.getUint16(o + 4, true),
                 dv.getUint8(o + 6), dv.getUint8(o + 7), dv.getUint8(o + 8), dv.getUint8(o + 9)]);
    }
    let vparts = null;
    if (version >= 2) { vparts = new Uint8Array(buf.slice(o, o + nv)); o += nv; }
    const cam = [f[1], f[2], f[3]], target = [f[4], f[5], f[6]];
    const fwd = norm([target[0] - cam[0], target[1] - cam[1], target[2] - cam[2]]);
    const right = norm(cross(fwd, [0, 1, 0])), up = norm(cross(right, fwd));
    out.push({ name, focal: f[0], cam, fwd, right, up, verts, tris, parts, vparts });
  }
  return out;
}

function shade(t, n, view) {
  if ((t[6] & FLAG_DOUBLE) && dot(n, view) < 0) n = [-n[0], -n[1], -n[2]];
  let l = 0.30 + 0.72 * Math.max(dot(n, KEY), 0) + 0.22 * Math.max(dot(n, FILL), 0);
  if (t[6] & FLAG_SPECULAR) l += 0.55 * Math.pow(Math.max(dot(n, norm([KEY[0] + view[0], KEY[1] + view[1], KEY[2] + view[2]])), 0), 16);
  l = Math.min(Math.max(l, 0), 1.35);
  return `rgb(${Math.min(255, t[3] * l) | 0},${Math.min(255, t[4] * l) | 0},${Math.min(255, t[5] * l) | 0})`;
}

function draw(now) {
  requestAnimationFrame(draw);
  const m = models[current];
  if (!m) return;
  const ctx = $("view").getContext("2d");
  ctx.fillStyle = "#000"; ctx.fillRect(0, 0, 800, 480);
  const yaw = (now / 1000) * (2 * Math.PI / TURN_SECONDS) + dragYaw, c = Math.cos(yaw), s = Math.sin(yaw);
  const n = m.verts.length / 3, world = new Float32Array(n * 3), scr = new Float32Array(n * 3);
  const secs = now / 1000;
  const swings = m.parts.map((p) => { const a = p.amp * Math.sin(2 * Math.PI * (p.hz * secs + p.phase)); return [Math.cos(a), Math.sin(a)]; });
  for (let i = 0; i < n; i++) {
    let x = m.verts[i * 3], y = m.verts[i * 3 + 1], z = m.verts[i * 3 + 2];
    const part = m.vparts ? m.vparts[i] : 0;
    if (part) {
      // Same swing as the board: rotate about the part's axis through its pivot.
      const { pivot, axis } = m.parts[part - 1], [pc, ps] = swings[part - 1];
      const dx = x - pivot[0], dy = y - pivot[1], dz = z - pivot[2];
      if (axis === 0) { y = pivot[1] + pc * dy - ps * dz; z = pivot[2] + ps * dy + pc * dz; }
      else if (axis === 1) { x = pivot[0] + pc * dx + ps * dz; z = pivot[2] - ps * dx + pc * dz; }
      else { x = pivot[0] + pc * dx - ps * dy; y = pivot[1] + ps * dx + pc * dy; }
    }
    const wx = c * x + s * z, wz = -s * x + c * z;
    world[i * 3] = wx; world[i * 3 + 1] = y; world[i * 3 + 2] = wz;
    const d = [wx - m.cam[0], y - m.cam[1], wz - m.cam[2]], cz = dot(d, m.fwd);
    scr[i * 3] = 400 + dot(d, m.right) / cz * m.focal;
    scr[i * 3 + 1] = 240 - dot(d, m.up) / cz * m.focal;
    scr[i * 3 + 2] = cz;
  }
  const list = [];
  for (const t of m.tris) {
    const a = t[0] * 3, b = t[1] * 3, cc = t[2] * 3;
    const e1 = [world[b] - world[a], world[b + 1] - world[a + 1], world[b + 2] - world[a + 2]];
    const e2 = [world[cc] - world[a], world[cc + 1] - world[a + 1], world[cc + 2] - world[a + 2]];
    const nrm = norm(cross(e1, e2));
    const view = norm([m.cam[0] - (world[a] + world[b] + world[cc]) / 3, m.cam[1] - (world[a + 1] + world[b + 1] + world[cc + 1]) / 3,
                       m.cam[2] - (world[a + 2] + world[b + 2] + world[cc + 2]) / 3]);
    if (!(t[6] & FLAG_DOUBLE) && dot(nrm, view) <= 0) continue;
    list.push([scr[a + 2] + scr[b + 2] + scr[cc + 2], a, b, cc, shade(t, nrm, view)]);
  }
  // Painter's order, far to near. Close enough to the board's depth buffer for a preview.
  list.sort((p, q) => q[0] - p[0]);
  for (const [, a, b, cc, col] of list) {
    ctx.fillStyle = col; ctx.strokeStyle = col; ctx.lineWidth = 0.6;
    ctx.beginPath(); ctx.moveTo(scr[a], scr[a + 1]); ctx.lineTo(scr[b], scr[b + 1]); ctx.lineTo(scr[cc], scr[cc + 1]); ctx.closePath();
    ctx.fill(); ctx.stroke();
  }
}

function pick(i) {
  current = i;
  document.querySelectorAll(".chip").forEach((el, k) => el.setAttribute("aria-pressed", k === i));
  $("showMsg").textContent = "";
}

async function loadModels() {
  try {
    const [bin, info] = await Promise.all([fetch("/animals.bin").then((r) => { if (!r.ok) throw new Error("no animals.bin on the SD card"); return r.arrayBuffer(); }),
                                           fetch("/files").then((r) => r.json())]);
    models = parseModels(bin);
    $("chips").innerHTML = "";
    models.forEach((m, i) => {
      const b = document.createElement("button");
      b.className = "chip"; b.textContent = m.name; b.onclick = () => pick(i);
      $("chips").appendChild(b);
    });
    pick(Math.min(info.current || 0, models.length - 1));
    $("viewNote").hidden = true;
    $("show").disabled = false;
  } catch (err) {
    $("viewNote").textContent = "Couldn't load models: " + err.message + ". Upload them on the SD card page.";
  }
}

$("show").onclick = async () => {
  const r = await fetch("/animal?i=" + current, { method: "POST" }).catch(() => null);
  $("showMsg").textContent = r && r.ok ? models[current].name + " is on the display." : "The board didn't respond.";
};
$("stage").onpointerdown = (e) => { dragging = { x: e.clientX, yaw: dragYaw }; $("stage").setPointerCapture(e.pointerId); };
$("stage").onpointermove = (e) => { if (dragging) dragYaw = dragging.yaw + (e.clientX - dragging.x) * 0.01; };
$("stage").onpointerup = () => { dragging = null; };

loadModels();
requestAnimationFrame(draw);

