import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { renderCompanionFrame } from "../docs/demo/companion-renderer.js";

const productionSource = readFileSync(new URL("../data/www/companions.js", import.meta.url), "utf8");
const production = {};
vm.runInNewContext(productionSource, production);
const kinds = ["goldfish", "ghost", "axolotl", "dinosaur", "turtle", "octopus", "red-panda"];

test("demo companion source is identical to production dashboard", () => {
  assert.equal(readFileSync(new URL("../docs/demo/companions.js", import.meta.url), "utf8"), productionSource);
});

for (const [id, kind] of kinds.entries()) {
  test(`${kind} matches production idle, nap, greeting, and greeting expiry frames`, () => {
    for (const now of [0, 219, 220, 649, 650, 1399, 1400, 1599, 1600, 3500, 4294968000]) {
      for (const nap of [false, true]) {
        for (const greetingUntil of [0, now, now + 1]) {
          const actual = renderCompanionFrame({ pet: { kind, nap, greetingUntil } }, now);
          const { frame, resolveColor } = production.PixelCompanions;
          const expected = Array.from(frame(id, now, nap, now < greetingUntil, 0, 0).flat(), key =>
            key === "." ? null : resolveColor(key));
          assert.equal(actual.length, 256);
          assert.deepEqual(actual, expected, `${kind}: t=${now}, nap=${nap}, greetingUntil=${greetingUntil}`);
        }
      }
    }
  });
}

test("unknown or missing pet defaults to production goldfish", () => {
  assert.deepEqual(renderCompanionFrame({}, 650), renderCompanionFrame({ pet: { kind: "goldfish" } }, 650));
  assert.deepEqual(renderCompanionFrame({ pet: { kind: "unknown" } }, 650), renderCompanionFrame({}, 650));
});
