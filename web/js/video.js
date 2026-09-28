// ---------------------------------------------------------------- Video
const video = $("video"), canvas = $("canvas"), ctx = canvas.getContext("2d");
let streaming = false;

function load(file) {
  if (!file || !file.type.startsWith("video/")) return;
  video.src = URL.createObjectURL(file);
  video.style.display = "block";
  video.play();
  $("go").disabled = false;
}
$("drop").onclick = () => $("file").click();
$("file").onchange = (e) => load(e.target.files[0]);
$("drop").ondragover = (e) => { e.preventDefault(); $("drop").classList.add("over"); };
$("drop").ondragleave = () => $("drop").classList.remove("over");
$("drop").ondrop = (e) => { e.preventDefault(); $("drop").classList.remove("over"); load(e.dataTransfer.files[0]); };

function drawFrame(zoom) {
  const w = 800 / zoom, h = 480 / zoom;
  canvas.width = w; canvas.height = h;
  ctx.fillStyle = "#000"; ctx.fillRect(0, 0, w, h);
  const vw = video.videoWidth, vh = video.videoHeight;
  if (!vw || !vh) return;
  const s = $("fit").value === "cover" ? Math.max(w / vw, h / vh) : Math.min(w / vw, h / vh);
  ctx.save();
  // Mirror the same way the board mirrors the animals in hologram mode.
  if (holo.hologram) {
    ctx.translate(holo.rotate ? w : 0, holo.rotate ? 0 : h);
    ctx.scale(holo.rotate ? -1 : 1, holo.rotate ? 1 : -1);
  }
  ctx.drawImage(video, (w - vw * s) / 2, (h - vh * s) / 2, vw * s, vh * s);
  ctx.restore();
}

async function pump() {
  let frames = 0, bytes = 0, t0 = performance.now();
  while (streaming) {
    const zoom = Number($("res").value);
    drawFrame(zoom);
    const blob = await new Promise((r) => canvas.toBlob(r, "image/jpeg", Number($("quality").value)));
    try {
      const res = await fetch("/frame?zoom=" + zoom, { method: "POST", body: blob, headers: { "Content-Type": "application/octet-stream" } });
      if (!res.ok) throw new Error(res.status + " " + (await res.text()));
      frames++; bytes += blob.size;
    } catch (err) {
      $("stats").textContent = "Board not responding (" + err.message + "). Retrying…";
      await new Promise((r) => setTimeout(r, 800));
      continue;
    }
    const dt = performance.now() - t0;
    if (dt > 1000) {
      $("stats").textContent = (frames * 1000 / dt).toFixed(1) + " fps · " + Math.round(bytes / frames / 1024) + " KB per frame";
      frames = 0; bytes = 0; t0 = performance.now();
    }
  }
}

$("go").onclick = () => {
  if (streaming || wildStreaming || gardenStreaming || nesStreaming) return;
  streaming = true;
  $("go").disabled = true; $("stop").disabled = false;
  video.play();
  pump();
};
$("stop").onclick = () => {
  streaming = false;
  $("go").disabled = false; $("stop").disabled = true;
  $("stats").textContent = "Stopped.";
  fetch("/stop", { method: "POST" }).catch(() => {});
};
