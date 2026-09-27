import assert from "node:assert/strict";
import test from "node:test";
import { deviceLayout, DEVICE_GEOMETRY } from "../docs/demo/device-geometry.js";

test("TC001 body dimensions and square 32×8 pitch remain proportional at every size", () => {
  for (const width of [280, 320, 640, 760, 1100, 1920]) {
    const l = deviceLayout(width);
    assert(Math.abs(l.width / l.height - 200.58 / 70.25) < 1e-10);
    assert.equal(l.matrixWidth / l.matrixHeight, 4);
    assert.equal(l.matrixWidth / 32, l.matrixHeight / 8);
    assert(Math.abs(l.matrixLeft * 2 + l.matrixWidth - width) < 1e-10);
    assert(Math.abs(l.matrixTop * 2 + l.matrixHeight - l.height) < 1e-10);
    assert(l.matrixTop > 0 && l.matrixLeft > 0);
  }
  assert.equal(DEVICE_GEOMETRY.columns * DEVICE_GEOMETRY.rows, 256);
  assert(DEVICE_GEOMETRY.pixelFillFraction <= 1);
});
