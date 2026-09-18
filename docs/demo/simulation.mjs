/** Deterministic, elapsed-time simulation. No network or patient data is used. */
export const MODES = [
  "glucose",
  "time",
  "weather",
  "pet",
  "pomodoro",
  "stopwatch",
  "event",
];
export class Simulation {
  constructor(now) {
    // Preserve epoch-compatible event dates without coupling durations to wall-clock corrections.
    const epochAnchor = Date.now();
    const monotonicAnchor = performance.now();
    this.now =
      now == null
        ? () => epochAnchor + performance.now() - monotonicAnchor
        : typeof now === "function"
          ? now
          : now.now;
    this.lastNow = this.now();
    this.lastRotation = this.lastNow;
    this.ipUntil = 0;
    this.providerUntil = 0;
    this.data = {
      mode: "glucose",
      modeStartedAt: this.lastNow,
      ipStartedAt: 0,
      glucose: {
        value: 112,
        trend: "flat",
        status: "range",
        age: 1,
        provider: "dexcom",
      },
      clock: { hour24: false },
      weather: { temperature: 72, unit: "F", condition: "sunny" },
      pet: { kind: "goldfish", nap: false, greetingUntil: 0 },
      pomodoro: {
        durationMs: 25 * 60000,
        breakDurationMs: 5 * 60000,
        remainingMs: 25 * 60000,
        phase: "work",
        running: false,
      },
      stopwatch: { elapsedMs: 0, running: false },
      event: {
        name: "Birthday",
        targetEpoch: this.lastNow + 7 * 86400000,
        remainingMs: 7 * 86400000,
      },
      ip: "192.168.1.42",
      providerLabel: false,
      rotation: {
        enabled: ["glucose", "time", "weather", "pet"],
        auto: false,
        intervalMs: 4000,
      },
      brightness: 100,
      alertsSnoozedUntil: 0,
    };
  }
  get state() {
    return this.snapshot();
  }
  tick(now = this.now()) {
    // Ignore a backwards wall-clock jump; duration accounting never runs backwards.
    now = Math.max(this.lastNow, now);
    const elapsed = now - this.lastNow;
    this.lastNow = now;
    const p = this.data.pomodoro;
    if (p.running) {
      let remaining = p.remainingMs - elapsed;
      if (remaining <= 0) {
        p.phase = p.phase === "work" ? "break" : "work";
        remaining += p.phase === "work" ? p.durationMs : p.breakDurationMs;
        // Skip complete work/break pairs after long suspended-tab intervals.
        if (remaining <= 0)
          remaining +=
            Math.floor(-remaining / (p.durationMs + p.breakDurationMs)) *
            (p.durationMs + p.breakDurationMs);
        while (remaining <= 0) {
          p.phase = p.phase === "work" ? "break" : "work";
          remaining += p.phase === "work" ? p.durationMs : p.breakDurationMs;
        }
      }
      p.remainingMs = remaining;
    }
    if (this.data.stopwatch.running) this.data.stopwatch.elapsedMs += elapsed;
    this.data.event.remainingMs = Math.max(
      0,
      this.data.event.targetEpoch - now,
    );
    if (this.ipUntil && now >= this.ipUntil) {
      this.ipUntil = 0;
      this.lastRotation = now;
    }
    const r = this.data.rotation;
    if (
      !this.ipUntil &&
      r.auto &&
      r.enabled.length &&
      now - this.lastRotation >= r.intervalMs
    ) {
      const steps = Math.floor((now - this.lastRotation) / r.intervalMs);
      this.advance(steps);
      this.lastRotation += steps * r.intervalMs;
    }
    return this;
  }
  snapshot(now = this.now()) {
    this.tick(now);
    const state = structuredClone(this.data);
    state.mode = this.ipUntil ? "ip" : state.mode;
    state.providerLabel = this.providerUntil > this.lastNow;
    return state;
  }
  update(section, patch) {
    this.tick();
    if (section === "ip") {
      this.data.ip = String(
        typeof patch === "string" ? patch : (patch.value ?? patch.ip),
      );
      return;
    }
    if (!this.data[section] || typeof this.data[section] !== "object") return;
    if (
      section === "glucose" &&
      patch.provider &&
      patch.provider !== this.data.glucose.provider
    )
      this.providerUntil =
        patch.provider.toLowerCase() === "libre" ? this.lastNow + 1300 : 0;
    Object.assign(this.data[section], patch);
    if (section === "pomodoro") {
      const p = this.data.pomodoro;
      p.durationMs = Math.max(1000, Number(p.durationMs) || 25 * 60000);
      p.breakDurationMs = Math.max(
        1000,
        Number(p.breakDurationMs) || 5 * 60000,
      );
      if ("durationMs" in patch || "breakDurationMs" in patch) {
        p.running = false;
        p.phase = "work";
        p.remainingMs = p.durationMs;
      }
    }
    if (section === "rotation") {
      const r = this.data.rotation;
      r.enabled = MODES.filter((mode) => r.enabled.includes(mode));
      r.intervalMs = Math.max(1000, Number(r.intervalMs) || 4000);
      this.lastRotation = this.lastNow;
    }
  }
  selectMode(mode) {
    this.tick();
    if (MODES.includes(mode)) {
      this.data.mode = mode;
      this.data.modeStartedAt = this.lastNow;
      this.ipUntil = 0;
      this.lastRotation = this.lastNow;
    }
  }
  advance(steps) {
    const modes = this.data.rotation.enabled;
    if (!modes.length) return;
    let index = modes.indexOf(this.data.mode);
    if (index < 0) index = steps > 0 ? -1 : 0;
    this.data.mode =
      modes[(((index + steps) % modes.length) + modes.length) % modes.length];
    this.data.modeStartedAt = this.lastNow;
  }
  next() {
    this.tick();
    this.ipUntil = 0;
    this.advance(1);
    this.lastRotation = this.lastNow;
  }
  preset(status) {
    const presets = {
      range: { value: 112, trend: "flat", status: "range" },
      high: { value: 245, trend: "up", status: "high" },
      low: { value: 58, trend: "down", status: "low" },
    };
    if (presets[status]) this.update("glucose", presets[status]);
  }
  timer(action) {
    this.tick();
    const p = this.data.pomodoro;
    if (action === "reset") {
      p.remainingMs = p.durationMs;
      p.phase = "work";
      p.running = false;
    } else if (action === "pause") p.running = false;
    else if (action === "toggle") p.running = !p.running;
    else if (action === "start" || action === "resume") p.running = true;
  }
  stopwatch(action) {
    this.tick();
    const s = this.data.stopwatch;
    if (action === "reset") {
      s.elapsedMs = 0;
      s.running = false;
    } else if (action === "pause") s.running = false;
    else if (action === "toggle") s.running = !s.running;
    else if (action === "start" || action === "resume") s.running = true;
  }
  setEvent(name, targetEpoch) {
    if (!Number.isFinite(targetEpoch)) return;
    this.tick();
    this.data.event = {
      name,
      targetEpoch,
      remainingMs: Math.max(0, targetEpoch - this.lastNow),
    };
  }
  demoEvent(seconds = 15) {
    this.tick();
    this.setEvent(
      this.data.event.name,
      this.lastNow + Math.max(0, seconds) * 1000,
    );
  }
  showIP() {
    this.tick();
    this.data.ipStartedAt = this.lastNow;
    this.ipUntil = this.lastNow + 6000;
  }
  button(name, kind = "short") {
    this.tick();
    if (this.ipUntil) {
      this.ipUntil = 0;
      this.lastRotation = this.lastNow;
      return;
    }
    if (name === "middle") {
      if (kind === "double") this.showIP();
      else if (kind === "long")
        this.data.alertsSnoozedUntil = this.lastNow + 30 * 60000;
      else {
        const levels = [10, 40, 100, 200];
        this.data.brightness =
          levels[(levels.indexOf(this.data.brightness) + 1) % levels.length];
      }
    } else if (name === "left") {
      if (kind === "long") this.selectMode("glucose");
      else this.next();
    } else if (name === "right") {
      if (this.data.mode === "pomodoro")
        this.timer(kind === "long" ? "reset" : "toggle");
      else if (this.data.mode === "stopwatch")
        this.stopwatch(kind === "long" ? "reset" : "toggle");
      else if (kind === "long") this.selectMode("glucose");
      else if (this.data.mode === "pet") {
        this.data.pet.nap = false;
        this.data.pet.greetingUntil = this.lastNow + 3000;
        this.lastRotation = this.lastNow;
      } else {
        this.advance(-1);
        this.lastRotation = this.lastNow;
      }
    }
  }
}
