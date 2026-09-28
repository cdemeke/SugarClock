// Renders showreel.html frame by frame to PNGs with headless Chromium.
// Usage: node render.mjs <outDir> [fps=30] [onlyTimes=comma,separated,seconds]
// Serve docs/ over HTTP first: python3 -m http.server 8765 --directory docs
import { chromium } from "playwright";
import { mkdirSync } from "node:fs";

const [outDir = "frames", fpsArg = "30", only] = process.argv.slice(2);
const fps = Number(fpsArg);
mkdirSync(outDir, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
page.on("pageerror", (e) => console.error("page error:", e.message));
await page.goto("http://127.0.0.1:8765/showreel/showreel.html?record", { waitUntil: "load" });
await page.evaluate(() => window.ready);
const canvas = await page.$("canvas");

const times = only
  ? only.split(",").map(Number)
  : Array.from({ length: Math.round((await page.evaluate(() => window.DURATION)) * fps) }, (_, i) => i / fps);

for (const [i, t] of times.entries()) {
  await page.evaluate((tt) => window.renderAt(tt), t);
  const name = only ? `still_${String(t).replace(".", "_")}.png` : `f_${String(i).padStart(4, "0")}.png`;
  await canvas.screenshot({ path: `${outDir}/${name}` });
  if (!only && i % 60 === 0) console.log(`frame ${i}/${times.length}`);
}
await browser.close();
