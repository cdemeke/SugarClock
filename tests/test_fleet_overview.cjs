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

const day = offset => new Date(Date.now() - offset * 86400000).toISOString().slice(0, 10);
const byClass = (root, tag, name) => root.all(tag).filter(el => (el.attributes.class || el.className || '').split(' ').includes(name));

test('features keep unknown reports distinct and use all active clocks as the denominator', async () => {
  const app = await boot(payload());
  const rows = app.node('feature-counts').children[0].children;
  assert.deepEqual(rows.slice(0, 2).map(row => [row.children[0].textContent, row.children[1].textContent]),
    [['Auto cycle', '29% · 2'], ['Weather', '14% · 1']]);
  assert.match(app.node('feature-counts').textContent, /Share of 7 active installations; 5 didn’t report configuration/);
  const fill = rows[0].all('span').find(el => el.className?.includes('bar-fill'));
  assert.ok(Math.abs(parseFloat(fill.style.width) - 200 / 7) < .001);
  assert.match(app.node('metrics').textContent, /78% of registered/);
  assert.match(app.node('capacity-summary').textContent, /0.09% used/);
});

test('features show per-row unknowns when clocks disagree on which settings they report', async () => {
  const app = await boot(payload({feature_counts: {weather_enabled: {enabled: 1, known: 2, unknown: 5}, auto_cycle_enabled: {enabled: 2, known: 7, unknown: 0}}}));
  const rows = app.node('feature-counts').children[0].children;
  assert.equal(rows[0].children[1].textContent, '29% · 2');
  assert.equal(rows[1].children[1].textContent, '14% · 1 · 5 unknown');
});

test('empty fleet renders useful states without NaN, invented activity, or an online dot', async () => {
  const app = await boot(payload({counts: {total: 0, online: 0, active_7d: 0, active_30d: 0}, feature_denominator: 0, source_counts: {}, version_counts: {}, location_counts: {}}));
  assert.match(app.node('feature-counts').textContent, /No active clocks/);
  assert.match(app.node('daily-counts').textContent, /No snapshots/);
  assert.equal(app.node('daily-counts').all('svg').length, 0);
  assert.equal(byClass(app.node('metrics'), 'span', 'online-dot').length, 0);
  assert.equal(byClass(app.node('metrics'), 'span', 'delta').length, 0);
  assert.match(app.node('day-detail').textContent, /Select a day/);
  for (const id of ['metrics', 'capacity-summary', 'version-counts', 'source-counts', 'location-counts']) {
    assert.doesNotMatch(app.node(id).textContent, /NaN|Infinity|undefined/);
  }
});

test('history uses calendar days, sorts chronologically and preserves missing days', async () => {
  const app = await boot(payload({daily: [
    {day: day(0), active_30d: 7}, {day: day(31), active_30d: 999},
    {day: day(29), active_30d: 2}, {day: day(10), active_30d: 5},
  ]}));
  const history = app.node('daily-counts');
  assert.match(history.textContent, /View 3 recorded snapshots/);
  assert.match(history.textContent, /3 of 30 days recorded/);
  assert.doesNotMatch(history.textContent, /999/);
  const dots = byClass(history, 'circle', 'activity-dot').map(dot => Number(dot.attributes.cx));
  assert.equal(dots.length, 3);
  assert.equal(dots[0], 40);
  assert.equal(dots[2], 706);
  assert.ok(dots[1] > 373); // The missing dates retain their space on the time axis.
  assert.equal(byClass(history, 'path', 'activity-gap').length, 1); // Gaps are drawn as gaps, not as data.
  assert.deepEqual(history.all('tbody')[0].all('tr').map(row => row.children[0].textContent), [day(0), day(10), day(29)]);
});

test('a single zero snapshot is a real point, not an invented trend', async () => {
  const app = await boot(payload({daily: [{day: day(0), active_30d: 0}]}));
  const history = app.node('daily-counts');
  assert.equal(byClass(history, 'path', 'activity-line').length, 0);
  assert.equal(byClass(history, 'circle', 'activity-dot').length, 1);
  assert.match(history.textContent, /View 1 recorded snapshot/);
  assert.match(app.node('day-detail').textContent, /Active · 30 days0/);
});

test('selecting a day shows that snapshot and compares it with the previous recorded snapshot', async () => {
  const app = await boot(payload({daily: [
    {day: day(0), active_30d: 41, active_7d: 39, online: 16, total: 43},
    {day: day(4), active_30d: 36, active_7d: 30, online: 9, total: 40},
    {day: day(6), active_30d: 23, active_7d: 21, online: 9, total: 27},
  ]}));
  assert.match(app.node('day-detail').textContent, /Today · updates hourly/); // Latest is selected by default.
  const hits = byClass(app.node('daily-counts'), 'rect', 'activity-hit');
  assert.equal(hits.length, 30);
  hits[29 - 4].listeners.click();
  const panel = app.node('day-detail');
  const stats = panel.all('dd').map(dd => dd.textContent);
  assert.deepEqual(stats, ['36+13', '30+9', '9', '40+13']);
  assert.match(panel.textContent, /previous recorded snapshot/);
  hits[29 - 5].listeners.click();
  assert.match(app.node('day-detail').textContent, /No snapshot was recorded this day/);
  assert.equal(app.node('day-detail').all('dd').length, 0);
});

