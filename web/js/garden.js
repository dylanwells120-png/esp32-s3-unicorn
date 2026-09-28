// ---------------------------------------------------------------- Garden
// Fairies and gnomes on a moonlit garden. Same send path as wildlife.
const GARDEN = [
  { id: "fairies", label: "Fairies", on: true },
  { id: "gnomes", label: "Gnomes", on: true },
];
const fairies = [], gnomes = [];
const gardenStars = [], gardenFlowers = [], gardenMushrooms = [], gardenFlies = [];

function spawnFairy() {
  const rx = rand(110, 220), ry = rand(46, 100);
  const cx = rand(rx + 24, 800 - rx - 24);
  const cy = rand(70, 190);
  const dresses = ["#c4476a", "#6a8ec8", "#6eaa78", "#e4c98a", "#a56eb8", "#d47858"];
  const hairs = ["#e8c56a", "#b86a38", "#5c3828", "#f0e6d4", "#e0b0c4", "#7a4e32"];
  const wings = ["#e7f6ea", "#f4e8ff", "#fff6dc", "#e5f4ff"];
  return { cx, cy, rx, ry, speed: rand(0.00045, 0.00105), phase: rand(0, Math.PI * 2),
           eight: Math.random() < 0.45, x: cx, y: cy, px: cx, py: cy, face: 1, bank: 0,
           scale: rand(0.82, 1.08),
           dress: dresses[rand(0, dresses.length) | 0],
           hair: hairs[rand(0, hairs.length) | 0],
           wing: wings[rand(0, wings.length) | 0],
           glow: `hsla(${rand(36, 58) | 0} 100% 72% / 0.42)` };
}
function spawnGnome() {
  const hats = ["#c4383a", "#2f6fbe", "#2f8a4a", "#7a3e8a"];
  const coats = ["#3d6b4f", "#6b4a32", "#3a4a6b", "#5c3a4a"];
  return { x: rand(40, 760), y: rand(400, 455), dir: Math.random() < 0.5 ? 1 : -1,
           speed: rand(0.28, 0.55), phase: rand(0, 6), scale: rand(0.85, 1.2),
           hat: hats[rand(0, hats.length) | 0], coat: coats[rand(0, coats.length) | 0],
           wave: Math.random() < 0.5 };
}
for (let i = 0; i < 8; i++) fairies.push(spawnFairy());
for (let i = 0; i < 4; i++) gnomes.push(spawnGnome());
for (let i = 0; i < 48; i++) gardenStars.push({ x: rand(8, 792), y: rand(8, 200), r: rand(1.2, 2.6), phase: rand(0, 6), speed: rand(0.6, 1.4) });
for (let i = 0; i < 16; i++) gardenFlowers.push({ x: rand(16, 784), y: rand(360, 468), hue: rand(0, 360) | 0, phase: rand(0, 6), h: rand(14, 26) });
for (let i = 0; i < 6; i++) gardenMushrooms.push({ x: rand(40, 760), y: rand(350, 440), scale: rand(0.7, 1.3), cap: Math.random() < 0.5 ? "#d24a4a" : "#efe6c8" });
for (let i = 0; i < 10; i++) gardenFlies.push({ x: rand(20, 780), y: rand(300, 460), phase: rand(0, 6), drift: rand(12, 28) });

GARDEN.forEach((g) => {
  const b = document.createElement("button");
  b.className = "chip"; b.textContent = g.label; b.setAttribute("aria-pressed", "true");
  b.onclick = () => { g.on = !g.on; b.setAttribute("aria-pressed", g.on); };
  $("gardenChips").appendChild(b);
});

