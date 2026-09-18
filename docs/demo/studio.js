import { Simulation } from "./simulation.mjs?v=neon-1";
import "./virtual-sugar-clock.js?v=neon-1";
const $ = (id) => document.getElementById(id);
const sim = new Simulation();
sim.update("clock", { hour24: true });
sim.update("glucose", { age: 0 });
sim.update("rotation", { intervalMs: 3000 });
const device = document.querySelector("virtual-sugar-clock");
const labels = {
  glucose: "Blood glucose",
  time: "Clock / time",
  weather: "Weather",
  pet: "Pixel companion",
  pomodoro: "Pomodoro",
  stopwatch: "Stopwatch",
  event: "Event countdown",
  ip: "Connection address",
};
let selectedMode = "glucose";
function notice(message) {
  $("feedback").textContent = message;
}
function syncGlucose() {
  const s = sim.snapshot();
  for (const [id, key] of [
    ["glucose", "value"],
    ["trend", "trend"],
    ["status", "status"],
    ["provider", "provider"],
    ["age", "age"],
  ])
    $(id).value = s.glucose[key];
}
function bind(id, section, key, convert = (v) => v) {
  $(id).addEventListener("change", (e) => {
    const v = e.target.type === "checkbox" ? e.target.checked : e.target.value;
    const normalized = convert(v);
    sim.update(section, { [key]: normalized });
    if (e.target.type !== "checkbox") e.target.value = normalized;
  });
}
bind("provider", "glucose", "provider");
bind("glucose", "glucose", "value", (v) =>
  Math.max(20, Math.min(600, Number(v) || 112)),
);
bind("trend", "glucose", "trend");
bind("status", "glucose", "status");
bind("age", "glucose", "age", (v) => Math.max(0, Math.min(60, Number(v) || 0)));
bind("hour24", "clock", "hour24");
$("condition").addEventListener("change", () =>
  sim.update("weather", {
    condition: $("condition").value,
    animationStartedAt: sim.now(),
  }),
);
bind("pet", "pet", "kind");
bind("nap", "pet", "nap");
bind("auto", "rotation", "auto");
bind("interval", "rotation", "intervalMs", Number);
$("temperature").addEventListener("change", () => {
  const v = Number($("temperature").value);
  sim.update("weather", {
    temperature: $("unit").value === "C" ? (v * 9) / 5 + 32 : v,
  });
});
$("unit").addEventListener("change", () => {
  const s = sim.snapshot();
  sim.update("weather", { unit: $("unit").value });
  $("temperature").value = Math.round(
    $("unit").value === "C"
      ? ((s.weather.temperature - 32) * 5) / 9
      : s.weather.temperature,
  );
});
$("mode").addEventListener("change", () => sim.selectMode($("mode").value));
$("next").addEventListener("click", () => sim.next());
document.querySelectorAll("[data-preset]").forEach((b) =>
  b.addEventListener("click", () => {
    sim.preset(b.dataset.preset);
    syncGlucose();
    notice(
      b.dataset.preset === "range"
        ? "In range. Your companion is back."
        : "Glucose takes priority over the companion.",
    );
  }),
);
$("greet").addEventListener("click", () => {
  sim.button("right");
  notice("Hello, little companion.");
});
$("duration").addEventListener("change", () => {
  const durationMs = Number($("duration").value);
  sim.update("pomodoro", {
    durationMs,
    breakDurationMs: durationMs === 10000 ? 5000 : 300000,
  });
  sim.timer("reset");
});
document
  .querySelectorAll("[data-timer]")
  .forEach((b) =>
    b.addEventListener("click", () => sim.timer(b.dataset.timer)),
  );
document
  .querySelectorAll("[data-stopwatch]")
  .forEach((b) =>
    b.addEventListener("click", () => sim.stopwatch(b.dataset.stopwatch)),
  );
const tomorrow = new Date(Date.now() + 86400000);
tomorrow.setMinutes(tomorrow.getMinutes() - tomorrow.getTimezoneOffset());
$("event-date").value = tomorrow.toISOString().slice(0, 16);
sim.setEvent("Birthday", new Date($("event-date").value).getTime());
$("event-kind").addEventListener("change", () => {
  $("event-name").value = $("event-kind").value;
});
$("event-set").addEventListener("click", () => {
  const target = new Date($("event-date").value).getTime();
  if (!Number.isFinite(target)) {
    notice("Choose a valid event date and time.");
    return;
  }
  sim.setEvent($("event-name").value.trim() || "Event", target);
  notice("Event countdown updated.");
});
$("event-demo").addEventListener("click", () => {
  sim.setEvent($("event-name").value.trim() || "Event", Date.now() + 15000);
  notice("15-second countdown started.");
});
const groups = {
  glucose: ["glucose"],
  time: ["time"],
  weather: ["weather"],
  pet: ["pet"],
  extras: ["pomodoro", "stopwatch", "event"],
};
for (const [key, modes] of Object.entries(groups)) {
  $("rotation-options").insertAdjacentHTML(
    "beforeend",
    `<label class="check"><input type="checkbox" data-rotation="${key}" ${key === "extras" ? "" : "checked"}>${key === "extras" ? "Extra services" : labels[modes[0]]}</label>`,
  );
}
function rotation() {
  const enabled = [
    ...document.querySelectorAll("[data-rotation]:checked"),
  ].flatMap((el) => groups[el.dataset.rotation]);
  sim.update("rotation", { enabled });
  $("next").disabled = enabled.length === 0;
  notice(
    enabled.length
      ? "Rotation updated."
      : "No rotation displays selected. Choose a display above to use it manually.",
  );
}
document
  .querySelectorAll("[data-rotation]")
  .forEach((el) => el.addEventListener("change", rotation));
