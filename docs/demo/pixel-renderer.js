/** Virtual 32×8 LED renderer. Pet silhouettes and palette adapted from include/companion.h. */
export const MATRIX_WIDTH = 32;
export const MATRIX_HEIGHT = 8;
const C = {
  white: "#d1fff1",
  green: "#94e6a1",
  red: "#f06460",
  blue: "#74bfdc",
  gold: "#ffd47c",
  purple: "#c997ff",
};
const PALETTE = {
  W: C.white,
  M: "#86dfd1",
  S: "#4b9d9d",
  E: "#0b1725",
  O: "#ff9d48",
  Y: C.gold,
  R: "#e57439",
  P: "#ffc8d9",
  G: "#ec779e",
  L: C.green,
  D: "#50ad7c",
  H: "#ff83ac",
  C: "#fff1d7",
  V: C.purple,
  T: "#8f5c47",
  B: C.blue,
};
// Classic 5×7 column bitmaps: same fixed six-pixel advance as Adafruit GFX.
const FONT = {
  0: [62, 81, 73, 69, 62],
  1: [0, 66, 127, 64, 0],
  2: [66, 97, 81, 73, 70],
  3: [33, 65, 69, 75, 49],
  4: [24, 20, 18, 127, 16],
  5: [39, 69, 69, 69, 57],
  6: [60, 74, 73, 73, 48],
  7: [1, 113, 9, 5, 3],
  8: [54, 73, 73, 73, 54],
  9: [6, 73, 73, 41, 30],
  A: [126, 17, 17, 17, 126],
  B: [127, 73, 73, 73, 54],
  C: [62, 65, 65, 65, 34],
  D: [127, 65, 65, 34, 28],
  E: [127, 73, 73, 73, 65],
  F: [127, 9, 9, 9, 1],
  G: [62, 65, 73, 73, 122],
  H: [127, 8, 8, 8, 127],
  I: [0, 65, 127, 65, 0],
  J: [32, 64, 65, 63, 1],
  K: [127, 8, 20, 34, 65],
  L: [127, 64, 64, 64, 64],
  M: [127, 2, 12, 2, 127],
  N: [127, 4, 8, 16, 127],
  O: [62, 65, 65, 65, 62],
  P: [127, 9, 9, 9, 6],
  Q: [62, 65, 81, 33, 94],
  R: [127, 9, 25, 41, 70],
  S: [70, 73, 73, 73, 49],
  T: [1, 1, 127, 1, 1],
  U: [63, 64, 64, 64, 63],
  V: [31, 32, 64, 32, 31],
  W: [63, 64, 56, 64, 63],
  X: [99, 20, 8, 20, 99],
  Y: [7, 8, 112, 8, 7],
  Z: [97, 81, 73, 69, 67],
  ":": [0, 54, 54, 0, 0],
  ".": [0, 96, 96, 0, 0],
  "-": [8, 8, 8, 8, 8],
  " ": [0, 0, 0, 0, 0],
  "°": [6, 9, 9, 6, 0],
  "!": [0, 0, 95, 0, 0],
};
const TINY = {
  O: ["111", "101", "101", "101", "111"],
  K: ["101", "101", "110", "101", "101"],
  Z: ["111", "001", "010", "100", "111"],
  H: ["101", "101", "111", "101", "101"],
  I: ["111", "010", "010", "010", "111"],
};
const PETS = {
  goldfish: [
    ".....YY......",
    "....OOOOO..Y.",
    "..YOOOOOOOYY.",
    ".OOOEOOOOOYYY",
    "..OOOOOOOOYY.",
    "...RRRORR..Y.",
    "......Y......",
  ],
  ghost: [
    "...WWW...",
    "..WWWWW..",
    ".WWWWWWW.",
    ".WEWWEWW.",
    ".WWWWWWW.",
    ".MWWEWWM.",
    ".MM.M.MM.",
  ],
  axolotl: [
    ".G.......G.",
    "..GPPPPPG..",
    "G.PPPPPPP.G",
    ".GPEPPPEPG.",
    "G.PGPEPGP.G",
    "...PPPPP...",
    "..P.....P..",
  ],
  dinosaur: [
    "............",
    "......LLLLL.",
    ".....LLLELLL",
    ".....LLLLLLL",
    "L...DLLLL...",
    "LL.DLLLLLL..",
    ".LLLLLYL....",
    "...LL.LL....",
  ],
  turtle: [
    "..............",
    "....LLLL......",
    "...LDLLDL.....",
    "..LDDLDDDL.LL.",
    "..LDLLLDLL.LEL",
    ".LLLLLLLLLLLL.",
    "...L....L.....",
    "..LL...LL.....",
  ],
  octopus: [
    "....VVVV......",
    "...VVVVVV.....",
    "..VVVVVVVV....",
    "..VCEVVCEV....",
    "..VVVVVVVV....",
    "...VVVVVV.....",
    "..VV.VV.VV....",
    ".VV..VV..VV...",
  ],
  "red-panda": [
    ".O...O........",
    ".OCCCO........",
    "OOOOOOO.......",
    "OCECECO....OO.",
    "OCCYCCO...OYY.",
    ".OOOOO...YYOO.",
    ".TTTTT.OOYY...",
    ".TT.TT.OO.....",
  ],
};
const ARROWS = {
  flat: [".....", "..X..", "...X.", "XXXXX", "...X.", "..X..", "....."],
  up: ["..X..", ".XXX.", "X.X.X", "..X..", "..X..", "..X..", "..X.."],
  down: ["..X..", "..X..", "..X..", "..X..", "X.X.X", ".XXX.", "..X.."],
  "up-right": ["XXXXX", "...XX", "..X.X", ".X..X", "X...X", ".....", "....."],
  "down-right": [".....", ".....", "X...X", ".X..X", "..X.X", "...XX", "XXXXX"],
  "double-up": [".X.X.", "XXXXX", ".X.X.", ".X.X.", ".X.X.", ".X.X.", ".X.X."],
  "double-down": [
    ".X.X.",
    ".X.X.",
    ".X.X.",
    ".X.X.",
    ".X.X.",
    "XXXXX",
    ".X.X.",
  ],
};
const clampNumber = (value, fallback = 0) =>
  Number.isFinite(Number(value)) ? Number(value) : fallback;
