// Drives Abyssal Garden as a wallpaper: never needs a click, a key or the pointer from the user.
const opt = Object.fromEntries(new URLSearchParams(location.hash.slice(1)));
const DETAIL = opt.detail ?? "2";            // the page's own setting: 2 Low, 1 Medium, 0 High
const DWELL = (+opt.dwell || 40) * 1000;     // ms on each view
const GLIDE = +opt.glide || 2500;            // ms of a gentle swim forwards after each jump
const f = document.getElementById("f"), veil = document.getElementById("veil");
const sleep = ms => new Promise(r => setTimeout(r, ms));

try { localStorage.abyssal = DETAIL; } catch {}   // read by the page when it starts (same file:// storage)
f.src = "../vendor/abyssal-garden/index.html";

// synthetic keys go to the frame's window, where the page listens; the user's keys never come here
// (the wallpaper window never has the keyboard)
const key = (code, down) => f.contentWindow.dispatchEvent(new f.contentWindow.KeyboardEvent(down ? "keydown" : "keyup", { code }));
const tap = code => { key(code, true); key(code, false); };
// Paused while the wallpaper cannot be seen: the minimo-abyssal plugin writes 0/1 to this file
// (Chrome itself keeps drawing a "suspended" window). The page's frames are held meanwhile.
const STATE = opt.state;
let paused = false;
let held = [], raf = null;
function holdFrames(w) {
  raf = w.requestAnimationFrame.bind(w);
  held = [];
  // the page asks for each frame through the global, so this is seen from its next frame on
  w.requestAnimationFrame = cb => paused ? (held.push(cb), 0) : raf(cb);
}
if (STATE)
  setInterval(() => {
    const x = new XMLHttpRequest();
    x.onloadend = () => {
      const now = x.responseText.trim() === "0";    // no file (no plugin): draw
      if (paused && !now) held.splice(0).forEach(cb => raf(cb));
      paused = now;
    };
    x.open("GET", "file://" + STATE); x.send();
  }, 700);
const fade = async on => { veil.style.opacity = on ? "1" : "0"; await sleep(1300); };

// Nothing of the page's own interface is shown: the menu button (#o), the stats (#i), the links (#f),
// the menu itself once dived in, and the canvas' crosshair cursor. Set again at every load of the
// frame (the page reloads itself after a detail change or a failed start).
const HIDE = "#o,#i,#f{display:none!important} .g #m{display:none!important} *{cursor:none!important}";
let dived = null;   // resolved at the first dive
f.addEventListener("load", async () => {
  const d = f.contentDocument;
  d.head.appendChild(Object.assign(d.createElement("style"), { textContent: HIDE }));
  holdFrames(f.contentWindow);
  // the menu is up (class "m") once the world is built; then "Dive in"
  while (!/\bm\b/.test(d.body.className)) {
    if (/\be\b/.test(d.body.className)) return;        // the page failed (no WebGPU): the veil stays
    await sleep(250);
  }
  d.getElementById("d").click();
  await sleep(300);
  dived?.();
});

async function main() {
  await new Promise(r => (dived = r));
  await fade(false);
  for (let view = 1; ; view = view % 6 + 1) {
    key("KeyW", true); await sleep(GLIDE); key("KeyW", false);
    await sleep(DWELL - GLIDE);
    await fade(true);
    tap("Digit" + (view % 6 + 1));
    await sleep(400);
    await fade(false);
  }
}
main();
