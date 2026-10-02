/* Exercise rendering with API-shaped data, without a browser dependency. */
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

class Element {
  constructor(tag, text = '') {
    this.tag = tag;
    this.text = String(text ?? '');
    this.children = [];
    this.attributes = {};
    this.style = {};
    this.listeners = {};
    this.classList = {add() {}, remove() {}};
  }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.text = ''; this.children = children; }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  addEventListener(event, callback) { this.listeners[event] = callback; }
  set textContent(value) { this.text = String(value); this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(''); }
  all(tag) { return this.children.flatMap(child => [...(child.tag === tag ? [child] : []), ...child.all(tag)]); }
}
const settle = () => new Promise(resolve => setImmediate(resolve));
const source = fs.readFileSync('fleet/sugarfleet/static/overview.js', 'utf8');
function payload(overrides = {}) {
  return {
    counts: {total: 9, online: 7, active_7d: 7, active_30d: 7, feature_reporting: 2, blocked: 0, retired: 0, legacy: 5},
    capacity: {ceiling: 10000, warning: false}, feature_denominator: 7,
    feature_counts: {weather_enabled: {enabled: 1, known: 2, unknown: 5}, auto_cycle_enabled: {enabled: 2, known: 2, unknown: 5}},
    source_counts: {dexcom: 2, unknown: 5}, version_counts: {'0.2.6': 5, '0.2.11': 2},
    location_counts: {'Washington, US': 5, Unknown: 2}, daily: [], ...overrides,
  };
}
async function boot(initial) {
  const nodes = new Map();
  const node = id => {if (!nodes.has(id)) nodes.set(id, new Element('div')); return nodes.get(id);};
  let response = initial;
  const calls = [];
  const fleet = {
    element(tag, text, className) {const el = new Element(tag, text); el.className = className; return el;},
    label(value) {return value;},
    async request(...args) {calls.push(args); if (response instanceof Error) throw response; return typeof response === 'function' ? response(...args) : response;},
    async action(button, output, callback) {button.disabled = true; try {await callback();} finally {button.disabled = false;}},
  };
  vm.runInNewContext(source, {fleet, document: {getElementById: node, createElementNS: (_, tag) => new Element(tag), createTextNode: text => new Element('#text', text)}, Date, Number, Math});
  await settle();
  return {node, calls, respond(value) {response = value;}, async refresh() {await node('refresh').listeners.click();}, async cleanup() {await node('cleanup').listeners.click();}};
}

test('features keep unknown reports distinct and use all active clocks as the denominator', async () => {
  const app = await boot(payload());
  const rows = app.node('feature-counts').all('tbody')[0].all('tr');
  assert.deepEqual(rows[0].children.map(cell => cell.textContent), ['Auto cycle', '2', '5', '29%']);
  assert.deepEqual(rows[1].children.map(cell => cell.textContent), ['Weather', '1', '5', '14%']);
  const fill = rows[0].all('span').find(el => el.className?.includes('bar-fill'));
  assert.ok(Math.abs(parseFloat(fill.style.width) - 200 / 7) < .001);
  assert.match(app.node('metrics').textContent, /78% of registered/);
  assert.match(app.node('capacity-summary').textContent, /0.09% used/);
});

test('empty fleet renders useful states without NaN, invented activity, or an online dot', async () => {
  const app = await boot(payload({counts: {total: 0, online: 0, active_7d: 0, active_30d: 0}, feature_denominator: 0, source_counts: {}, version_counts: {}, location_counts: {}}));
  assert.match(app.node('feature-counts').textContent, /No active clocks/);
  assert.match(app.node('daily-counts').textContent, /No snapshots/);
  assert.equal(app.node('daily-counts').all('svg').length, 0);
  assert.equal(app.node('metrics').all('span').length, 0);
  assert.doesNotMatch(app.node('capacity-summary').textContent, /NaN|Infinity/);
});

test('history uses calendar days, sorts chronologically and preserves missing days', async () => {
  const day = offset => new Date(Date.now() - offset * 86400000).toISOString().slice(0, 10);
  const app = await boot(payload({daily: [
    {day: day(0), active_30d: 7}, {day: day(31), active_30d: 999},
    {day: day(29), active_30d: 2}, {day: day(10), active_30d: 5},
  ]}));
  const history = app.node('daily-counts');
  assert.match(history.textContent, /View 3 recorded snapshots/);
  assert.doesNotMatch(history.textContent, /999/);
  const points = history.all('polyline')[0].attributes.points.split(' ').map(point => point.split(',').map(Number));
  assert.equal(points.length, 3);
  assert.equal(points[0][0], 5);
  assert.equal(points[2][0], 355);
  assert.ok(points[1][0] > 180); // The missing dates retain their space on the time axis.
  assert.deepEqual(history.all('tbody')[0].all('tr').map(row => row.children[0].textContent), [day(0), day(10), day(29)]);
});

test('a single zero snapshot is a real point, not an invented trend', async () => {
  const app = await boot(payload({daily: [{day: new Date().toISOString().slice(0, 10), active_30d: 0}]}));
  assert.equal(app.node('daily-counts').all('polyline').length, 0);
  assert.equal(app.node('daily-counts').all('circle').length, 1);
  assert.match(app.node('daily-counts').textContent, /View 1 recorded snapshot/);
});