rotation();
notice("Choose a display, then Record pixels only for a clean capture.");
$("ip").addEventListener("change", () => {
  const value = $("ip").value.trim();
  if (
    !/^(\d{1,3}\.){3}\d{1,3}$/.test(value) ||
    value.split(".").some((v) => Number(v) > 255)
  ) {
    notice("Enter an IPv4 address, such as 192.168.1.42.");
    $("ip").setCustomValidity(
      "Enter four numbers from 0 to 255 separated by dots.",
    );
    $("ip").reportValidity();
    return;
  }
  $("ip").setCustomValidity("");
  sim.update("ip", value);
});
$("ip").addEventListener("input", () => $("ip").setCustomValidity(""));
device.addEventListener("clock-button", (e) => {
  sim.button(e.detail.name, e.detail.kind);
  notice(
    e.detail.name === "middle" && e.detail.kind === "double"
      ? "IP address shown for six seconds."
      : `${e.detail.name[0].toUpperCase() + e.detail.name.slice(1)} button · ${e.detail.kind} press`,
  );
});
function setAppearance(value) {
  const pixelsOnly = value !== "device";
  device.toggleAttribute("pixels-only", pixelsOnly);
  document.body.classList.toggle("pixels-only", pixelsOnly);
  $("appearance").value = pixelsOnly ? "pixels" : "device";
  try {
    localStorage.setItem("sugarclock-demo-appearance", $("appearance").value);
  } catch {}
}
let savedAppearance = "pixels";
try {
  savedAppearance =
    localStorage.getItem("sugarclock-demo-appearance") || "pixels";
} catch {}
setAppearance(savedAppearance);
$("appearance").addEventListener("change", () =>
  setAppearance($("appearance").value),
);
$("record-pixels").addEventListener("click", () => {
  setAppearance("pixels");
  if (!document.body.classList.contains("filming")) film();
});
device.addEventListener("dblclick", () => {
  if (
    document.body.classList.contains("filming") &&
    device.hasAttribute("pixels-only")
  )
    film();
});
function film() {
  const active = document.body.classList.toggle("filming");
  $("exit-film").hidden = !active;
  if (active && device.hasAttribute("pixels-only")) {
    device.tabIndex = -1;
    device.focus();
  } else (active ? $("exit-film") : $("film")).focus();
}
$("film").addEventListener("click", film);
$("exit-film").addEventListener("click", film);
document.addEventListener("keydown", (e) => {
  if (
    e.target.matches("input,select,textarea") ||
    e.ctrlKey ||
    e.metaKey ||
    e.altKey
  )
    return;
  if (
    e.key.toLowerCase() === "f" ||
    (e.key === "Escape" && document.body.classList.contains("filming"))
  )
    film();
});
function render() {
  const s = sim.snapshot();
  // Restart the weather animation when returning to the screen, as on firmware.
  if (s.mode === "weather")
    s.weather.animationStartedAt = Math.max(
      s.modeStartedAt,
      s.weather.animationStartedAt || 0,
    );
  device.state = s;
  if (s.mode !== "ip") selectedMode = s.mode;
  $("mode").value = selectedMode;
  $("nap").checked = s.pet.nap;
  $("mode-label").textContent =
    labels[s.mode] + (s.mode === "pomodoro" ? ` · ${s.pomodoro.phase}` : "");
  document
    .querySelectorAll("[data-panel]")
    .forEach(
      (el) => (el.hidden = !el.dataset.panel.split(" ").includes(selectedMode)),
    );
  $("pomodoro-info").textContent =
    `${s.pomodoro.phase === "work" ? "Work" : "Break"} · ${s.pomodoro.running ? "Running" : "Paused"}`;
  $("stopwatch-info").textContent =
    `${(s.stopwatch.elapsedMs / 1000).toFixed(2)} seconds · ${s.stopwatch.running ? "Running" : "Paused"}`;
  $("event-info").textContent =
    s.event.remainingMs > 0
      ? `${Math.ceil(s.event.remainingMs / 1000).toLocaleString()} seconds remaining`
      : "Your event is here!";
  requestAnimationFrame(render);
}
render();