// Slender fairy, same shading as the gnomes. Materials: 0 stored, 1 dress, 2 hair, 3 wing.
function buildFairyMesh() {
  const V = [], T = [], parts = [], vpart = [];
  let part = 0;
  const at = (x, y, z) => { V.push(x, y, z); vpart.push(part); return V.length / 3 - 1; };
  const use = (pivot, axis, deg, hz, phase) => {
    parts.push({ pivot, axis, amp: deg * Math.PI / 180, hz, phase });
    part = parts.length;
  };
  const still = () => { part = 0; };
  const xyz = (i) => [V[i * 3], V[i * 3 + 1], V[i * 3 + 2]];
  const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const addTri = (ia, ib, ic, rgb, mat, inside) => {
    const a = xyz(ia), b = xyz(ib), c = xyz(ic);
    let n = cross(sub3(b, a), sub3(c, a));
    if (Math.hypot(n[0], n[1], n[2]) < 1e-8) return;
    const mid = [(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3];
    if (dot(n, sub3(mid, inside)) < 0) [ib, ic] = [ic, ib];
    const j = 1 + (Math.random() - 0.5) * 0.05;
    T.push([ia, ib, ic, Math.min(255, rgb[0] * j) | 0, Math.min(255, rgb[1] * j) | 0, Math.min(255, rgb[2] * j) | 0, 0, mat]);
  };
  const quad = (a, b, c, d, rgb, mat, inside) => { addTri(a, b, c, rgb, mat, inside); addTri(a, c, d, rgb, mat, inside); };
  function ellipsoid(c, r, rgb, mat, lat, lon) {
    const p = (phi, th) => at(c[0] + Math.sin(phi) * Math.cos(th) * r[0], c[1] + Math.cos(phi) * r[1], c[2] + Math.sin(phi) * Math.sin(th) * r[2]);
    const top = p(0, 0), bot = p(Math.PI, 0), rings = [];
    for (let i = 1; i < lat; i++) {
      const phi = i / lat * Math.PI, ring = [];
      for (let j = 0; j < lon; j++) ring.push(p(phi, j / lon * Math.PI * 2));
      rings.push(ring);
    }
    for (let j = 0; j < lon; j++) {
      const j2 = (j + 1) % lon;
      addTri(top, rings[0][j], rings[0][j2], rgb, mat, c);
      addTri(bot, rings[rings.length - 1][j2], rings[rings.length - 1][j], rgb, mat, c);
    }
    for (let i = 0; i < rings.length - 1; i++) for (let j = 0; j < lon; j++) {
      const j2 = (j + 1) % lon;
      quad(rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j], rgb, mat, c);
    }
  }
  function tube(pts, rad, rgb, mat, sides) {
    const rings = pts.map((p, i) => {
      const t = norm(i === 0 ? sub3(pts[1], pts[0]) : i === pts.length - 1 ? sub3(pts[i], pts[i - 1]) : sub3(pts[i + 1], pts[i - 1]));
      let side = cross([0, 0, 1], t);
      if (Math.hypot(side[0], side[1], side[2]) < 0.2) side = cross([1, 0, 0], t);
      side = norm(side);
      const up = norm(cross(t, side)), ring = [];
      for (let s = 0; s < sides; s++) {
        const a = s / sides * Math.PI * 2;
        ring.push(at(p[0] + (side[0] * Math.cos(a) + up[0] * Math.sin(a)) * rad[i],
                     p[1] + (side[1] * Math.cos(a) + up[1] * Math.sin(a)) * rad[i],
                     p[2] + (side[2] * Math.cos(a) + up[2] * Math.sin(a)) * rad[i]));
      }
      return ring;
    });
    for (let i = 0; i < rings.length - 1; i++) {
      const inside = [(pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2, (pts[i][2] + pts[i + 1][2]) / 2];
      for (let s = 0; s < sides; s++) {
        const s2 = (s + 1) % sides;
        quad(rings[i][s], rings[i][s2], rings[i + 1][s2], rings[i + 1][s], rgb, mat, inside);
      }
    }
  }
  function wing(sign, phase, y0, length, breadth) {
    use([sign * 0.045, y0, -0.02], 2, 36, 4.2, phase);
    const U = 4, V = 2, grid = [];
    for (let v = 0; v <= V; v++) {
      const row = [];
      for (let u = 0; u <= U; u++) {
        const uu = u / U, vv = v / V;
        const span = length * Math.pow(uu, 0.82);
        const half = breadth * Math.sin(uu * Math.PI);
        const yLead = y0 + half * 0.95 + span * 0.04;
        const yTrail = y0 - half * 0.62 - span * 0.03;
        row.push(at(sign * (0.05 + span), yLead + (yTrail - yLead) * vv, -0.025 - uu * 0.035 + Math.sin(vv * Math.PI) * 0.012));
      }
      grid.push(row);
    }
    const inside = [sign * 0.25, y0, -1.3];
    for (let v = 0; v < V; v++) for (let u = 0; u < U; u++) {
      quad(grid[v][u], grid[v][u + 1], grid[v + 1][u + 1], grid[v + 1][u], [220, 232, 214], 3, inside);
    }
    still();
  }

  const skin = [244, 206, 184], lip = [196, 112, 122], eye = [48, 36, 40], light = [255, 248, 240];
  const foot = [232, 186, 168];

  ellipsoid([0, 1.02, 0.02], [0.1, 0.155, 0.072], [0, 0, 0], 1, 5, 8);
  tube([[0, 1.16, 0.02], [0, 1.24, 0.02]], [0.038, 0.034], skin, 0, 6);
  ellipsoid([0, 1.36, 0.03], [0.1, 0.115, 0.098], skin, 0, 5, 8);
  ellipsoid([0, 1.33, 0.115], [0.018, 0.022, 0.028], [214, 150, 130], 0, 3, 5);
  ellipsoid([-0.038, 1.375, 0.1], [0.018, 0.02, 0.014], eye, 0, 3, 4);
  ellipsoid([0.038, 1.375, 0.1], [0.018, 0.02, 0.014], eye, 0, 3, 4);
  ellipsoid([-0.032, 1.382, 0.112], [0.006, 0.006, 0.004], light, 0, 2, 3);
  ellipsoid([0.044, 1.382, 0.112], [0.006, 0.006, 0.004], light, 0, 2, 3);
  ellipsoid([0, 1.305, 0.112], [0.026, 0.01, 0.012], lip, 0, 2, 4);
  ellipsoid([-0.055, 1.33, 0.09], [0.028, 0.016, 0.012], [214, 140, 130], 0, 2, 4);
  ellipsoid([0.055, 1.33, 0.09], [0.028, 0.016, 0.012], [214, 140, 130], 0, 2, 4);
  for (const s of [-1, 1]) {
    const base = at(s * 0.09, 1.35, 0.01), mid = at(s * 0.145, 1.39, -0.01), tip = at(s * 0.175, 1.45, -0.03), low = at(s * 0.1, 1.3, 0.02);
    addTri(base, s > 0 ? mid : tip, s > 0 ? tip : mid, skin, 0, [0, 1.35, 0.08]);
    addTri(base, s > 0 ? tip : low, s > 0 ? low : tip, skin, 0, [0, 1.35, 0.08]);
  }

  const skirt = [[0.1, 0.9, 0.01], [0.15, 0.68, -0.02], [0.2, 0.46, -0.05], [0.25, 0.26, -0.08]];
  const rings = skirt.map(([rx, y, z]) => {
    const ring = [];
    for (let s = 0; s < 10; s++) {
      const a = s / 10 * Math.PI * 2;
      const back = Math.max(0, -Math.sin(a));
      ring.push(at(Math.cos(a) * rx, y - back * 0.06, z + Math.sin(a) * rx * 0.72 - back * 0.06));
    }
    return ring;
  });
  for (let i = 0; i < rings.length - 1; i++) for (let s = 0; s < 10; s++) {
    const s2 = (s + 1) % 10;
    quad(rings[i][s], rings[i][s2], rings[i + 1][s2], rings[i + 1][s], [0, 0, 0], 1, [0, 0.6, 0]);
  }

  use([0, 1.4, -0.04], 0, 8, 0.7, 0);
  ellipsoid([0, 1.4, 0.06], [0.09, 0.035, 0.04], [0, 0, 0], 2, 3, 6);
  tube([[0, 1.44, -0.01], [-0.04, 1.22, -0.08], [-0.06, 0.92, -0.12], [-0.04, 0.68, -0.08]], [0.07, 0.055, 0.045, 0.03], [0, 0, 0], 2, 5);
  tube([[0.03, 1.42, -0.02], [0.07, 1.16, -0.1], [0.05, 0.84, -0.14]], [0.055, 0.042, 0.028], [0, 0, 0], 2, 5);
  still();

  use([-0.1, 1.12, 0.02], 2, 14, 4.2, 0);
  tube([[-0.1, 1.12, 0.02], [-0.24, 1.04, 0.04], [-0.36, 0.96, 0.05]], [0.032, 0.026, 0.02], skin, 0, 5);
  ellipsoid([-0.39, 0.94, 0.055], [0.028, 0.02, 0.02], skin, 0, 3, 4);
  still();
  use([0.1, 1.12, 0.02], 2, 14, 4.2, 0.5);
  tube([[0.1, 1.12, 0.02], [0.24, 1.04, 0.04], [0.36, 0.96, 0.05]], [0.032, 0.026, 0.02], skin, 0, 5);
  ellipsoid([0.39, 0.94, 0.055], [0.028, 0.02, 0.02], skin, 0, 3, 4);
  still();

  use([0, 0.32, -0.02], 0, 10, 1.2, 0.25);
  tube([[-0.04, 0.3, -0.01], [-0.035, 0.14, -0.05], [-0.025, 0.02, -0.09]], [0.032, 0.024, 0.018], skin, 0, 5);
  tube([[0.04, 0.3, -0.01], [0.035, 0.14, -0.05], [0.025, 0.02, -0.09]], [0.032, 0.024, 0.018], skin, 0, 5);
  ellipsoid([-0.02, 0.0, -0.1], [0.02, 0.014, 0.032], foot, 0, 3, 4);
  ellipsoid([0.02, 0.0, -0.1], [0.02, 0.014, 0.032], foot, 0, 3, 4);
  still();

  wing(1, 0, 1.14, 0.72, 0.2);
  wing(-1, 0.5, 1.14, 0.72, 0.2);
  wing(1, 0.07, 0.98, 0.46, 0.13);
  wing(-1, 0.57, 0.98, 0.46, 0.13);

  let anchor = 0, best = 1e9;
  for (let i = 0; i < V.length / 3; i++) {
    const d = V[i * 3] ** 2 + (V[i * 3 + 1] - 1.02) ** 2 + (V[i * 3 + 2] - 0.02) ** 2;
    if (d < best) { best = d; anchor = i; }
  }
  return { V, T, parts, vpart, anchor };
}
const fairyMesh = buildFairyMesh();

function drawFairy(ctx, f, t) {
  const secs = t / 1000;
  const { V, T, parts, vpart, anchor } = fairyMesh;
  const n = V.length / 3;
  const swings = parts.map((p) => {
    const a = p.amp * Math.sin(2 * Math.PI * (p.hz * secs + p.phase + f.phase));
    return [Math.cos(a), Math.sin(a)];
  });
  const yaw = f.face * 0.7;
  const cyaw = Math.cos(yaw), syaw = Math.sin(yaw);
  const bob = Math.sin(2 * Math.PI * (4.2 * secs + f.phase)) * 0.012;
  const world = new Float32Array(n * 3), scr = new Float32Array(n * 3);
  const cam = [0, 1.15, 2.55], target = [0, 0.95, 0];
  const fwd = norm([target[0] - cam[0], target[1] - cam[1], target[2] - cam[2]]);
  const right = norm(cross(fwd, [0, 1, 0])), up = norm(cross(right, fwd));
  const focal = 2.5;
  for (let i = 0; i < n; i++) {
    let x = V[i * 3], y = V[i * 3 + 1] + bob, z = V[i * 3 + 2];
    const pi = vpart[i];
    if (pi) {
      const { pivot, axis } = parts[pi - 1], [pc, ps] = swings[pi - 1];
      const dx = x - pivot[0], dy = y - pivot[1], dz = z - pivot[2];
      if (axis === 0) { y = pivot[1] + pc * dy - ps * dz; z = pivot[2] + ps * dy + pc * dz; }
      else if (axis === 1) { x = pivot[0] + pc * dx + ps * dz; z = pivot[2] - ps * dx + pc * dz; }
      else { x = pivot[0] + pc * dx - ps * dy; y = pivot[1] + ps * dx + pc * dy; }
    }
    const wx = cyaw * x + syaw * z, wz = -syaw * x + cyaw * z;
    world[i * 3] = wx; world[i * 3 + 1] = y; world[i * 3 + 2] = wz;
    const d = [wx - cam[0], y - cam[1], wz - cam[2]], cz = dot(d, fwd);
    scr[i * 3] = dot(d, right) / cz * focal;
    scr[i * 3 + 1] = -dot(d, up) / cz * focal;
    scr[i * 3 + 2] = cz;
  }
  const px = 78 * f.scale;
  const dress = hexRgb(f.dress), hair = hexRgb(f.hair), wing = hexRgb(f.wing);
  const body = [], wings = [];
  for (const tri of T) {
    const a = tri[0] * 3, b = tri[1] * 3, c = tri[2] * 3;
    const e1 = [world[b] - world[a], world[b + 1] - world[a + 1], world[b + 2] - world[a + 2]];
    const e2 = [world[c] - world[a], world[c + 1] - world[a + 1], world[c + 2] - world[a + 2]];
    let nrm = norm(cross(e1, e2));
    const mid = [(world[a] + world[b] + world[c]) / 3, (world[a + 1] + world[b + 1] + world[c + 1]) / 3, (world[a + 2] + world[b + 2] + world[c + 2]) / 3];
    const view = norm([cam[0] - mid[0], cam[1] - mid[1], cam[2] - mid[2]]);
    const facing = dot(nrm, view);
    if (tri[7] !== 3 && facing <= 0) continue;
    if (facing < 0) nrm = [-nrm[0], -nrm[1], -nrm[2]];
    const rgb = tri[7] === 1 ? dress : tri[7] === 2 ? hair : tri[7] === 3 ? wing : [tri[3], tri[4], tri[5]];
    const item = [scr[a + 2] + scr[b + 2] + scr[c + 2], a, b, c, shade([0, 0, 0, rgb[0], rgb[1], rgb[2], tri[6]], nrm, view)];
    (tri[7] === 3 ? wings : body).push(item);
  }
  body.sort((p, q) => q[0] - p[0]);
  wings.sort((p, q) => q[0] - p[0]);
  const ox = f.x - scr[anchor * 3] * px, oy = f.y - scr[anchor * 3 + 1] * px;
  ctx.save();
  ctx.translate(f.x, f.y);
  ctx.rotate(f.bank);
  ctx.translate(-f.x, -f.y);
  const glow = ctx.createRadialGradient(f.x, f.y, 2, f.x, f.y, 34 * f.scale);
  glow.addColorStop(0, f.glow);
  glow.addColorStop(1, "rgba(0,0,0,0)");
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(f.x, f.y, 34 * f.scale, 0, Math.PI * 2);
  ctx.fill();
  const paint = (list) => {
    for (const [, a, b, c, col] of list) {
      ctx.fillStyle = col; ctx.strokeStyle = col; ctx.lineWidth = 0.6;
      ctx.beginPath();
      ctx.moveTo(ox + scr[a] * px, oy + scr[a + 1] * px);
      ctx.lineTo(ox + scr[b] * px, oy + scr[b + 1] * px);
      ctx.lineTo(ox + scr[c] * px, oy + scr[c + 1] * px);
      ctx.closePath();
      ctx.fill(); ctx.stroke();
    }
  };
  paint(body);
  ctx.globalAlpha = 0.62;
  paint(wings);
  ctx.restore();
}

// Low-poly gnome, shaded like the animals. Materials: 0 stored color, 1 hat, 2 coat.
function buildGnomeMesh() {
  const V = [], T = [], parts = [], vpart = [];
  let part = 0;
  const at = (x, y, z) => { V.push(x, y, z); vpart.push(part); return V.length / 3 - 1; };
  const use = (pivot, axis, deg, hz, phase) => {
    parts.push({ pivot, axis, amp: deg * Math.PI / 180, hz, phase });
    part = parts.length;
  };
  const still = () => { part = 0; };
  const xyz = (i) => [V[i * 3], V[i * 3 + 1], V[i * 3 + 2]];
  const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const addTri = (ia, ib, ic, rgb, mat, inside) => {
    const a = xyz(ia), b = xyz(ib), c = xyz(ic);
    let n = cross(sub3(b, a), sub3(c, a));
    if (Math.hypot(n[0], n[1], n[2]) < 1e-8) return;
    const mid = [(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3];
    if (dot(n, sub3(mid, inside)) < 0) [ib, ic] = [ic, ib];
    const j = 1 + (Math.random() - 0.5) * 0.06;
    T.push([ia, ib, ic, Math.min(255, rgb[0] * j) | 0, Math.min(255, rgb[1] * j) | 0, Math.min(255, rgb[2] * j) | 0, 0, mat]);
  };
  const quad = (a, b, c, d, rgb, mat, inside) => { addTri(a, b, c, rgb, mat, inside); addTri(a, c, d, rgb, mat, inside); };
  function ellipsoid(c, r, rgb, mat, lat, lon) {
    const p = (phi, th) => at(c[0] + Math.sin(phi) * Math.cos(th) * r[0], c[1] + Math.cos(phi) * r[1], c[2] + Math.sin(phi) * Math.sin(th) * r[2]);
    const top = p(0, 0), bot = p(Math.PI, 0), rings = [];
    for (let i = 1; i < lat; i++) {
      const phi = i / lat * Math.PI, ring = [];
      for (let j = 0; j < lon; j++) ring.push(p(phi, j / lon * Math.PI * 2));
      rings.push(ring);
    }
    for (let j = 0; j < lon; j++) {
      const j2 = (j + 1) % lon;
      addTri(top, rings[0][j], rings[0][j2], rgb, mat, c);
      addTri(bot, rings[rings.length - 1][j2], rings[rings.length - 1][j], rgb, mat, c);
    }
    for (let i = 0; i < rings.length - 1; i++) for (let j = 0; j < lon; j++) {
      const j2 = (j + 1) % lon;
      quad(rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j], rgb, mat, c);
    }
  }
  function tube(pts, rad, rgb, mat, sides) {
    const rings = pts.map((p, i) => {
      const t = norm(i === 0 ? sub3(pts[1], pts[0]) : i === pts.length - 1 ? sub3(pts[i], pts[i - 1]) : sub3(pts[i + 1], pts[i - 1]));
      let side = cross([0, 0, 1], t);
      if (Math.hypot(side[0], side[1], side[2]) < 0.2) side = cross([1, 0, 0], t);
      side = norm(side);
      const up = norm(cross(t, side)), ring = [];
      for (let s = 0; s < sides; s++) {
        const a = s / sides * Math.PI * 2;
        ring.push(at(p[0] + (side[0] * Math.cos(a) + up[0] * Math.sin(a)) * rad[i],
                     p[1] + (side[1] * Math.cos(a) + up[1] * Math.sin(a)) * rad[i],
                     p[2] + (side[2] * Math.cos(a) + up[2] * Math.sin(a)) * rad[i]));
      }
      return ring;
    });
    for (let i = 0; i < rings.length - 1; i++) {
      const inside = [(pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2, (pts[i][2] + pts[i + 1][2]) / 2];
      for (let s = 0; s < sides; s++) {
        const s2 = (s + 1) % sides;
        quad(rings[i][s], rings[i][s2], rings[i + 1][s2], rings[i + 1][s], rgb, mat, inside);
      }
    }
    return rings;
  }

  const skin = [236, 186, 148], beard = [246, 242, 234], nose = [214, 126, 98];
  const pants = [46, 62, 118], boot = [54, 34, 26], belt = [62, 40, 28], buckle = [214, 168, 62];
  const eye = [28, 22, 20], band = [246, 232, 196];

  use([-0.12, 0.62, 0], 0, 26, 1.6, 0);
  tube([[-0.12, 0.62, 0], [-0.12, 0.34, 0.02], [-0.13, 0.12, 0.04]], [0.09, 0.075, 0.07], pants, 0, 7);
  ellipsoid([-0.13, 0.07, 0.07], [0.1, 0.07, 0.14], boot, 0, 4, 7);
  still();
  use([0.12, 0.62, 0], 0, 26, 1.6, 0.5);
  tube([[0.12, 0.62, 0], [0.12, 0.34, 0.02], [0.13, 0.12, 0.04]], [0.09, 0.075, 0.07], pants, 0, 7);
  ellipsoid([0.13, 0.07, 0.07], [0.1, 0.07, 0.14], boot, 0, 4, 7);
  still();

  ellipsoid([0, 0.84, 0], [0.3, 0.34, 0.24], [0, 0, 0], 2, 6, 10);
  ellipsoid([0, 0.6, 0.02], [0.3, 0.055, 0.25], belt, 0, 3, 10);
  ellipsoid([0, 0.6, 0.24], [0.06, 0.045, 0.03], buckle, 0, 3, 6);
  ellipsoid([0, 1.0, 0.1], [0.18, 0.26, 0.14], beard, 0, 5, 8);

  use([-0.28, 1.02, 0], 0, 32, 1.6, 0.5);
  tube([[-0.24, 1.0, 0.02], [-0.34, 0.78, 0.08], [-0.3, 0.58, 0.14]], [0.07, 0.06, 0.055], [0, 0, 0], 2, 6);
  ellipsoid([-0.28, 0.52, 0.16], [0.055, 0.05, 0.05], skin, 0, 3, 6);
  still();
  use([0.28, 1.02, 0], 0, 32, 1.6, 0);
  tube([[0.24, 1.0, 0.02], [0.34, 0.78, 0.08], [0.3, 0.58, 0.14]], [0.07, 0.06, 0.055], [0, 0, 0], 2, 6);
  ellipsoid([0.28, 0.52, 0.16], [0.055, 0.05, 0.05], skin, 0, 3, 6);
  still();

  ellipsoid([0, 1.26, 0.04], [0.22, 0.21, 0.2], skin, 0, 6, 10);
  ellipsoid([0, 1.22, 0.22], [0.07, 0.09, 0.08], nose, 0, 4, 7);
  ellipsoid([-0.08, 1.3, 0.2], [0.035, 0.04, 0.025], eye, 0, 3, 5);
  ellipsoid([0.08, 1.3, 0.2], [0.035, 0.04, 0.025], eye, 0, 3, 5);
  ellipsoid([-0.09, 1.24, 0.18], [0.04, 0.025, 0.02], [214, 120, 120], 0, 3, 5);
  ellipsoid([0.09, 1.24, 0.18], [0.04, 0.025, 0.02], [214, 120, 120], 0, 3, 5);
  ellipsoid([0, 1.08, 0.16], [0.16, 0.2, 0.12], beard, 0, 5, 8);

  const hat = [
    [0, 1.44, 0.02, 0.15],
    [0.05, 1.6, 0.03, 0.1],
    [0.14, 1.74, 0.03, 0.055],
    [0.24, 1.84, 0.02, 0.028],
  ];
  const rings = hat.map(([x, y, z, r]) => {
    const ring = [];
    for (let s = 0; s < 10; s++) {
      const a = s / 10 * Math.PI * 2;
      ring.push(at(x + Math.cos(a) * r, y, z + Math.sin(a) * r));
    }
    return ring;
  });
  const insideHat = [0.04, 1.6, 0.08];
  for (let i = 0; i < rings.length - 1; i++) for (let s = 0; s < 10; s++) {
    const s2 = (s + 1) % 10;
    quad(rings[i][s], rings[i][s2], rings[i + 1][s2], rings[i + 1][s], [0, 0, 0], 1, insideHat);
  }
  const tip = at(0.34, 1.9, 0.02);
  for (let s = 0; s < 10; s++) addTri(tip, rings[3][s], rings[3][(s + 1) % 10], [0, 0, 0], 1, insideHat);
  const brim = [], brimLo = [];
  for (let s = 0; s < 12; s++) {
    const a = s / 12 * Math.PI * 2;
    brim.push(at(Math.cos(a) * 0.3, 1.42, Math.sin(a) * 0.3));
    brimLo.push(at(Math.cos(a) * 0.3, 1.385, Math.sin(a) * 0.3));
  }
  const hub = at(0, 1.42, 0);
  for (let s = 0; s < 12; s++) {
    const s2 = (s + 1) % 12;
    addTri(hub, brim[s], brim[s2], [0, 0, 0], 1, [0, 1.2, 0]);
    quad(brim[s], brim[s2], brimLo[s2], brimLo[s], [0, 0, 0], 1, [0, 1.38, 0]);
  }
  ellipsoid([0, 1.44, 0.02], [0.17, 0.03, 0.17], band, 0, 3, 8);

  return { V, T, parts, vpart };
}
const gnomeMesh = buildGnomeMesh();

function drawGnome(ctx, g, t) {
  const walking = Math.sin(t * 0.004 + g.phase) > -0.35 ? 1 : 0.12;
  const secs = t / 1000;
  const { V, T, parts, vpart } = gnomeMesh;
  const n = V.length / 3;
  const swings = parts.map((p) => {
    const a = p.amp * walking * Math.sin(2 * Math.PI * (p.hz * secs + p.phase + g.phase));
    return [Math.cos(a), Math.sin(a)];
  });
  const yaw = g.dir * 0.45;
  const cyaw = Math.cos(yaw), syaw = Math.sin(yaw);
  const bob = (walking > 0.5 ? Math.abs(Math.sin(t * 0.01 * g.speed * 7 + g.phase)) : 0) * 0.035;
  const world = new Float32Array(n * 3), scr = new Float32Array(n * 3);
  const cam = [0, 1.15, 2.55], target = [0, 0.86, 0];
  const fwd = norm([target[0] - cam[0], target[1] - cam[1], target[2] - cam[2]]);
  const right = norm(cross(fwd, [0, 1, 0])), up = norm(cross(right, fwd));
  const focal = 2.35;
  for (let i = 0; i < n; i++) {
    let x = V[i * 3], y = V[i * 3 + 1] + bob, z = V[i * 3 + 2];
    const pi = vpart[i];
    if (pi) {
      const { pivot, axis } = parts[pi - 1], [pc, ps] = swings[pi - 1];
      const dx = x - pivot[0], dy = y - pivot[1], dz = z - pivot[2];
      if (axis === 0) { y = pivot[1] + pc * dy - ps * dz; z = pivot[2] + ps * dy + pc * dz; }
      else if (axis === 1) { x = pivot[0] + pc * dx + ps * dz; z = pivot[2] - ps * dx + pc * dz; }
      else { x = pivot[0] + pc * dx - ps * dy; y = pivot[1] + ps * dx + pc * dy; }
    }
    const wx = cyaw * x + syaw * z, wz = -syaw * x + cyaw * z;
    world[i * 3] = wx; world[i * 3 + 1] = y; world[i * 3 + 2] = wz;
    const d = [wx - cam[0], y - cam[1], wz - cam[2]], cz = dot(d, fwd);
    scr[i * 3] = dot(d, right) / cz * focal;
    scr[i * 3 + 1] = -dot(d, up) / cz * focal;
    scr[i * 3 + 2] = cz;
  }
  let footI = 0;
  for (let i = 1; i < n; i++) if (world[i * 3 + 1] < world[footI * 3 + 1]) footI = i;
  const foot = [scr[footI * 3], scr[footI * 3 + 1]];
  const px = 86 * g.scale;
  const hat = hexRgb(g.hat), coat = hexRgb(g.coat);
  const list = [];
  for (const tri of T) {
    const a = tri[0] * 3, b = tri[1] * 3, c = tri[2] * 3;
    const e1 = [world[b] - world[a], world[b + 1] - world[a + 1], world[b + 2] - world[a + 2]];
    const e2 = [world[c] - world[a], world[c + 1] - world[a + 1], world[c + 2] - world[a + 2]];
    const nrm = norm(cross(e1, e2));
    const mid = [(world[a] + world[b] + world[c]) / 3, (world[a + 1] + world[b + 1] + world[c + 1]) / 3, (world[a + 2] + world[b + 2] + world[c + 2]) / 3];
    const view = norm([cam[0] - mid[0], cam[1] - mid[1], cam[2] - mid[2]]);
    if (dot(nrm, view) <= 0) continue;
    const rgb = tri[7] === 1 ? hat : tri[7] === 2 ? coat : [tri[3], tri[4], tri[5]];
    list.push([scr[a + 2] + scr[b + 2] + scr[c + 2], a, b, c, shade([0, 0, 0, rgb[0], rgb[1], rgb[2], tri[6]], nrm, view)]);
  }
  list.sort((p, q) => q[0] - p[0]);
  const ox = g.x - foot[0] * px, oy = g.y - foot[1] * px;
  for (const [, a, b, c, col] of list) {
    ctx.fillStyle = col; ctx.strokeStyle = col; ctx.lineWidth = 0.8;
    ctx.beginPath();
    ctx.moveTo(ox + scr[a] * px, oy + scr[a + 1] * px);
    ctx.lineTo(ox + scr[b] * px, oy + scr[b + 1] * px);
    ctx.lineTo(ox + scr[c] * px, oy + scr[c + 1] * px);
    ctx.closePath();
    ctx.fill(); ctx.stroke();
  }
}

function hexRgb(h) {
  const n = parseInt(h.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function stepGarden(t) {
  const on = Object.fromEntries(GARDEN.map((g) => [g.id, g.on]));
  if (on.fairies) for (const f of fairies) {
    const a = t * f.speed + f.phase;
    f.px = f.x; f.py = f.y;
    f.x = f.cx + Math.cos(a) * f.rx;
    f.y = f.cy + (f.eight ? Math.sin(a * 2) : Math.sin(a)) * f.ry;
    const dx = f.x - f.px;
    if (dx > 0.35) f.face = 1;
    else if (dx < -0.35) f.face = -1;
    f.bank = Math.max(-0.4, Math.min(0.4, (f.y - f.py) * 0.04));
  }
  if (on.gnomes) for (const g of gnomes) {
    const walking = Math.sin(t * 0.004 + g.phase) > -0.35;
    if (walking) g.x += g.dir * g.speed;
    if (g.x > 740) g.dir = -1;
    if (g.x < 60) g.dir = 1;
  }
}

function paintGarden(ctx, w, h, t) {
  ctx.clearRect(0, 0, w, h);
  const sx = w / 800, sy = h / 480;
  ctx.save();
  ctx.scale(sx, sy);

  const sky = ctx.createLinearGradient(0, 0, 0, 320);
  sky.addColorStop(0, "#120a24");
  sky.addColorStop(0.45, "#3a2466");
  sky.addColorStop(1, "#6d3d62");
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, 800, 330);

  const moon = ctx.createRadialGradient(640, 74, 8, 640, 74, 130);
  moon.addColorStop(0, "rgba(255, 236, 190, 0.95)");
  moon.addColorStop(0.25, "rgba(255, 228, 170, 0.35)");
  moon.addColorStop(1, "rgba(255, 228, 170, 0)");
  ctx.fillStyle = moon;
  ctx.fillRect(500, 0, 280, 220);
  ctx.fillStyle = "#fff4d2";
  ctx.beginPath();
  ctx.arc(640, 74, 28, 0, Math.PI * 2);
  ctx.fill();

  for (const s of gardenStars) {
    ctx.globalAlpha = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(t * 0.003 * s.speed + s.phase));
    ctx.fillStyle = "#fff8e4";
    ctx.fillRect(s.x, s.y, s.r, s.r);
  }
  ctx.globalAlpha = 1;

  ctx.fillStyle = "#2a3f68";
  ctx.beginPath();
  ctx.moveTo(0, 250);
  ctx.quadraticCurveTo(160, 180, 340, 236);
  ctx.quadraticCurveTo(520, 290, 800, 190);
  ctx.lineTo(800, 330);
  ctx.lineTo(0, 330);
  ctx.fill();
  ctx.fillStyle = "#1b4638";
  ctx.beginPath();
  ctx.moveTo(0, 290);
  ctx.quadraticCurveTo(220, 250, 460, 310);
  ctx.quadraticCurveTo(640, 350, 800, 280);
  ctx.lineTo(800, 360);
  ctx.lineTo(0, 360);
  ctx.fill();

  ctx.save();
  ctx.translate(150, 250);
  ctx.fillStyle = "#24182f";
  ctx.fillRect(-26, -8, 52, 34);
  ctx.beginPath();
  ctx.moveTo(-34, -6);
  ctx.lineTo(0, -40);
  ctx.lineTo(34, -6);
  ctx.fill();
  ctx.globalAlpha = 0.55 + 0.45 * (0.5 + 0.5 * Math.sin(t * 0.007));
  ctx.fillStyle = "#ffc45a";
  ctx.fillRect(-6, 4, 10, 12);
  ctx.restore();
  ctx.globalAlpha = 1;

  const ground = ctx.createLinearGradient(0, 320, 0, 480);
  ground.addColorStop(0, "#1f6a40");
  ground.addColorStop(1, "#0d3a28");
  ctx.fillStyle = ground;
  ctx.fillRect(0, 318, 800, 162);

  ctx.fillStyle = "#7a5b45";
  ctx.beginPath();
  ctx.moveTo(340, 480);
  ctx.quadraticCurveTo(430, 400, 520, 348);
  ctx.lineTo(566, 362);
  ctx.quadraticCurveTo(470, 420, 400, 480);
  ctx.fill();

  for (const m of gardenMushrooms) {
    const s = 16 * m.scale;
    ctx.fillStyle = "#f3efe4";
    ctx.fillRect(m.x - 3, m.y - s * 0.7, 6, s * 0.7);
    ctx.fillStyle = m.cap;
    ctx.beginPath();
    ctx.ellipse(m.x, m.y - s * 0.7, s, s * 0.55, 0, Math.PI, 0, true);
    ctx.fill();
    if (m.cap === "#d24a4a") {
      ctx.fillStyle = "#fff6e8";
      ctx.beginPath();
      ctx.arc(m.x - s * 0.3, m.y - s * 0.85, 2.2, 0, Math.PI * 2);
      ctx.arc(m.x + s * 0.25, m.y - s * 0.7, 1.6, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  for (const f of gardenFlowers) {
    const sway = Math.sin(t * 0.002 + f.phase) * 0.35;
    ctx.save();
    ctx.translate(f.x, f.y);
    ctx.rotate(sway);
    ctx.strokeStyle = "#1d5a32";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(0, -f.h);
    ctx.stroke();
    ctx.fillStyle = `hsl(${f.hue} 75% 62%)`;
    ctx.beginPath();
    ctx.arc(0, -f.h, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#ffe08a";
    ctx.beginPath();
    ctx.arc(0, -f.h, 2, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  for (const fly of gardenFlies) {
    const x = fly.x + Math.sin(t * 0.0015 + fly.phase) * fly.drift;
    const y = fly.y + Math.cos(t * 0.0018 + fly.phase) * 10;
    ctx.fillStyle = "rgba(210, 255, 140, 0.9)";
    ctx.globalAlpha = 0.45 + 0.55 * (0.5 + 0.5 * Math.sin(t * 0.01 + fly.phase));
    ctx.beginPath();
    ctx.arc(x, y, 2.2, 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = 1;
  }

  const on = Object.fromEntries(GARDEN.map((g) => [g.id, g.on]));
  if (on.gnomes) for (const g of gnomes) drawGnome(ctx, g, t);
  if (on.fairies) for (const f of fairies) drawFairy(ctx, f, t);
  ctx.restore();
}

const gardenCtx = $("garden").getContext("2d");
function gardenFrame(t) {
  requestAnimationFrame(gardenFrame);
  stepGarden(t);
  paintGarden(gardenCtx, 800, 480, t);
}
requestAnimationFrame(gardenFrame);

let gardenStreaming = false;
const gardenSend = document.createElement("canvas");
const gardenSendCtx = gardenSend.getContext("2d");

async function pumpGarden() {
  let frames = 0, t0 = performance.now();
  while (gardenStreaming) {
    gardenSend.width = 400; gardenSend.height = 240;
    paintGarden(gardenSendCtx, 400, 240, performance.now());
    const blob = await new Promise((r) => gardenSend.toBlob(r, "image/jpeg", 0.72));
    try {
      const res = await fetch("/frame?zoom=2", { method: "POST", body: blob, headers: { "Content-Type": "application/octet-stream" } });
      if (!res.ok) throw new Error(res.status);
      frames++;
    } catch (err) {
      $("gardenMsg").textContent = "Board not responding (" + err.message + "). Retrying…";
      await new Promise((r) => setTimeout(r, 800));
      continue;
    }
    const dt = performance.now() - t0;
    if (dt > 1000) {
      $("gardenMsg").textContent = (frames * 1000 / dt).toFixed(1) + " fps on the display";
      frames = 0; t0 = performance.now();
    }
  }
}

$("gardenGo").onclick = () => {
  if (gardenStreaming || wildStreaming || streaming || nesStreaming) return;
  gardenStreaming = true;
  $("gardenGo").disabled = true; $("gardenStop").disabled = false;
  pumpGarden();
};
$("gardenStop").onclick = () => {
  gardenStreaming = false;
  $("gardenGo").disabled = false; $("gardenStop").disabled = true;
  $("gardenMsg").textContent = "Stopped.";
  fetch("/stop", { method: "POST" }).catch(() => {});
};
