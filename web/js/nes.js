// ---------------------------------------------------------------- NES
// JSNES runs in the browser. Frames are JPEGs posted to /frame, same as video.
const nesView = $("nesView"), nesCtx = nesView.getContext("2d", { alpha: false });
const nesImage = nesCtx.createImageData(256, 240);
const nesOut = document.createElement("canvas");
const nesOutCtx = nesOut.getContext("2d");
let nes = null, nesOn = false, nesStreaming = false;
const keyBits = { ArrowUp: 4, ArrowDown: 5, ArrowLeft: 6, ArrowRight: 7, KeyZ: 1, KeyX: 0, Enter: 3, ShiftLeft: 2, ShiftRight: 2 };
let audioCtx = null, audioNode = null;
const audioBuf = new Float32Array(16384);
let audioR = 0, audioW = 0;

function onAudioSample(l, r) {
  audioBuf[audioW] = (l + r) * 0.5;
  audioW = (audioW + 1) & 16383;
  if (audioW === audioR) audioR = (audioR + 1) & 16383;
}
function ensureAudio() {
  if (audioCtx) { audioCtx.resume(); return; }
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return;
  audioCtx = new AC();
  audioNode = audioCtx.createScriptProcessor(1024, 0, 1);
  audioNode.onaudioprocess = (e) => {
    const out = e.outputBuffer.getChannelData(0);
    for (let i = 0; i < out.length; i++) {
      if (audioR === audioW) { out[i] = 0; continue; }
      out[i] = audioBuf[audioR];
      audioR = (audioR + 1) & 16383;
    }
  };
  audioNode.connect(audioCtx.destination);
}

function setBit(bit, down) {
  if (!nes) return;
  if (down) nes.buttonDown(1, bit);
  else nes.buttonUp(1, bit);
}
document.querySelectorAll("#controller button[data-bit]").forEach((b) => {
  const bit = Number(b.dataset.bit);
  const down = (e) => { e.preventDefault(); b.classList.add("held"); setBit(bit, true); };
  const up = (e) => { e.preventDefault(); b.classList.remove("held"); setBit(bit, false); };
  b.onpointerdown = down;
  b.onpointerup = up;
  b.onpointerleave = up;
});
window.addEventListener("keydown", (e) => {
  if (!(e.code in keyBits) || e.repeat) return;
  e.preventDefault();
  setBit(keyBits[e.code], true);
});
window.addEventListener("keyup", (e) => {
  if (!(e.code in keyBits)) return;
  setBit(keyBits[e.code], false);
});

function romToString(buf) {
  const bytes = new Uint8Array(buf);
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return s;
}

function bootRom(buf, name) {
  if (typeof jsnes === "undefined") { $("nesNote").textContent = "The emulator script did not load."; return; }
  ensureAudio();
  nes = new jsnes.NES({
    onFrame(fb) {
      const d = nesImage.data;
      for (let i = 0, p = 0; i < fb.length; i++, p += 4) {
        const c = fb[i];
        d[p] = c >> 16; d[p + 1] = c >> 8; d[p + 2] = c; d[p + 3] = 255;
      }
      nesCtx.putImageData(nesImage, 0, 0);
    },
    onAudioSample,
  });
  nes.loadROM(romToString(buf));
  nesOn = true;
  $("nesGo").disabled = false;
  $("nesNote").textContent = name + " is running.";
}

async function openController() {
  $("controller").classList.add("open");
  document.body.style.overflow = "hidden";
  try { await document.documentElement.requestFullscreen(); } catch (e) {}
  try { await screen.orientation.lock("landscape"); } catch (e) {}
}
function closeController() {
  $("controller").classList.remove("open");
  document.body.style.overflow = "";
  try { screen.orientation.unlock(); } catch (e) {}
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
}
$("nesPad").onclick = openController;
$("controllerClose").onclick = (e) => { e.stopPropagation(); closeController(); };
$("nesPick").onclick = () => $("nesFile").click();
$("nesFile").onchange = () => {
  const file = $("nesFile").files[0];
  if (!file) return;
  file.arrayBuffer().then((buf) => bootRom(buf, file.name)).catch((err) => { $("nesNote").textContent = "Couldn't start that ROM. " + err.message; });
};

function nesTick() {
  requestAnimationFrame(nesTick);
  if (nesOn && nes) nes.frame();
}
requestAnimationFrame(nesTick);

// 128×120 is every other NES pixel. A full 256×240 frame is 120 KB and the
// board drops that upload. The panel stretches this back to full screen.
const NES_W = 128, NES_H = 120;
const nesRaw = new Uint8Array(NES_W * NES_H * 2);
function packNes() {
  const px = nesCtx.getImageData(0, 0, 256, 240).data;
  let o = 0;
  for (let y = 0; y < NES_H; y++) {
    const row = y * 2 * 256 * 4;
    for (let x = 0; x < NES_W; x++) {
      const i = row + x * 2 * 4;
      const v = ((px[i] & 248) << 8) | ((px[i + 1] & 252) << 3) | (px[i + 2] >> 3);
      nesRaw[o++] = v;
      nesRaw[o++] = v >> 8;
    }
  }
}

async function pumpNes() {
  let frames = 0, t0 = performance.now();
  while (nesStreaming) {
    packNes();
    try {
      const res = await fetch("/frame?raw=565&w=" + NES_W + "&h=" + NES_H, { method: "POST", body: nesRaw, headers: { "Content-Type": "application/octet-stream" } });
      if (!res.ok) throw new Error(res.status);
      frames++;
    } catch (err) {
      $("nesMsg").textContent = "Board not responding (" + err.message + "). Retrying…";
      await new Promise((r) => setTimeout(r, 800));
      continue;
    }
    const dt = performance.now() - t0;
    if (dt > 1000) {
      $("nesMsg").textContent = (frames * 1000 / dt).toFixed(1) + " fps on the display";
      frames = 0; t0 = performance.now();
    }
  }
}
$("nesGo").onclick = () => {
  if (nesStreaming || streaming || wildStreaming || gardenStreaming || !nesOn) return;
  nesStreaming = true;
  $("nesGo").disabled = true; $("nesStop").disabled = false;
  pumpNes();
};
$("nesStop").onclick = () => {
  nesStreaming = false;
  $("nesGo").disabled = !nesOn; $("nesStop").disabled = true;
  $("nesMsg").textContent = "Stopped.";
  fetch("/stop", { method: "POST" }).catch(() => {});
};

