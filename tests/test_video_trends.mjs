import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { renderFrame } from '../docs/demo/pixel-renderer.js';

test('all seven demo arrows match the firmware bitmap and placement', () => {
  const header = readFileSync(new URL('../include/trend_arrows.h', import.meta.url), 'utf8');
  const trends = {
    'double-up': 'RISING_FAST', up: 'RISING', 'up-right': 'FORTY_FIVE_UP',
    flat: 'FLAT', 'down-right': 'FORTY_FIVE_DOWN', down: 'FALLING',
    'double-down': 'FALLING_FAST',
  };
  for (const [trend, bitmap] of Object.entries(trends)) {
    const body = header.match(new RegExp(`TREND_BITMAP_${bitmap}\\[7\\] = \\{([\\s\\S]*?)\\};`))[1];
    const rows = [...body.matchAll(/0b([01]{5})/g)].map(match => match[1]);
    assert.equal(rows.length, 7);
    const frame = renderFrame({mode: 'glucose', glucose: {value: 112, trend, status: 'range'}}, 0);
    for (let y = 0; y < 8; y++) for (let x = 23; x < 32; x++) {
      const expected = y < 7 && x < 28 && rows[y][x - 23] === '1';
      assert.equal(Boolean(frame[y * 32 + x]), expected, `${trend} at ${x},${y}`);
    }
  }
});
