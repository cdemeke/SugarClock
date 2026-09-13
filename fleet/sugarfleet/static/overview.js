'use strict';
(() => {
  const element = fleet.element;
  const number = value => Number(value || 0).toLocaleString();
  const percent = (value, total) => total > 0 ? Math.min(100, Math.max(0, value / total * 100)) : 0;
  const featureNames = {
    auto_cycle_enabled: 'Auto cycle', companion_enabled: 'Companion',
    notifications_enabled: 'Notifications', sysmon_enabled: 'System monitor',
    time_display_enabled: 'Time display', weather_enabled: 'Weather',
    auto_brightness: 'Auto brightness', countdown_enabled: 'Countdown',
    night_mode_enabled: 'Night mode', stopwatch_enabled: 'Stopwatch', timer_enabled: 'Timer',
  };
  const sourceColors = {dexcom: '#215bd6', libre: '#6182d4', custom: '#8494b6', demo: '#56667e', unknown: '#c6cfdb'};
  const sortedCounts = counts => Object.entries(counts || {}).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  const target = id => document.getElementById(id);

  function bar(value, total, neutral = false) {
    const track = element('div', null, 'bar-track');
    track.setAttribute('aria-hidden', 'true');
    const fill = element('span', null, `bar-fill${neutral ? ' neutral' : ''}${value > 0 && total > 0 ? ' has-value' : ''}`);
    fill.style.width = `${percent(value, total)}%`;
    track.append(fill);
    return track;
  }

  function metric(label, value, note, online = false) {
    const card = element('section', null, 'card metric');
    const total = element('strong', number(value));
    if (online && value > 0) {
      const dot = element('span', null, 'online-dot');
      dot.setAttribute('aria-hidden', 'true');
      total.append(dot);
    }
    card.append(element('h2', label), total, element('p', note, 'mono hint'));
    return card;
  }

  function renderFeatures(features, total) {
    const node = target('feature-counts');
    node.replaceChildren();
    if (!total) {
      node.append(element('p', 'No active clocks yet. Features appear after a clock checks in.', 'empty'));
      return;
    }
    const table = element('table');
    const caption = element('caption', `Enabled configuration among ${number(total)} active installations. Missing reports are unknown.`, 'sr-only');
    const head = element('thead');
    const headings = element('tr');
    ['Feature', 'On', 'Unknown', `Share of ${number(total)}`].forEach((name, index) => {
      const th = element('th', name, index ? 'numeric' : undefined);
      th.scope = 'col';
      headings.append(th);
    });
    head.append(headings);
    const body = element('tbody');
    Object.entries(features || {}).sort((a, b) => b[1].enabled - a[1].enabled || (featureNames[a[0]] || a[0]).localeCompare(featureNames[b[0]] || b[0])).forEach(([key, counts]) => {
      const row = element('tr');
      const share = element('td', null, 'share-cell');
      share.append(element('span', `${Math.round(percent(counts.enabled, total))}%`, 'sr-only'), bar(counts.enabled, total));
      row.append(element('td', featureNames[key] || fleet.label(key)),
        element('td', number(counts.enabled), `numeric ${counts.enabled ? 'count-enabled' : 'count-zero'}`),
        element('td', number(counts.unknown), 'numeric count-unknown'), share);
      body.append(row);
    });
    table.append(caption, head, body);
    node.append(table);
  }

  function renderDistribution(id, counts, total, versions = false) {
    const node = target(id);
    node.replaceChildren();
    const rows = sortedCounts(counts);
    if (!rows.length) {
      node.append(element('p', versions ? 'Firmware versions appear when clocks check in.' : 'No location reports yet.', 'empty'));
      return;
    }
    const list = element('div', null, 'distribution');
    rows.forEach(([name, count], index) => {
      const row = element('div');
      const label = element('div', null, 'distribution-label');
      label.append(element('span', name, versions ? 'mono' : undefined),
        element('span', versions ? `${number(count)} · ${Math.round(percent(count, total))}%` : number(count), 'distribution-value'));
      row.append(label, bar(count, total, name.toLowerCase() === 'unknown' || (versions && index === 0 && rows.length > 1)));
      list.append(row);
    });
    node.append(list);
    if (versions) node.append(element('p', `${rows.length} firmware version${rows.length === 1 ? '' : 's'} across ${number(total)} active installation${total === 1 ? '' : 's'}.`, 'hint panel-footnote'));
  }

  function renderSources(counts, total) {
    const node = target('source-counts');
    node.replaceChildren();
    const rows = sortedCounts(counts);
    if (!rows.length) {
      node.append(element('p', 'Data sources appear when clocks report their configuration.', 'empty'));
      return;
    }
    const stacked = element('div', null, 'source-bar');
    stacked.setAttribute('aria-hidden', 'true');
    const legend = element('ul', null, 'source-legend');
    rows.forEach(([name, count]) => {
      const color = sourceColors[name] || sourceColors.unknown;
      const segment = element('span', null, 'source-segment');
      segment.style.width = `${percent(count, total)}%`;
      segment.style.backgroundColor = color;
      stacked.append(segment);
      const row = element('li');
      const swatch = element('span', null, 'source-swatch');
      swatch.style.backgroundColor = color;
      swatch.setAttribute('aria-hidden', 'true');
      row.append(swatch, element('span', name === 'unknown' ? 'Unknown' : fleet.label(name)),
        element('span', `${number(count)} · ${Math.round(percent(count, total))}%`, 'distribution-value'));
      legend.append(row);
    });
    node.append(stacked, legend);
  }

  function renderHistory(daily) {
    const node = target('daily-counts');
    node.replaceChildren();
    const today = new Date();
    const end = Date.UTC(today.getUTCFullYear(), today.getUTCMonth(), today.getUTCDate());
    const start = end - 29 * 86400000;
    const days = (daily || []).filter(day => {
      const date = Date.parse(`${day.day}T00:00:00Z`);
      return date >= start && date <= end && Number.isFinite(day.active_30d);
    }).sort((a, b) => a.day.localeCompare(b.day));
    if (!days.length) {
      node.append(element('p', 'No snapshots in the last 30 days yet.', 'empty'));
      return;
    }
    const latest = days[days.length - 1];
    const chart = element('div', null, 'activity-chart');
    const stat = element('div', null, 'activity-total');
    stat.append(element('strong', number(latest.active_30d)), element('span', latest.day, 'mono'));
    const svgElement = (tag, attrs) => {
      const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
      Object.entries(attrs || {}).forEach(([key, value]) => node.setAttribute(key, value));
      return node;
    };
    const svg = svgElement('svg', {viewBox: '0 0 360 76', class: 'sparkline', role: 'img', 'aria-label': `Activity over ${days.length} recorded day${days.length === 1 ? '' : 's'}; latest snapshot ${latest.day}: ${number(latest.active_30d)} active installations. Exact values below.`});
    const max = Math.max(1, ...days.map(day => day.active_30d));
    const points = days.map(day => {
      const x = 5 + (Date.parse(`${day.day}T00:00:00Z`) - start) / (end - start) * 350;
      return [x, 66 - day.active_30d / max * 56];
    });
    svg.append(svgElement('line', {x1: 5, y1: 66, x2: 355, y2: 66, stroke: 'var(--line)'}));
    if (points.length > 1) svg.append(svgElement('polyline', {points: points.map(point => point.join(',')).join(' '), fill: 'none', stroke: 'currentColor', 'stroke-width': 2.5, 'stroke-linejoin': 'round', 'vector-effect': 'non-scaling-stroke'}));
    points.forEach(([cx, cy], index) => {
      const circle = svgElement('circle', {cx, cy, r: points.length === 1 ? 4 : 2.5, fill: 'currentColor'});
      const title = svgElement('title');
      title.textContent = `${days[index].day}: ${number(days[index].active_30d)} active installations`;
      circle.append(title);
      svg.append(circle);
    });
    chart.append(stat, svg);
    const detail = element('details', null, 'history-details');
    detail.append(element('summary', `View ${days.length} recorded snapshot${days.length === 1 ? '' : 's'}`));
    const wrap = element('div', null, 'table-wrap');
    const table = element('table');
    table.append(element('caption', 'Recorded activity snapshots; dates are UTC.', 'sr-only'));
    const head = element('thead');
    const heading = element('tr');
    ['Date (UTC)', 'Active · 30 days'].forEach(label => {const th = element('th', label); th.scope = 'col'; heading.append(th);});
    head.append(heading);
    const body = element('tbody');
    days.slice().reverse().forEach(day => {const row = element('tr'); row.append(element('td', day.day), element('td', number(day.active_30d))); body.append(row);});
    table.append(head, body);
    wrap.append(table);
    detail.append(wrap);
    node.append(chart, detail);
  }

  function renderCapacity(counts, capacity) {
    const node = target('capacity-summary');
    node.replaceChildren();
    const ceiling = capacity?.ceiling;
    const ratio = ceiling > 0 ? counts.total / ceiling * 100 : 0;
    node.append(bar(counts.total, ceiling));
    const labels = element('div', null, 'capacity-labels mono');
    const used = ratio > 0 && ratio < .01 ? '<0.01' : ratio.toLocaleString(undefined, {maximumFractionDigits: 2});
    labels.append(element('span', `${number(counts.total)} stored · ${ceiling ? `${used}% used` : 'Capacity unavailable'}`), element('span', ceiling ? number(ceiling) : '—'));
    const breakdown = element('div', null, 'capacity-counts');
    [['blocked', counts.blocked], ['retired', counts.retired], ['on legacy updates', counts.legacy]].forEach(([label, count]) => {
      const item = element('span');
      item.append(element('strong', number(count)), document.createTextNode(` ${label}`));
      breakdown.append(item);
    });
    node.append(labels, breakdown);
    if (capacity?.warning) node.append(element('p', 'Registration capacity is running low. Review abandoned registrations to free space.', 'warning'));
  }

  let refreshing = false;
  async function refreshOverview() {
    if (refreshing) return;
    refreshing = true;
    const button = target('refresh');
    const status = target('overview-status');
    const content = target('overview-content');
    button.disabled = true;
    button.textContent = 'Refreshing…';
    content.setAttribute('aria-busy', 'true');
    status.textContent = 'Loading fleet summary…';
    status.classList.remove('is-error');
    try {
      const data = await fleet.request('/admin/api/overview', undefined, 'GET');
      const counts = data.counts;
      target('metrics').replaceChildren(
        metric('Registered', counts.total, data.capacity?.ceiling ? `of ${number(data.capacity.ceiling)} cap` : 'Stored installations'),
        metric('Online now', counts.online, counts.total ? `${Math.round(percent(counts.online, counts.total))}% of registered` : 'No registered clocks', true),
        metric('Active · 7 days', counts.active_7d, 'Rolling 7-day window'),
        metric('Active · 30 days', counts.active_30d, `${number(counts.feature_reporting)} reporting config`),
      );
      target('reporting-summary').textContent = counts.active_30d
        ? `Feature reports come from ${number(counts.feature_reporting)} of ${number(counts.active_30d)} active installations; missing fields are unknown.`
        : 'No clocks have checked in during the last 30 days.';
      renderFeatures(data.feature_counts, data.feature_denominator ?? counts.active_30d);
      renderDistribution('version-counts', data.version_counts, counts.active_30d, true);
      renderSources(data.source_counts, counts.active_30d);
      renderDistribution('location-counts', data.location_counts, counts.active_30d);
      renderHistory(data.daily);
      renderCapacity(counts, data.capacity);
      target('updated-at').textContent = `Updated ${new Date().toLocaleTimeString()}`;
      target('cleanup').disabled = false;
      status.textContent = '';
    } catch (error) {
      status.textContent = `Couldn’t refresh the summary. ${error.message} Use Refresh to try again. Any displayed counts are from the last successful refresh.`;
      status.classList.add('is-error');
    } finally {
      refreshing = false;
      button.disabled = false;
      button.textContent = 'Refresh';
      content.setAttribute('aria-busy', 'false');
    }
  }

  target('refresh').addEventListener('click', refreshOverview);
  target('cleanup').addEventListener('click', event => fleet.action(event.currentTarget, target('cleanup-result'), async () => {
    const result = await fleet.request('/admin/api/cleanup', {registration_only: true});
    const removed = result.deleted ?? result.removed ?? 0;
    target('cleanup-result').textContent = `Removed ${number(removed)} abandoned registration${removed === 1 ? '' : 's'}.`;
    await refreshOverview();
  }));
  refreshOverview();
})();
