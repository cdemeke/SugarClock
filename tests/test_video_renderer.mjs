/** Run with node --test tests/test_video_renderer.mjs (no npm dependencies). */
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
const source = await readFile(new URL('../docs/demo/pixel-renderer.js', import.meta.url), 'utf8');
const { renderFrame, describeFrame, formatDuration } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const base = {
  glucose: { value: 112, status: 'range', trend: 'flat', provider: 'dexcom' },
  clock: { hour24: true },
  weather: { temperature: 72, unit: 'F', condition: 'sunny' },
  pet: { kind: 'goldfish', nap: false },
  pomodoro: { remainingMs: 25000, phase: 'work' },
  stopwatch: { elapsedMs: 16000 },
  event: { remainingMs: 15000, name: 'Birthday' },
  ip: '192.168.1.42',
  modeStartedAt: 0,
  ipStartedAt: 0,
};

test('every mode renders a populated 32×8 matrix of valid LED colors', () => {
  for (const mode of ['glucose', 'time', 'weather', 'pet', 'pomodoro', 'stopwatch', 'event', 'ip']) {
    const state = { ...base, mode };
    const pixels = renderFrame(state, 0);
    assert.equal(pixels.length, 256, mode);
    assert(pixels.some(Boolean), `${mode} must light pixels`);
    assert(pixels.every(color => color === null || /^#[a-f0-9]{6}$/i.test(color)), mode);
    assert.equal(typeof describeFrame(state, 0), 'string');
    assert(!describeFrame(state, 0).includes('undefined'), mode);
  }
});

for (const kind of ['goldfish', 'ghost', 'axolotl', 'dinosaur', 'turtle', 'octopus', 'red-panda']) {
  test(`${kind} animates, naps and yields to actual glucose for either alert`, () => {
    const state = { ...base, mode: 'pet', pet: { kind } };
    const awake = renderFrame(state, 0);
    assert.notDeepEqual(awake, renderFrame(state, 1950), 'pet animation');
    assert.notDeepEqual(awake, renderFrame({ ...state, pet: { kind, nap: true } }, 0), 'nap pose');
    for (const [status, value, trend] of [['high', 245, 'up'], ['low', 58, 'down']]) {
      const alert = { ...state, glucose: { ...base.glucose, status, value, trend } };
      assert.deepEqual(renderFrame(alert, 0), renderFrame({ ...alert, mode: 'glucose' }, 0), status);
      assert.match(describeFrame(alert, 0), new RegExp(`Pet hidden:.*${value}`));
    }
    assert.deepEqual(renderFrame(state, 0), awake, 'restoring range restores the pet');
  });
}

for (const condition of ['rain', 'snow']) {
  test(`${condition} particles move while the temperature stays stable`, () => {
    const state = { ...base, mode: 'weather', weather: { ...base.weather, condition } };
    const a = renderFrame(state, 0);
    const b = renderFrame(state, 450);
    assert.notDeepEqual(a, b);
    for (let y = 0; y < 8; y++) assert.deepEqual(a.slice(y * 32 + 9, y * 32 + 32), b.slice(y * 32 + 9, y * 32 + 32));
  });
}

test('IP scrolls in firmware white and supersedes transient provider identification', () => {
  const state = { ...base, mode: 'ip' };
  const first = renderFrame(state, 0);
  assert.notDeepEqual(first, renderFrame(state, 2000));
  assert.deepEqual([...new Set(first.filter(Boolean))], ['#d1fff1']);
  assert.deepEqual(renderFrame({ ...state, providerLabel: true }, 0), first);
  assert.equal(describeFrame({ ...state, providerLabel: true }, 0), 'IP address 192.168.1.42');
});

test('timers round countdowns up, elapsed time down, and never show negative values', () => {
  assert.equal(formatDuration(9999, true), '00:10');
  assert.equal(formatDuration(9999), '00:09');
  assert.equal(formatDuration(-1), '00:00');
  assert.equal(formatDuration(1500000), '25:00');
  assert.equal(formatDuration(0, true), '00:00');
});

test('provider identification and work/break phases remain visually distinct', () => {
  const state = { ...base, mode: 'glucose', providerLabel: true };
  assert.notDeepEqual(renderFrame(state, 0), renderFrame({ ...state, glucose: { ...state.glucose, provider: 'libre' } }, 0));
  const timer = { ...base, mode: 'pomodoro' };
  assert.notDeepEqual(renderFrame(timer, 0), renderFrame({ ...timer, pomodoro: { ...timer.pomodoro, phase: 'break' } }, 0));
});
