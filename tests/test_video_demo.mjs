import test from 'node:test';
import assert from 'node:assert/strict';
import { Simulation } from '../docs/demo/simulation.mjs';
function fixture() { let now = 100000; const sim = new Simulation(() => now); return { sim, advance(ms) { now += ms; return sim.snapshot(); } }; }
test('pomodoro uses elapsed time, pause/resume, and overshoot into break', () => {
  const { sim, advance } = fixture();
  sim.update('pomodoro', { durationMs: 10000, breakDurationMs: 5000 }); sim.timer('start');
  assert.equal(advance(3500).pomodoro.remainingMs, 6500);
  sim.timer('pause'); assert.equal(advance(100000).pomodoro.remainingMs, 6500);
  sim.timer('resume'); const p = advance(7000).pomodoro;
  assert.equal(p.phase, 'break'); assert.equal(p.remainingMs, 4500);
  assert.equal(advance(4500).pomodoro.phase, 'work');
  sim.timer('reset'); assert.deepEqual(sim.state.pomodoro, { durationMs: 10000, breakDurationMs: 5000, remainingMs: 10000, phase: 'work', running: false });
});
test('suspended tabs catch up through multiple work and break phases', () => {
  const { sim, advance } = fixture(); sim.update('pomodoro', { durationMs: 10000, breakDurationMs: 5000 }); sim.timer('start');
  const p = advance(1000004).pomodoro; assert.equal(p.phase, 'break'); assert.equal(p.remainingMs, 4996);
});
test('stopwatch pauses and resumes without including paused duration', () => {
  const { sim, advance } = fixture(); sim.stopwatch('start'); assert.equal(advance(1234).stopwatch.elapsedMs, 1234);
  sim.stopwatch('pause'); advance(4000); sim.stopwatch('resume'); assert.equal(advance(2345).stopwatch.elapsedMs, 3579);
  sim.stopwatch('reset'); assert.equal(sim.state.stopwatch.elapsedMs, 0); assert.equal(sim.state.stopwatch.running, false);
});
test('rotation excludes disabled modes and empty rotation is safe', () => {
  const { sim, advance } = fixture(); sim.update('rotation', { enabled: ['glucose', 'weather'], auto: true, intervalMs: 3000 });
  assert.equal(advance(3000).mode, 'weather'); assert.equal(advance(9000).mode, 'glucose');
  sim.update('rotation', { enabled: [] }); sim.next(); assert.equal(advance(30000).mode, 'glucose');
});
test('IP overlay freezes rotation, restores previous display, and buttons dismiss only', () => {
  const { sim, advance } = fixture(); sim.selectMode('pet'); sim.update('rotation', { auto: true }); sim.button('middle', 'double');
  assert.equal(advance(5999).mode, 'ip'); assert.equal(advance(1).mode, 'pet'); assert.equal(advance(4000).mode, 'glucose');
  sim.selectMode('pet'); sim.showIP(); sim.button('left'); assert.equal(sim.state.mode, 'pet');
});
test('presets share glucose state while retaining selected pet for restoration', () => {
  const { sim } = fixture(); sim.selectMode('pet'); sim.update('pet', { kind: 'axolotl' });
  sim.preset('high'); assert.equal(sim.state.glucose.value, 245); assert.equal(sim.state.glucose.trend, 'up');
  sim.preset('low'); assert.equal(sim.state.glucose.value, 58);
  sim.preset('range'); assert.equal(sim.state.glucose.status, 'range'); assert.equal(sim.state.mode, 'pet'); assert.equal(sim.state.pet.kind, 'axolotl');
});
test('event countdown clamps at zero and Libre label is temporary', () => {
  const { sim, advance } = fixture(); sim.demoEvent(15); assert.equal(advance(4250).event.remainingMs, 10750); assert.equal(advance(15000).event.remainingMs, 0);
  sim.update('glucose', { provider: 'libre' }); assert.equal(sim.state.providerLabel, true); assert.equal(advance(1300).providerLabel, false);
});
test('physical buttons follow context and snapshots cannot mutate model', () => {
  const { sim, advance } = fixture(); sim.selectMode('pomodoro'); sim.button('right'); assert.equal(advance(1000).pomodoro.running, true);
  sim.button('right', 'long'); assert.equal(sim.state.pomodoro.running, false);
  sim.selectMode('pet'); sim.update('pet', { nap: true }); sim.button('right'); assert.equal(sim.state.pet.nap, false);
  sim.button('middle'); assert.equal(sim.state.brightness, 200); sim.button('left', 'long'); assert.equal(sim.state.mode, 'glucose');
  const snapshot = sim.state; snapshot.glucose.value = 999; assert.equal(sim.state.glucose.value, 112);
});