test('failed refresh retains previous counts and allows recovery', async () => {
  const app = await boot(payload());
  const before = app.node('metrics').textContent;
  app.respond(new Error('Network unavailable'));
  await app.refresh();
  assert.equal(app.node('metrics').textContent, before);
  assert.match(app.node('overview-status').textContent, /Network unavailable/);
  assert.equal(app.node('refresh').disabled, false);
  assert.equal(app.node('overview-content').attributes['aria-busy'], 'false');
  app.respond(payload());
  await app.refresh();
  assert.equal(app.node('overview-status').textContent, '');
});

test('initial failure leaves cleanup unavailable and retry restores the dashboard', async () => {
  const app = await boot(new Error('Service unavailable'));
  assert.doesNotMatch(app.node('updated-at').textContent, /Updated/);
  assert.match(app.node('overview-status').textContent, /Use Refresh/);
  assert.equal(app.node('cleanup').disabled, true);
  app.respond(payload());
  await app.refresh();
  assert.equal(app.node('cleanup').disabled, false);
  assert.match(app.node('updated-at').textContent, /Updated/);
});

test('capacity warning survives the redesign and arbitrary location labels remain text', async () => {
  const name = '<img src=x onerror=alert(1)>';
  const app = await boot(payload({capacity: {ceiling: 10, warning: true}, location_counts: {[name]: 7}}));
  assert.match(app.node('capacity-summary').textContent, /Registration capacity is running low/);
  assert.ok(app.node('location-counts').textContent.includes(name));
  assert.equal(app.node('location-counts').all('img').length, 0);
});

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => {resolve = yes; reject = no;});
  return {promise, resolve, reject};
}

test('refresh and cleanup are serialized and post-cleanup totals come from a fresh GET', async () => {
  const app = await boot(payload());
  const before = deferred();
  app.respond(before.promise);
  const refreshing = app.refresh();
  assert.equal(app.node('cleanup').disabled, true);
  await app.cleanup(); // Also guard queued clicks, not just the disabled UI.
  assert.equal(app.calls.filter(([url]) => url.endsWith('/cleanup')).length, 0);
  before.resolve(payload());
  await refreshing;
  assert.equal(app.node('cleanup').disabled, false);

  const deletion = deferred(), after = deferred();
  app.respond(url => url.endsWith('/cleanup') ? deletion.promise : after.promise);
  const cleaning = app.cleanup();
  const callsBeforeRefresh = app.calls.length;
  assert.equal(app.node('refresh').disabled, true);
  await app.refresh();
  await app.cleanup();
  assert.equal(app.calls.length, callsBeforeRefresh);
  deletion.resolve({removed: 2});
  await settle();
  assert.equal(app.calls.at(-1)[0], '/admin/api/overview');
  assert.equal(app.calls.at(-1)[2], 'GET');
  assert.equal(app.node('cleanup').disabled, true);
  assert.equal(app.node('refresh').disabled, true);
  after.resolve(payload({counts: {...payload().counts, total: 7}}));
  await cleaning;
  assert.match(app.node('cleanup-result').textContent, /Removed 2 abandoned registrations/);
  assert.equal(app.node('metrics').children[0].all('strong')[0].textContent, '7');
  assert.match(app.node('capacity-summary').textContent, /7 stored/);
  assert.equal(app.node('cleanup').disabled, false);
  assert.equal(app.node('refresh').disabled, false);
});

test('cleanup errors release the controls; failed post-cleanup refresh requires a successful retry', async () => {
  const app = await boot(payload());
  app.respond(new Error('Cleanup unavailable'));
  await app.cleanup();
  assert.match(app.node('cleanup-result').textContent, /Cleanup unavailable/);
  assert.equal(app.node('cleanup').disabled, false);
  assert.equal(app.node('refresh').disabled, false);

  app.respond(url => {
    if (url.endsWith('/cleanup')) return {removed: 2};
    throw new Error('Refresh unavailable');
  });
  await app.cleanup();
  assert.match(app.node('cleanup-result').textContent, /Removed 2/);
  assert.match(app.node('overview-status').textContent, /Refresh unavailable/);
  assert.equal(app.node('cleanup').disabled, true);
  assert.equal(app.node('refresh').disabled, false);
  app.respond(payload({counts: {...payload().counts, total: 7}}));
  await app.refresh();
  assert.equal(app.node('cleanup').disabled, false);
  assert.equal(app.node('metrics').children[0].all('strong')[0].textContent, '7');
});

test('newest reported firmware stays highlighted whether it is rare or the majority', async () => {
  for (const counts of [{'0.2.9': 5, '0.2.11': 2}, {'0.2.9': 2, '0.2.11': 5}, {'0.2.11': 7}]) {
    const app = await boot(payload({version_counts: counts}));
    const rows = app.node('version-counts').children[0].children;
    for (const row of rows) {
      const label = row.children[0].children[0];
      const fill = row.all('span').find(el => el.className?.includes('bar-fill'));
      const newest = label.text === '0.2.11';
      assert.equal(label.textContent.includes('Newest reported'), newest);
      assert.equal(fill.className.includes('neutral'), !newest);
    }
  }
});

test('firmware emphasis compares major and minor versions, and ignores unknown labels', async () => {
  const app = await boot(payload({version_counts: {'0.99.99': 1, '1.9.99': 1, '1.10.0': 1, Unknown: 4}}));
  const badges = app.node('version-counts').all('span').filter(el => el.className === 'version-badge');
  assert.equal(badges.length, 1);
  const labels = app.node('version-counts').all('span').filter(el => el.className === 'version-label mono');
  assert.equal(labels.find(el => el.textContent.includes('Newest reported')).text, '1.10.0');
});