export function formatDuration(ms, roundUp = false) {
  const seconds = Math.max(
    0,
    (roundUp ? Math.ceil : Math.floor)(clampNumber(ms) / 1000),
  );
  return `${String(Math.floor(seconds / 60)).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
}
function clockText(state, now) {
  const date = new Date(now);
  const hours = state.clock?.hour24
    ? date.getHours()
    : date.getHours() % 12 || 12;
  return `${String(hours).padStart(2, "0")}:${String(date.getMinutes()).padStart(2, "0")}`;
}
function eventText(event) {
  const ms = Math.max(0, clampNumber(event?.remainingMs));
  if (ms >= 86400000) return `${Math.ceil(ms / 86400000)}D`;
  if (ms >= 3600000)
    return `${Math.floor(ms / 3600000)}H ${Math.floor((ms % 3600000) / 60000)}M`;
  return formatDuration(ms, true);
}

/** Returns a fresh row-major array of exactly 256 CSS colors/null (unlit). */
export function renderFrame(state, now = Date.now()) {
  const frame = Array(256).fill(null);
  const put = (x, y, color) => {
    if (x >= 0 && x < 32 && y >= 0 && y < 8) frame[y * 32 + x] = color;
  };
  const text = (value, x, y, color) => {
    [...String(value).toUpperCase()].forEach((char, index) =>
      (FONT[char] || FONT[" "]).forEach((bits, cx) => {
        for (let cy = 0; cy < 7; cy++)
          if (bits & (1 << cy)) put(x + index * 6 + cx, y + cy, color);
      }),
    );
  };
  const center = (value, color = C.white) =>
    text(
      value,
      Math.floor((32 - (String(value).length * 6 - 1)) / 2),
      0,
      color,
    );
  const scroll = (value, color = C.white, startedAt = state.modeStartedAt) => {
    const width = String(value).length * 6 - 1;
    if (width <= 32) return center(value, color);
    const elapsed = Math.max(
      0,
      now - clampNumber(startedAt, now - (now % 10000)),
    );
    // Hold the leading text for readability, then scroll continuously with a small gap.
    const shift = Math.floor(Math.max(0, elapsed - 700) / 90) % (width + 12);
    text(value, -shift, 0, color);
    text(value, width + 12 - shift, 0, color);
  };
  const sprite = (rows, x, y, color) =>
    rows.forEach((row, dy) =>
      [...row].forEach((key, dx) => {
        if (key !== "." && key !== "0")
          put(x + dx, y + dy, color || PALETTE[key]);
      }),
    );
  const tiny = (value, x, y, color) =>
    [...value].forEach((char, i) =>
      (TINY[char] || []).forEach((row, dy) =>
        [...row].forEach((bit, dx) => {
          if (bit === "1") put(x + i * 4 + dx, y + dy, color);
        }),
      ),
    );
  const glucose = () => {
    const g = state.glucose || {};
    const color = g.status === "range" ? C.green : C.red;
    const value = String(Math.round(clampNumber(g.value, 112)));
    const x = Math.floor((32 - value.length * 6 - 6) / 2);
    text(value, x, 0, color);
    sprite(ARROWS[g.trend] || ARROWS.flat, x + value.length * 6 + 1, 0, color);
  };
  if (state.providerLabel && state.mode !== "ip") {
    center(state.glucose?.provider === "libre" ? "LIBRE" : "DEX", C.blue);
    return frame;
  }
  switch (state.mode) {
    case "glucose":
      glucose();
      break;
    case "time": {
      const value = clockText(state, now);
      center(Math.floor(now / 500) % 2 ? value : value.replace(":", " "));
      break;
    }
    case "weather": {
      const w = state.weather || {};
      const c = w.condition || "sunny";
      if (c === "sunny") {
        sprite(
          [
            "...Y...",
            ".Y...Y.",
            "..YYY..",
            "Y.YYY.Y",
            "..YYY..",
            ".Y...Y.",
            "...Y...",
          ],
          0,
          0,
        );
        if (Math.floor(now / 450) % 2) {
          put(0, 0, C.gold);
          put(6, 6, C.gold);
        }
      } else {
        sprite(
          ["..WWW...", ".WWWWW..", "WWWWWWWW", "WWWWWWWW"],
          0,
          c === "cloudy" ? 2 : 0,
        );
        if (c === "rain" || c === "snow")
          for (let i = 0; i < 4; i++) {
            const y =
              4 + ((Math.floor(now / (c === "rain" ? 130 : 320)) + i * 3) % 4);
            const x =
              (i * 2 + (c === "snow" ? Math.floor(now / 850) % 2 : 0)) % 8;
            put(x, y, c === "rain" ? C.blue : C.white);
            if (c === "rain" && y < 7) put(x, y + 1, "#397dba");
          }
      }
      const temperature = Math.round(
        w.unit === "C"
          ? ((clampNumber(w.temperature, 72) - 32) * 5) / 9
          : clampNumber(w.temperature, 72),
      );
      const value = `${temperature}${w.unit === "C" ? "C" : "F"}`;
      if (value.length <= 4)
        text(value, 32 - (value.length * 6 - 1), 0, C.white);
      else scroll(value, C.white);
      break;
    }
    case "pet": {
      if (state.glucose?.status !== "range") {
        glucose();
        break;
      }
      const p = state.pet || {};
      const happy = now < clampNumber(p.greetingUntil);
      const nap = p.nap && !happy;
      const kind = PETS[p.kind] ? p.kind : "goldfish";
      const rows = PETS[kind].map((row) => [...row]);
      const phase = nap ? 0 : Math.floor(now / (happy ? 180 : 650)) % 2;
      const set = (x, y, c) => {
        if (rows[y] && x >= 0 && x < rows[y].length) rows[y][x] = c;
      };
      if (kind === "goldfish" && phase) {
        set(12, 3, ".");
        set(11, 0, "Y");
        set(11, 6, "Y");
      }
      if (kind === "ghost")
        for (let x = 1; x < 8; x++)
          set(x, 6, (x + phase) % 3 === 0 ? "." : "M");
      if (kind === "axolotl" && phase) {
        set(1, 0, ".");
        set(9, 0, ".");
        set(0, 1, "G");
        set(10, 1, "G");
      }
      if (kind === "dinosaur" && phase) {
        set(3, 7, ".");
        set(7, 7, ".");
        set(5, 7, "D");
        set(8, 7, "D");
      }
      if (kind === "turtle" && phase) {
        set(2, 7, ".");
        set(7, 7, ".");
        set(4, 7, "L");
        set(9, 7, "L");
      }
      if (kind === "octopus" && phase) {
        set(1, 7, ".");
        set(10, 7, ".");
        set(3, 7, "V");
        set(8, 7, "V");
      }
      if (kind === "red-panda" && phase) {
        set(7, 7, ".");
        set(8, 7, ".");
        set(9, 7, "O");
        set(10, 6, "Y");
      }
      if (nap) {
        const eyes = {
          goldfish: [[4, 3, "O"]],
          ghost: [
            [2, 3, "W"],
            [5, 3, "W"],
          ],
          axolotl: [
            [3, 3, "P"],
            [7, 3, "P"],
          ],
          dinosaur: [[8, 2, "L"]],
          turtle: [[12, 4, "L"]],
          octopus: [
            [4, 3, "V"],
            [8, 3, "V"],
          ],
          "red-panda": [
            [2, 3, "C"],
            [4, 3, "C"],
          ],
        };
        eyes[kind].forEach(([x, y, color]) => {
          set(x, y, color);
          set(x, y + 1, "E");
        });
      }
      const width = rows[0].length;
      const x =
        Math.floor((16 - width) / 2) +
        (nap ? 0 : Math.floor(now / (happy ? 300 : 1600)) % 2);
      const y = rows.length < 8 ? (nap ? 1 : Math.floor(now / 1600) % 2) : 0;
      sprite(
        rows.map((row) => row.join("")),
        x,
        y,
      );
      tiny(nap ? "ZZ" : happy ? "HI" : "OK", 21, 2, nap ? C.blue : C.green);
      if (nap && Math.floor(now / 800) % 2) put(30, 0, C.blue);
      break;
    }
    case "pomodoro": {
      const p = state.pomodoro || {};
      const duration = p.phase === "break" ? p.breakDurationMs : p.durationMs;
      const color = p.phase === "break" ? C.green : C.gold;
      if (p.running && duration - p.remainingMs < 900)
        center(p.phase === "break" ? "BREAK" : "WORK", color);
      else scroll(formatDuration(p.remainingMs, true), color);
      break;
    }
    case "stopwatch":
      scroll(formatDuration(state.stopwatch?.elapsedMs), C.blue);
      break;
    case "event": {
      const event = state.event || {};
      const elapsed = Math.max(0, now - clampNumber(state.modeStartedAt, 0));
      const namePhase =
        Math.floor(elapsed / 4500) % 3 === 0 && event.remainingMs > 15000;
      scroll(namePhase ? event.name || "EVENT" : eventText(event), C.purple);
      break;
    }
    case "ip":
      scroll(
        state.ip || "192.168.1.42",
        C.white,
        state.ipStartedAt ?? state.modeStartedAt,
      );
      break;
    default:
      center("SUGAR", C.green);
  }
  return frame;
}

export function describeFrame(state, now = Date.now()) {
  const g = state.glucose || {};
  const glucose = `${g.provider === "libre" ? "FreeStyle Libre" : "Dexcom"} glucose ${g.value} mg/dL, ${g.status === "range" ? "in range" : g.status}, trend ${g.trend}${g.age ? `, reading ${g.age} minutes old` : ""}`;
  if (state.providerLabel && state.mode !== "ip")
    return `${g.provider === "libre" ? "FreeStyle Libre" : "Dexcom"} connected`;
  switch (state.mode) {
    case "glucose":
      return glucose;
    case "time":
      return `Time ${clockText(state, now)}, ${state.clock?.hour24 ? "24" : "12"} hour clock`;
    case "weather":
      return `${state.weather?.condition}, ${Math.round(state.weather?.unit === "C" ? ((state.weather.temperature - 32) * 5) / 9 : state.weather?.temperature)} degrees ${state.weather?.unit === "C" ? "Celsius" : "Fahrenheit"}`;
    case "pet":
      return g.status !== "range"
        ? `Pet hidden: ${glucose}`
        : `${state.pet?.kind} ${state.pet?.nap ? "napping" : "pet, glucose OK"}`;
    case "pomodoro":
      return `${state.pomodoro?.phase === "break" ? "Break" : "Work"} timer ${formatDuration(state.pomodoro?.remainingMs, true)} remaining`;
    case "stopwatch":
      return `Stopwatch ${formatDuration(state.stopwatch?.elapsedMs)}`;
    case "event":
      return `${state.event?.name || "Event"}: ${eventText(state.event)} remaining`;
    case "ip":
      return `IP address ${state.ip}`;
    default:
      return "SugarClock display";
  }
}
