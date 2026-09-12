const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('docs/js/analytics.js', 'utf8');
function boot({ token = 'phc_test', hostname = 'sugarclock.com', enabledHosts = ['sugarclock.com'] } = {}) {
  const events = [], listeners = {}, scripts = [], observed = [];
  let options, observerCallback;
  const document = {
    readyState: 'loading', head: { appendChild: s => scripts.push(s) },
    createElement: () => ({}), addEventListener: (e, cb) => listeners[e] = cb,
    querySelectorAll: () => [],
  };
  const window = {
    SUGARCLOCK_ANALYTICS: { token, enabledHosts, assetHost: 'https://us-assets.i.posthog.com', apiHost: 'https://us.i.posthog.com' },
    location: { hostname, pathname: '/' }, isSecureContext: true,
    posthog: { init: (token, opts) => { options = opts; opts.loaded({ capture: (e,p) => events.push([e,p]) }); } },
    IntersectionObserver: true,
  };
  vm.runInNewContext(source, { window, document, location: window.location, navigator: { serial: {} }, URL,
    IntersectionObserver: function(cb) { observerCallback = cb; this.observe = e => observed.push(e); this.unobserve = () => {}; },
  });
  return { events, listeners, scripts, document, load: () => scripts[0].onload(), options: () => options };
}
test('disabled without token or on preview hosts', () => {
  assert.equal(boot({token: ''}).scripts.length, 0);
  assert.equal(boot({hostname: 'localhost'}).scripts.length, 0);
});
test('queues early clicks and emits one native pageview with explicit capture configuration', () => {
  const b = boot();
  b.listeners.click({target: {closest: () => ({getAttribute: k => ({'data-track-event': 'install_cta_clicked','data-track-location': 'hero'})[k]})}});
  assert.equal(b.events.length, 0);
  b.load();
  assert.deepEqual(b.events.map(e => e[0]), ['$pageview', 'install_cta_clicked']);
  assert.equal(b.events[1][1].location, 'hero');
  assert.equal(b.options().autocapture, false);
  assert.equal(b.options().disable_session_recording, true);
  assert.equal(b.options().person_profiles, 'never');
});
test('strips query strings and fragments from SDK URL properties', () => {
  const b = boot(); b.load();
  const event = b.options().before_send({ properties: {$current_url: 'https://sugarclock.com/?secret=value#private', $referrer: 'invalid'} });
  assert.equal(event.properties.$current_url, 'https://sugarclock.com/');
  assert.equal(event.properties.$referrer, undefined);
});
test('blocked SDK does not break clicks or page setup', () => {
  const b = boot(); b.scripts[0].onerror();
  assert.doesNotThrow(() => b.listeners.click({target: {}}));
  assert.doesNotThrow(() => b.listeners.DOMContentLoaded());
  assert.equal(b.events.length, 0);
});
test('FAQ opening is captured but closing is not', () => {
  const b = boot(); let click, active = false;
  b.document.querySelectorAll = selector => selector === '.faq-question' ? [{
    addEventListener: (e, cb) => click = cb,
    closest: () => ({classList: {contains: () => active}}),
    getAttribute: () => 'hardware',
  }] : [];
  b.listeners.DOMContentLoaded(); b.load(); click(); active = true; click();
  assert.equal(b.events.filter(e => e[0] === 'faq_answer_opened').length, 1);
});
test('all FAQ questions have unique nonempty static IDs and both pages load config first', () => {
  const faq = fs.readFileSync('docs/faq.html', 'utf8');
  const ids = [...faq.matchAll(/data-question-id="([^"]*)"/g)].map(m => m[1]);
  const buttons = [...faq.matchAll(/<button\b[^>]*class="[^"]*\bfaq-question\b[^"]*"[^>]*>/g)];
  assert.ok(buttons.length > 0);
  assert.equal(ids.length, buttons.length);
  buttons.forEach(([button]) => assert.match(button, /data-question-id="[^"]+"/));
  assert.ok(ids.every(Boolean));
  assert.equal(new Set(ids).size, ids.length);
  for (const page of ['index', 'faq']) {
    const html = fs.readFileSync(`docs/${page}.html`, 'utf8');
    assert.ok(html.indexOf('js/analytics-config.js') < html.indexOf('js/analytics.js'));
    assert.match(html, /<script defer src="js\/analytics-config.js"/);
    assert.match(html, /<script defer src="js\/analytics.js"/);
  }
});
test('missing enabledHosts disables analytics without throwing', () => {
  assert.equal(boot({enabledHosts: null}).scripts.length, 0);
});
test('sanitizes initial and session URLs across SDK property maps', () => {
  const b = boot(); b.load();
  const dirty = 'https://sugarclock.com/faq.html?private=sentinel#secret';
  const maps = Array.from({length: 5}, () => ({
    $session_entry_url: dirty, $initial_current_url: dirty,
    $session_entry_referrer: dirty, $referrer: 'invalid', section: 'install',
  }));
  const event = {properties: maps[0], $set: maps[1], $set_once: maps[2]};
  event.properties.$set = maps[3]; event.properties.$set_once = maps[4];
  b.options().before_send(event);
  maps.forEach(props => {
    assert.equal(props.$session_entry_url, 'https://sugarclock.com/faq.html');
    assert.equal(props.$initial_current_url, 'https://sugarclock.com/faq.html');
    assert.equal(props.$session_entry_referrer, 'https://sugarclock.com/faq.html');
    assert.equal(props.$referrer, undefined);
    assert.equal(props.section, 'install');
  });
  assert.ok(!JSON.stringify(event).includes('sentinel'));
});
test('video event uses the same video ID as playback', () => {
  const b = boot(); b.load();
  b.listeners.click({target: {closest: () => ({getAttribute: k => ({
    'data-track-event': 'demo_video_requested', 'data-video-id': 'single-source',
  })[k]})}});
  assert.equal(b.events[1][1].video_id, 'single-source');
});
