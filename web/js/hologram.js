const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------- Hologram
let holo = { hologram: false, rotate: false, magic: true, half: false, lite: false, mirror: false, upside: false };

function showHolo() {
  $("holo").checked = holo.hologram;
  $("magic").checked = holo.magic;
  $("half").checked = holo.half;
  $("lite").checked = holo.lite;
  $("mirror").setAttribute("aria-pressed", holo.mirror);
  $("upside").setAttribute("aria-pressed", holo.upside);
  $("magic").disabled = !holo.hologram;
  const bits = [];
  if (holo.mirror) bits.push("mirrored");
  if (holo.upside) bits.push("upside down");
  $("holoMsg").textContent = bits.length ? bits.join(", ") + "." : "";
}

async function saveHolo(next) {
  const q = `hologram=${next.hologram ? 1 : 0}&rotate=${next.rotate ? 1 : 0}&magic=${next.magic ? 1 : 0}` +
    `&half=${next.half ? 1 : 0}&lite=${next.lite ? 1 : 0}&mirror=${next.mirror ? 1 : 0}&upside=${next.upside ? 1 : 0}`;
  let r = await fetch("/settings?" + q, { method: "POST" }).catch(() => null);
  if (!r || !r.ok) {
    // The reply can get lost on a busy board even when the change was applied,
    // so check what the board actually has before calling it a failure.
    await new Promise((done) => setTimeout(done, 400));
    const now = await fetch("/settings").then((x) => x.json()).catch(() => null);
    if (!now) { $("holoMsg").textContent = "The board didn't respond."; showHolo(); return; }
    const applied = ["hologram", "rotate", "magic", "half", "lite", "mirror", "upside"].every((k) => now[k] === next[k]);
    holo = { ...holo, ...now };
    showHolo();
    $("holoMsg").textContent = applied ? "Done." : "The board didn't take that change. Try again.";
    return;
  }
  holo = next;
  showHolo();
}
$("holo").onchange = () => saveHolo({ ...holo, hologram: $("holo").checked });
$("magic").onchange = () => saveHolo({ ...holo, magic: $("magic").checked });
$("half").onchange = () => saveHolo({ ...holo, half: $("half").checked });
$("lite").onchange = () => saveHolo({ ...holo, lite: $("lite").checked });
$("mirror").onclick = () => saveHolo({ ...holo, mirror: !holo.mirror });
$("upside").onclick = () => saveHolo({ ...holo, upside: !holo.upside });
$("rotate").onclick = () => saveHolo({ ...holo, mirror: !holo.mirror, upside: !holo.upside });
fetch("/settings").then((r) => r.json()).then((s) => { holo = { ...holo, ...s }; showHolo(); }).catch(() => {});

