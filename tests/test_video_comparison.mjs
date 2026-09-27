import assert from "node:assert/strict";
import test from "node:test";
import {
  COMPARISON_OPTIONS,
  comparisonState,
} from "../docs/demo/comparison.js";
import { Simulation } from "../docs/demo/simulation.mjs";
import { renderFrame } from "../docs/demo/pixel-renderer.js";
test("comparison includes all seven companions and all ten weather visuals", () => {
  assert.equal(COMPARISON_OPTIONS.pet.length, 7);
  assert.equal(COMPARISON_OPTIONS.weather.length, 10);
  for (const options of Object.values(COMPARISON_OPTIONS))
    assert.equal(new Set(options.map(([key]) => key)).size, options.length);
});
test("comparison shares live settings without overwriting selected companion or weather", () => {
  const sim = new Simulation(() => 1000);
  for (const mode of ["pet", "weather"]) {
    sim.selectMode(mode);
    const state = sim.snapshot();
    const before = structuredClone(state);
    for (const [key] of COMPARISON_OPTIONS[mode]) {
      const preview = comparisonState(state, key);
      assert.equal(preview[mode][mode === "pet" ? "kind" : "condition"], key);
      assert.equal(preview.glucose, state.glucose);
      assert.equal(preview.brightness, state.brightness);
      assert.equal(renderFrame(preview, 1000).length, 256);
    }
    assert.deepEqual(state, before);
  }
});
test("high/low takeover applies to every companion in comparison view", () => {
  const sim = new Simulation(() => 1000);
  sim.selectMode("pet");
  for (const status of ["high", "low"]) {
    sim.preset(status);
    const state = sim.snapshot();
    const expected = renderFrame({ ...state, mode: "glucose" }, 1000);
    for (const [key] of COMPARISON_OPTIONS.pet)
      assert.deepEqual(
        renderFrame(comparisonState(state, key), 1000),
        expected,
      );
  }
});
