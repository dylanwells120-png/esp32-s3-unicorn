#pragma once
// Built-in page at "/setup" for managing the SD card over Wi-Fi. It works with
// an empty card, so the rest of the web app can be uploaded from here.

static const char kSetupPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Unicorn Setup</title>
<style>
  :root { color-scheme: dark; --bg: #0e0e12; --panel: #18181e; --line: #2c2c36; --text: #f2f2f5; --dim: #9a9aa8; --accent: #f06c9b; --bad: #ff8a80; }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, sans-serif; }
  main { max-width: 640px; margin: 0 auto; padding: 24px 16px 40px; }
  h1 { font-size: 22px; margin: 0 0 4px; }
  p { color: var(--dim); margin: 0 0 16px; }
  a { color: var(--accent); }
  .card { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; margin-bottom: 12px; }
  .card h2 { font-size: 16px; margin: 0 0 2px; }
  .card .meta { color: var(--dim); font-size: 13px; margin-bottom: 10px; }
  .row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
  button { font: inherit; font-weight: 600; border: 0; border-radius: 10px; padding: 8px 18px; cursor: pointer; background: var(--accent); color: #1a0d13; }
  button.plain { background: var(--line); color: var(--text); }
  .msg { font-size: 13px; color: var(--dim); }
  .msg.bad { color: var(--bad); }
</style>
</head>
<body>
<main>
  <h1>SD card</h1>
  <p id="status">Checking the card…</p>
  <div id="slots"></div>
  <div class="row"><button class="plain" id="restart">Restart board</button><a href="/">Open the web app</a></div>
</main>
<script>
const slots = [
  ["animals.bin", "3D models", "Built by tools/build_mesh.py. Loaded right away."],
  ["index.html", "Web app", "The page at /. Reload it after uploading."],
  ["firmware.bin", "Firmware", "Installed from the card on the next restart."],
];
const $ = (id) => document.getElementById(id);
const kb = (n) => n > 1048576 ? (n / 1048576).toFixed(1) + " MB" : Math.ceil(n / 1024) + " KB";

async function refresh() {
  const info = await (await fetch("/files")).json();
  $("status").textContent = !info.sd ? "No SD card found. Insert one and restart the board."
    : info.animals.length ? "Models on the card: " + info.animals.join(", ") + "."
    : "No models loaded (" + info.modelError + ").";
  $("slots").innerHTML = "";
  for (const [name, title, help] of slots) {
    const f = info.files.find((x) => x.name === name);
    const done = info.files.find((x) => x.name === "firmware.done");
    const el = document.createElement("div");
    el.className = "card";
    el.innerHTML = `<h2>${title}</h2><div class="meta">/unicorn/${name} · ${f ? kb(f.size) : name === "firmware.bin" && done ? "last install " + kb(done.size) : "not on card"} · ${help}</div>
      <div class="row"><button>Upload ${name}</button><span class="msg"></span></div><input type="file" hidden>`;
    const input = el.querySelector("input"), msg = el.querySelector(".msg");
    el.querySelector("button").onclick = () => input.click();
    input.onchange = () => upload(name, input.files[0], msg);
    $("slots").appendChild(el);
  }
}

function upload(name, file, msg) {
  if (!file) return;
  msg.className = "msg";
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/upload?name=" + name);
  xhr.upload.onprogress = (e) => { msg.textContent = "Uploading " + Math.round(100 * e.loaded / e.total) + "%"; };
  xhr.onload = () => {
    const ok = xhr.status === 200 && xhr.responseText === "ok";
    msg.className = ok ? "msg" : "msg bad";
    msg.textContent = ok ? (name === "firmware.bin" ? "Saved. Restart to install." : "Saved.") : "Failed: " + xhr.responseText;
    if (ok) setTimeout(refresh, 600);
  };
  xhr.onerror = () => { msg.className = "msg bad"; msg.textContent = "Upload failed. Is the board still connected?"; };
  xhr.send(file);
}

$("restart").onclick = async () => {
  $("status").textContent = "Restarting… this page reloads in 10 seconds.";
  fetch("/restart", { method: "POST" }).catch(() => {});
  setTimeout(() => location.reload(), 10000);
};
refresh().catch(() => { $("status").textContent = "Could not reach the board."; });
</script>
</body>
</html>
)HTML";
