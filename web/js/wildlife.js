// ---------------------------------------------------------------- Wildlife
const WILD = [
  { id: "butterflies", label: "Butterflies", on: true },
  { id: "deer", label: "Deer", on: true },
  { id: "birds", label: "Birds", on: true },
  { id: "rabbits", label: "Rabbits", on: true },
];
const butterflies = [], deer = [], birds = [], rabbits = [];
const rand = (a, b) => a + Math.random() * (b - a);
const hue = () => `hsl(${rand(0, 360) | 0} ${rand(70, 100) | 0}% ${rand(52, 68) | 0}%)`;

function spawnButterfly() {
  return { x: rand(40, 760), y: rand(40, 300), vx: rand(-0.6, 0.6), vy: rand(-0.35, 0.35),
           hue: hue(), phase: rand(0, 6), size: rand(14, 28), wobble: rand(0.4, 1.2) };
}
function spawnDeer() {
  return { x: rand(-80, 800), y: rand(300, 420), dir: Math.random() < 0.5 ? 1 : -1, speed: rand(0.35, 0.7),
           phase: rand(0, 6), scale: rand(0.7, 1.15) };
}
function spawnBird() {
  return { x: rand(-40, 840), y: rand(20, 180), dir: Math.random() < 0.5 ? 1 : -1, speed: rand(0.8, 1.6),
           phase: rand(0, 6), size: rand(8, 14), bob: rand(0.3, 0.8) };
}
function spawnRabbit() {
  return { x: rand(0, 800), y: rand(340, 450), hop: rand(0, 6), speed: rand(0.5, 0.9),
           dir: Math.random() < 0.5 ? 1 : -1, scale: rand(0.7, 1) };
}
for (let i = 0; i < 14; i++) butterflies.push(spawnButterfly());
for (let i = 0; i < 3; i++) deer.push(spawnDeer());
for (let i = 0; i < 6; i++) birds.push(spawnBird());
for (let i = 0; i < 4; i++) rabbits.push(spawnRabbit());

WILD.forEach((w) => {
  const b = document.createElement("button");
  b.className = "chip"; b.textContent = w.label; b.setAttribute("aria-pressed", "true");
  b.onclick = () => { w.on = !w.on; b.setAttribute("aria-pressed", w.on); };
  $("wildChips").appendChild(b);
});