test('hovering shows a tooltip and day navigation stays within the range', async () => {
  const app = await boot(payload({daily: [{day: day(1), active_30d: 5}, {day: day(0), active_30d: 7}]}));
  const hits = byClass(app.node('daily-counts'), 'rect', 'activity-hit');
  hits[28].listeners.mouseenter();
  const tooltip = byClass(app.node('daily-counts'), 'div', 'chart-tooltip')[0];
  assert.match(tooltip.textContent, /5active/);
  hits[29].listeners.mouseenter();
  assert.match(tooltip.textContent, /7active · \+2/);
  const next = app.node('day-detail').all('button')[1];
  next.listeners.click(); // Already on the latest day.
  assert.match(app.node('day-detail').textContent, /Today/);
  app.node('day-detail').all('button')[0].listeners.click();
  assert.match(app.node('day-detail').textContent, new RegExp(day(1)));
});

test('range buttons rescale the window without dropping the stored history', async () => {
  const app = await boot(payload({daily: [{day: day(0), active_30d: 7}, {day: day(20), active_30d: 3}, {day: day(60), active_30d: 1}]}));
  assert.match(app.node('daily-counts').textContent, /2 of 30 days recorded/);
  app.node('activity-range').children[0].listeners.click();
  assert.match(app.node('daily-counts').textContent, /1 of 7 days recorded/);
  assert.equal(app.node('activity-range').children[0].attributes['aria-pressed'], 'true');
  app.node('activity-range').children[2].listeners.click();
  assert.match(app.node('daily-counts').textContent, /3 of 90 days recorded/);
});

test('metrics compare with the snapshot from a week ago only when one exists', async () => {
  const app = await boot(payload({daily: [{day: day(8), active_30d: 3, active_7d: 2, total: 6}, {day: day(0), active_30d: 7, active_7d: 7, total: 9}]}));
  const deltas = byClass(app.node('metrics'), 'span', 'delta').map(el => el.textContent);
  assert.deepEqual(deltas, ['+3 vs. 7 days ago', '+5 vs. 7 days ago', '+4 vs. 7 days ago']);
  const fresh = await boot(payload({daily: [{day: day(2), active_30d: 3, active_7d: 2, total: 6}]}));
  assert.equal(byClass(fresh.node('metrics'), 'span', 'delta').length, 0);
});

test('snapshots missing newer fields render as unknown rather than zero', async () => {
  const app = await boot(payload({daily: [{day: day(0), active_30d: 7}]}));
  assert.deepEqual(app.node('day-detail').all('dd').map(dd => dd.textContent), ['7', '—', '—', '—']);
  assert.match(app.node('daily-counts').all('tbody')[0].textContent, /7———/);
});

test('locations group by country, expand to cities, and link to the device search', async () => {
  const app = await boot(payload({counts: {...payload().counts, active_30d: 20}, location_counts: {'Valdepenas, ES': 12, 'Madrid, ES': 1, 'Washington, US': 5, Unknown: 2}}));
  const list = app.node('location-counts').children[0];
  const [spain, cities, usa, unknown] = list.children;
  assert.equal(spain.tag, 'button');
  assert.equal(spain.attributes['aria-expanded'], 'true'); // The largest multi-city country starts open.
  assert.match(spain.textContent, /2 cities13 · 65%/);
  assert.equal(byClass(spain, 'span', 'country-flag')[0].textContent, '🇪🇸');
  assert.equal(byClass(spain, 'span', 'country-flag')[0].attributes['aria-hidden'], 'true');
  assert.equal(byClass(usa, 'span', 'country-flag')[0].textContent, '🇺🇸');
  assert.equal(byClass(unknown, 'span', 'country-flag').length, 0);
  assert.deepEqual(cities.children.map(row => row.href), ['/admin/devices?q=Valdepenas', '/admin/devices?q=Madrid']);
  assert.equal(usa.href, '/admin/devices?q=Washington');
  assert.equal(unknown.tag, 'div');
  spain.listeners.click();
  assert.equal(app.node('location-counts').children[0].children.length, 3);
});

test('country-only location codes render country flag emoji without city sublabel', async () => {
  const app = await boot(payload({counts: {...payload().counts, active_30d: 5}, location_counts: {'FR': 5}}));
  const list = app.node('location-counts').children[0];
  const [france] = list.children;
  assert.equal(byClass(france, 'span', 'country-flag')[0].textContent, '🇫🇷');
  assert.equal(byClass(france, 'span', 'bar-sublabel').length, 0);
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

const versionRows = app => app.node('version-counts').children[0].children[1].children;
const badgeOf = row => row.children.find(el => el.className?.startsWith('version-badge'));

test('newest reported firmware stays highlighted whether it is rare or the majority', async () => {
  for (const counts of [{'0.2.9': 5, '0.2.11': 2}, {'0.2.9': 2, '0.2.11': 5}, {'0.2.11': 7}]) {
    const app = await boot(payload({version_counts: counts}));
    for (const row of versionRows(app)) {
      const newest = row.children[1].text === '0.2.11';
      assert.equal(badgeOf(row)?.textContent === 'Newest reported', newest);
      assert.equal(row.children[0].style.backgroundColor === 'var(--series-1)', newest);
      assert.equal(row.href, `/admin/devices?q=${row.children[1].text}`);
    }
  }
});

test('firmware emphasis compares major and minor versions, and ignores unknown labels', async () => {
  const app = await boot(payload({version_counts: {'0.99.99': 1, '1.9.99': 1, '1.10.0': 1, Unknown: 4}}));
  const rows = versionRows(app);
  const newest = rows.filter(row => badgeOf(row)?.textContent === 'Newest reported');
  assert.equal(newest.length, 1);
  assert.equal(newest[0].children[1].text, '1.10.0');
  const unknown = rows.find(row => row.children[1].text === 'Unknown');
  assert.equal(badgeOf(unknown)?.textContent, 'Most common');
  assert.equal(unknown.href, undefined); // Only real versions link to the device search.
});