function drawButterfly(ctx, b, t) {
  const flap = Math.sin(t * 0.012 * b.wobble + b.phase);
  const wing = 0.25 + 0.75 * Math.abs(flap);
  ctx.save();
  ctx.translate(b.x, b.y);
  ctx.rotate(Math.sin(t * 0.002 + b.phase) * 0.4);
  ctx.fillStyle = b.hue;
  ctx.beginPath();
  ctx.ellipse(-b.size * 0.15, 0, b.size * wing, b.size * 0.62, -0.5, 0, Math.PI * 2);
  ctx.ellipse(b.size * 0.15, 0, b.size * wing, b.size * 0.62, 0.5, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#1a1208";
  ctx.fillRect(-1.2, -b.size * 0.35, 2.4, b.size * 0.7);
  ctx.restore();
}

function drawDeer(ctx, d, t) {
  const s = 46 * d.scale;
  const step = Math.sin(t * 0.008 * d.speed * 8 + d.phase);
  ctx.save();
  ctx.translate(d.x, d.y);
  ctx.scale(d.dir, 1);
  ctx.fillStyle = "#c4a574";
  ctx.beginPath();
  ctx.ellipse(0, -s * 0.55, s * 0.72, s * 0.32, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.ellipse(s * 0.62, -s * 0.95, s * 0.22, s * 0.16, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = "#e8d7b0";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(s * 0.7, -s * 1.05);
  ctx.quadraticCurveTo(s * 0.85, -s * 1.45, s * 0.55, -s * 1.25);
  ctx.moveTo(s * 0.75, -s * 1.08);
  ctx.quadraticCurveTo(s * 1.05, -s * 1.35, s * 0.95, -s * 1.05);
  ctx.stroke();
  ctx.strokeStyle = "#8d6b45";
  ctx.lineWidth = 3;
  const legs = [[-s * 0.35, step], [-s * 0.1, -step], [s * 0.15, step], [s * 0.4, -step]];
  for (const [x, swing] of legs) {
    ctx.beginPath();
    ctx.moveTo(x, -s * 0.28);
    ctx.lineTo(x + swing * s * 0.18, 0);
    ctx.stroke();
  }
  ctx.restore();
}

function drawBird(ctx, b, t) {
  const flap = Math.sin(t * 0.02 + b.phase);
  ctx.save();
  ctx.translate(b.x, b.y);
  ctx.scale(b.dir, 1);
  ctx.strokeStyle = "#f4f1ea";
  ctx.lineWidth = 1.6;
  ctx.beginPath();
  ctx.moveTo(-b.size, flap * b.size * 0.45);
  ctx.quadraticCurveTo(0, -b.size * 0.2, b.size, flap * b.size * 0.45);
  ctx.stroke();
  ctx.restore();
}

function drawRabbit(ctx, r, t) {
  const hop = Math.abs(Math.sin(t * 0.01 * r.speed + r.hop));
  const s = 18 * r.scale;
  ctx.save();
  ctx.translate(r.x, r.y - hop * 16);
  ctx.scale(r.dir, 1);
  ctx.fillStyle = "#f3efe6";
  ctx.beginPath();
  ctx.ellipse(0, -s * 0.45, s * 0.7, s * 0.4, 0, 0, Math.PI * 2);
  ctx.ellipse(s * 0.55, -s * 0.85, s * 0.28, s * 0.22, 0, 0, Math.PI * 2);
  ctx.ellipse(s * 0.42, -s * 1.25, s * 0.08, s * 0.28, -0.2, 0, Math.PI * 2);
  ctx.ellipse(s * 0.62, -s * 1.22, s * 0.08, s * 0.26, 0.25, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function stepWildlife(t) {
  const on = Object.fromEntries(WILD.map((w) => [w.id, w.on]));
  if (on.butterflies) for (const b of butterflies) {
    b.x += b.vx + Math.sin(t * 0.001 + b.phase) * 0.4;
    b.y += b.vy + Math.cos(t * 0.0013 + b.phase) * 0.25;
    if (b.x < 10 || b.x > 790) b.vx *= -1;
    if (b.y < 16 || b.y > 340) b.vy *= -1;
  }
  if (on.deer) for (const d of deer) {
    d.x += d.dir * d.speed;
    if (d.x > 900) d.x = -100;
    if (d.x < -120) d.x = 880;
  }
  if (on.birds) for (const b of birds) {
    b.x += b.dir * b.speed;
    b.y += Math.sin(t * 0.003 + b.phase) * b.bob * 0.4;
    if (b.x > 860) b.x = -40;
    if (b.x < -50) b.x = 850;
  }
  if (on.rabbits) for (const r of rabbits) {
    const moving = Math.sin(t * 0.01 * r.speed + r.hop) > 0;
    if (moving) r.x += r.dir * r.speed;
    if (r.x > 840) r.dir = -1;
    if (r.x < -40) r.dir = 1;
  }
}

function paintWildlife(ctx, w, h, t) {
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, w, h);
  const sx = w / 800, sy = h / 480;
  ctx.save();
  ctx.scale(sx, sy);
  const on = Object.fromEntries(WILD.map((wld) => [wld.id, wld.on]));
  if (on.deer) for (const d of deer) drawDeer(ctx, d, t);
  if (on.rabbits) for (const r of rabbits) drawRabbit(ctx, r, t);
  if (on.birds) for (const b of birds) drawBird(ctx, b, t);
  if (on.butterflies) for (const b of butterflies) drawButterfly(ctx, b, t);
  ctx.restore();
}

const wildCtx = $("wild").getContext("2d");
function wildFrame(t) {
  requestAnimationFrame(wildFrame);
  stepWildlife(t);
  paintWildlife(wildCtx, 800, 480, t);
}
requestAnimationFrame(wildFrame);

let wildStreaming = false;
const wildSend = document.createElement("canvas");
const wildSendCtx = wildSend.getContext("2d");

async function pumpWildlife() {
  let frames = 0, t0 = performance.now();
  while (wildStreaming) {
    wildSend.width = 400; wildSend.height = 240;
    paintWildlife(wildSendCtx, 400, 240, performance.now());
    const blob = await new Promise((r) => wildSend.toBlob(r, "image/jpeg", 0.72));
    try {
      const res = await fetch("/frame?zoom=2", { method: "POST", body: blob, headers: { "Content-Type": "application/octet-stream" } });
      if (!res.ok) throw new Error(res.status);
      frames++;
    } catch (err) {
      $("wildMsg").textContent = "Board not responding (" + err.message + "). Retrying…";
      await new Promise((r) => setTimeout(r, 800));
      continue;
    }
    const dt = performance.now() - t0;
    if (dt > 1000) {
      $("wildMsg").textContent = (frames * 1000 / dt).toFixed(1) + " fps on the display";
      frames = 0; t0 = performance.now();
    }
  }
}

$("wildGo").onclick = () => {
  if (wildStreaming || streaming || gardenStreaming || nesStreaming) return;
  wildStreaming = true;
  $("wildGo").disabled = true; $("wildStop").disabled = false;
  pumpWildlife();
};
$("wildStop").onclick = () => {
  wildStreaming = false;
  $("wildGo").disabled = false; $("wildStop").disabled = true;
  $("wildMsg").textContent = "Stopped.";
  fetch("/stop", { method: "POST" }).catch(() => {});
};

