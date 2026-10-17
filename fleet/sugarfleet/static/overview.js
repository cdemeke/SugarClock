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
  // Color follows the entity, never its rank.
  const sourceColors = {dexcom: 'var(--series-1)', libre: 'var(--series-2)', custom: 'var(--series-3)', demo: 'var(--series-4)', unknown: 'var(--neutral)'};
  const sortedCounts = counts => Object.entries(counts || {}).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  const target = id => document.getElementById(id);
  const DAY = 86400000;
  const svgElement = (tag, attrs) => {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs || {}).forEach(([key, value]) => node.setAttribute(key, value));
    return node;
  };
  const devicesLink = query => `/admin/devices?q=${encodeURIComponent(query)}`;
  const isoDay = time => new Date(time).toISOString().slice(0, 10);
  const dayLabel = (day, options = {month: 'short', day: 'numeric'}) => new Date(`${day}T12:00:00Z`).toLocaleDateString(undefined, {timeZone: 'UTC', ...options});
  const signed = value => `${value > 0 ? '+' : value < 0 ? '−' : '±'}${number(Math.abs(value))}`;

  function bar(value, total, neutral = false) {
    const track = element('div', null, 'bar-track');
    track.setAttribute('aria-hidden', 'true');
    const fill = element('span', null, `bar-fill${neutral ? ' neutral' : ''}${value > 0 && total > 0 ? ' has-value' : ''}`);
    fill.style.width = `${percent(value, total)}%`;
    track.append(fill);
    return track;
  }

  /* ---------- Daily snapshots ---------- */

  // Snapshots store the full counts object; older rows may lack newer keys.
  function snapshots(daily) {
    const seen = new Set();
    return (daily || []).filter(day => typeof day.day === 'string' && Number.isFinite(day.active_30d) && !seen.has(day.day) && seen.add(day.day))
      .sort((a, b) => a.day.localeCompare(b.day));
  }
  function snapshotOnOrBefore(days, day) {
    for (let index = days.length - 1; index >= 0; index--) if (days[index].day <= day) return days[index];
    return undefined;
  }
  function weekAgo(days, key) {
    const previous = snapshotOnOrBefore(days, isoDay(Date.now() - 7 * DAY));
    return previous && Number.isFinite(previous[key]) ? previous[key] : undefined;
  }

  function sparkline(values) {
    const svg = svgElement('svg', {viewBox: '0 0 96 32', class: 'metric-spark', 'aria-hidden': 'true'});
    const min = Math.min(...values), max = Math.max(...values);
    const points = values.map((value, index) => [index / (values.length - 1) * 96, 29 - (value - min) / (max - min || 1) * 25]);
    svg.append(svgElement('polyline', {points: points.map(point => point.join(',')).join(' '), fill: 'none', stroke: 'currentColor', 'stroke-width': 1.75, 'stroke-linejoin': 'round', 'vector-effect': 'non-scaling-stroke'}));
    const [cx, cy] = points[points.length - 1];
    svg.append(svgElement('circle', {cx, cy, r: 2.5, fill: 'currentColor'}));
    return svg;
  }

  function metric(label, value, note, {online = false, previous, trend, meter} = {}) {
    const card = element('section', null, 'card metric');
    const heading = element('h2', label);
    const total = element('strong', number(value));
    if (online && value > 0) {
      const dot = element('span', null, 'online-dot');
      dot.setAttribute('aria-hidden', 'true');
      total.append(dot);
    }
    if (Number.isFinite(previous) && value !== previous) {
      const delta = element('span', I18n.message("{0} vs. 7 days ago", [signed(value - previous)]), `delta ${value > previous ? 'up' : 'down'}`);
      total.append(delta);
    }
    card.append(heading, total);
    if (trend && trend.length > 1) card.append(sparkline(trend));
    if (meter) card.append(meter);
    card.append(element('p', note, 'hint'));
    return card;
  }

  function renderMetrics(data, days) {
    const counts = data.counts;
    const recent = days.filter(day => day.day >= isoDay(Date.now() - 29 * DAY));
    const trend = key => recent.map(day => day[key]).filter(Number.isFinite);
    const ceiling = data.capacity?.ceiling;
    const capacity = ceiling ? bar(counts.total, ceiling) : undefined;
    const status = element('div', null, 'status-split');
    status.setAttribute('aria-hidden', 'true');
    const offline = Math.max(0, counts.total - counts.online - (counts.dormant || 0) - (counts.blocked || 0) - (counts.retired || 0) - (counts.registration_only || 0));
    [[counts.online, 'online'], [offline, 'offline'], [counts.dormant || 0, 'dormant']].forEach(([count, name]) => {
      if (!count) return;
      const part = element('span', null, `split-${name}`);
      part.style.flexGrow = String(count);
      status.append(part);
    });
    target('metrics').replaceChildren(
      metric(I18n.message('Registered'), counts.total, ceiling ? I18n.message("of {0} cap", [number(ceiling)]) : I18n.message('Stored installations'), {previous: weekAgo(days, 'total'), meter: capacity}),
      metric(I18n.message('Online now'), counts.online, counts.total ? I18n.message("{0}% of registered", [Math.round(percent(counts.online, counts.total))]) : I18n.message('No registered clocks'), {online: true, meter: counts.total ? status : undefined}),
      metric(I18n.message('Active · 7 days'), counts.active_7d, I18n.message('Rolling 7-day window'), {previous: weekAgo(days, 'active_7d'), trend: trend('active_7d')}),
      metric(I18n.message('Active · 30 days'), counts.active_30d, I18n.message("{0} reporting config", [number(counts.feature_reporting)]), {previous: weekAgo(days, 'active_30d'), trend: trend('active_30d')}),
    );
  }

  /* ---------- Activity history: chart + selected-day panel ---------- */

  const chart = {width: 720, height: 240, left: 40, right: 14, top: 30, bottom: 30};
  let historyDays = [], range = 30, selectedDay, chartSvg;

  function rangeDays() {
    const end = Date.parse(`${isoDay(Date.now())}T00:00:00Z`);
    return Array.from({length: range}, (_, index) => isoDay(end - (range - 1 - index) * DAY));
  }

  function renderRangeControl() {
    const group = target('activity-range');
    group.replaceChildren();
    [7, 30, 90].forEach(days => {
      const button = element('button', I18n.message("{0}D", [days]));
      button.type = 'button';
      button.setAttribute('aria-pressed', String(days === range));
      button.addEventListener('click', () => { range = days; renderHistory(); });
      group.append(button);
    });
  }

  function renderHistory(daily) {
    if (daily) historyDays = snapshots(daily);
    renderRangeControl();
    const node = target('daily-counts');
    node.replaceChildren();
    const calendar = rangeDays();
    const byDay = new Map(historyDays.map(day => [day.day, day]));
    const days = calendar.filter(day => byDay.has(day)).map(day => byDay.get(day));
    if (!days.length) {
      node.append(element('p', I18n.message("No snapshots in the last {0} days yet.", [range]), 'empty'));
      selectedDay = undefined;
      renderDay();
      return;
    }
    if (!selectedDay || !calendar.includes(selectedDay)) selectedDay = days[days.length - 1].day;

    const plotWidth = chart.width - chart.left - chart.right, plotHeight = chart.height - chart.top - chart.bottom;
    const step = plotWidth / Math.max(1, calendar.length - 1);
    const peak = Math.max(1, ...days.map(day => day.active_30d));
    const magnitude = 10 ** Math.floor(Math.log10(peak));
    const yMax = Math.ceil(peak * 1.1 / magnitude) * magnitude;
    const x = index => chart.left + index * step;
    const y = value => chart.top + (1 - value / yMax) * plotHeight;
    const latest = days[days.length - 1];
    const svg = svgElement('svg', {viewBox: `0 0 ${chart.width} ${chart.height}`, class: 'activity-svg', role: 'img', tabindex: '0',
      'aria-label': `Active installations per day over the last ${range} days; ${days.length} recorded. Latest ${latest.day}: ${number(latest.active_30d)}. Use the arrow keys to change the selected day; exact values are in the table below.`});
    const defs = svgElement('defs');
    const gradient = svgElement('linearGradient', {id: 'activity-fill', x1: 0, y1: 0, x2: 0, y2: 1});
    gradient.append(svgElement('stop', {offset: 0, 'stop-color': 'var(--series-1)', 'stop-opacity': .16}), svgElement('stop', {offset: 1, 'stop-color': 'var(--series-1)', 'stop-opacity': 0}));
    defs.append(gradient);
    svg.append(defs);

    [0, yMax / 2, yMax].forEach(tick => {
      svg.append(svgElement('line', {x1: chart.left, x2: chart.width - chart.right, y1: y(tick), y2: y(tick), class: 'grid-line'}));
      const label = svgElement('text', {x: chart.left - 8, y: y(tick) + 4, 'text-anchor': 'end', class: 'axis-label'});
      I18n.text(label, number(tick), false);
      svg.append(label);
    });
    const every = range <= 7 ? 1 : range <= 30 ? 5 : 15;
    calendar.forEach((day, index) => {
      if ((calendar.length - 1 - index) % every) return;
      const label = svgElement('text', {x: x(index), y: chart.height - 8, 'text-anchor': 'middle', class: 'axis-label'});
      I18n.text(label, dayLabel(day), false);
      svg.append(label);
    });

    const selectedIndex = calendar.indexOf(selectedDay);
    svg.append(svgElement('rect', {x: x(selectedIndex) - Math.max(step, 8) / 2, y: chart.top - 10, width: Math.max(step, 8), height: plotHeight + 10, rx: 6, class: 'selected-band'}));

    // Solid between consecutive calendar days, dashed across days without a snapshot.
    const points = days.map(day => ({day, index: calendar.indexOf(day.day)})).map(point => ({...point, x: x(point.index), y: y(point.day.active_30d)}));
    let solid = '', gaps = '';
    points.forEach((point, index) => {
      const previous = points[index - 1];
      if (!previous) solid += `M${point.x} ${point.y}`;
      else if (point.index - previous.index === 1) solid += ` L${point.x} ${point.y}`;
      else { gaps += ` M${previous.x} ${previous.y} L${point.x} ${point.y}`; solid += ` M${point.x} ${point.y}`; }
    });
    if (points.length > 1) {
      svg.append(svgElement('path', {d: `M${points[0].x} ${y(0)} ${points.map(point => `L${point.x} ${point.y}`).join(' ')} L${points[points.length - 1].x} ${y(0)}Z`, class: 'activity-area'}));
      if (gaps) svg.append(svgElement('path', {d: gaps.trim(), class: 'activity-gap'}));
      svg.append(svgElement('path', {d: solid, class: 'activity-line'}));
    }

    // Label the largest single-day change, when it is notable.
    let jump;
    points.forEach((point, index) => {
      const previous = points[index - 1];
      if (!previous || point.index - previous.index !== 1) return;
      const change = point.day.active_30d - previous.day.active_30d;
      if (Math.abs(change) >= Math.max(5, previous.day.active_30d * .15) && (!jump || Math.abs(change) > Math.abs(jump.change))) jump = {point, change};
    });
    if (jump) {
      svg.append(svgElement('line', {x1: jump.point.x, x2: jump.point.x, y1: chart.top - 12, y2: jump.point.y - 6, class: 'annotation-line'}));
      svg.append(svgElement('circle', {cx: jump.point.x, cy: chart.top - 14, r: 3.5, class: 'annotation-dot'}));
      const note = svgElement('text', {x: jump.point.x - 8, y: chart.top - 10, 'text-anchor': 'end', class: 'annotation-label'});
      I18n.text(note, I18n.message("{0} in one day", [signed(jump.change)]));
      svg.append(note);
    }

    points.forEach(point => {
      const selected = point.day.day === selectedDay;
      svg.append(svgElement('circle', {cx: point.x, cy: point.y, r: selected ? 5 : points.length === 1 ? 4 : range > 30 ? 2 : 3, class: `activity-dot${selected ? ' selected' : ''}`}));
    });

    const tooltip = element('div', null, 'chart-tooltip');
    tooltip.setAttribute('aria-hidden', 'true');
    const hide = () => tooltip.classList.remove('visible');
    calendar.forEach((day, index) => {
      const hit = svgElement('rect', {x: x(index) - step / 2, y: 0, width: Math.max(step, 6), height: chart.height, class: 'activity-hit'});
      const snapshot = byDay.get(day);
      hit.addEventListener('mouseenter', () => {
        const previous = snapshot && snapshotOnOrBefore(historyDays, isoDay(Date.parse(`${day}T00:00:00Z`) - DAY));
        tooltip.replaceChildren(element('span', dayLabel(day, {weekday: 'short', month: 'short', day: 'numeric'}), 'tooltip-date'),
          element('strong', snapshot ? number(snapshot.active_30d) : I18n.message('No snapshot')),
          element('span', snapshot ? I18n.message("active{0}", [previous ? ` · ${signed(snapshot.active_30d - previous.active_30d)}` : '']) : I18n.message('recorded this day'), 'tooltip-note'));
        tooltip.style.left = `${Math.min(88, Math.max(12, x(index) / chart.width * 100))}%`;
        tooltip.style.top = `${(snapshot ? y(snapshot.active_30d) : y(yMax / 2)) / chart.height * 100}%`;
        tooltip.classList.add('visible');
      });
      hit.addEventListener('click', () => selectDay(day));
      svg.append(hit);
    });
    svg.addEventListener('mouseleave', hide);
    svg.addEventListener('keydown', event => {
      if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
      event.preventDefault();
      stepDay(event.key === 'ArrowLeft' ? -1 : 1);
      chartSvg?.focus?.(); // The chart re-renders on selection; keep keyboard focus on it.
    });
    chartSvg = svg;

    const wrap = element('div', null, 'activity-plot');
    wrap.append(svg, tooltip);
    const legend = element('div', null, 'activity-legend');
    legend.append(element('span', I18n.message('Active · 30 days'), 'legend-line'));
    if (gaps) legend.append(element('span', I18n.message('No snapshot recorded'), 'legend-gap'));
    legend.append(element('span', I18n.message("{0} of {1} days recorded", [number(days.length),number(range)]), 'mono'));
    node.append(wrap, legend, snapshotTable(days));
    renderDay();
  }

  function snapshotTable(days) {
    const detail = element('details', null, 'history-details');
    detail.append(element('summary', I18n.message(days.length === 1 ? "View {0} recorded snapshot" : "View {0} recorded snapshots", [days.length])));
    const wrap = element('div', null, 'table-wrap');
    const table = element('table');
    table.append(element('caption', I18n.message('Recorded activity snapshots; dates are UTC.'), 'sr-only'));
    const head = element('thead');
    const heading = element('tr');
    ['Date (UTC)', 'Active · 30 days', 'Active · 7 days', 'Online', 'Registered'].forEach((label, index) => {
      const th = element('th', I18n.message(label), index ? 'numeric' : undefined);
      th.scope = 'col';
      heading.append(th);
    });
    head.append(heading);
    const body = element('tbody');
    days.slice().reverse().forEach(day => {
      const row = element('tr');
      row.append(element('td', day.day), ...['active_30d', 'active_7d', 'online', 'total'].map(key => element('td', Number.isFinite(day[key]) ? number(day[key]) : '—', 'numeric')));
      body.append(row);
    });
    table.append(head, body);
    wrap.append(table);
    detail.append(wrap);
    return detail;
  }

  function selectDay(day) {
    selectedDay = day;
    renderHistory();
  }
  function stepDay(direction) {
    const calendar = rangeDays();
    const index = calendar.indexOf(selectedDay);
    const next = calendar[Math.min(calendar.length - 1, Math.max(0, (index < 0 ? calendar.length - 1 : index) + direction))];
    if (next !== selectedDay) selectDay(next);
  }

  function renderDay() {
    const node = target('day-detail');
    node.replaceChildren();
    if (!selectedDay) {
      node.append(element('p', I18n.message('Select a day in the activity chart to see its snapshot.'), 'empty'));
      return;
    }
    const snapshot = historyDays.find(day => day.day === selectedDay);
    const previous = snapshotOnOrBefore(historyDays, isoDay(Date.parse(`${selectedDay}T00:00:00Z`) - DAY));
    const header = element('div', null, 'day-header');
    const back = element('button', '‹', 'icon-button');
    back.type = 'button';
    back.setAttribute('aria-label', 'Previous day');
    back.addEventListener('click', () => stepDay(-1));
    const next = element('button', '›', 'icon-button');
    next.type = 'button';
    next.setAttribute('aria-label', 'Next day');
    next.addEventListener('click', () => stepDay(1));
    const title = element('div', null, 'day-title');
    title.append(element('strong', dayLabel(selectedDay, {weekday: 'long', month: 'long', day: 'numeric'})),
      element('span', selectedDay === isoDay(Date.now()) ? I18n.message('Today · updates hourly') : I18n.message("{0} · UTC", [selectedDay]), 'mono'));
    header.append(back, title, next);
    node.append(header);
    if (!snapshot) {
      node.append(element('p', I18n.message('No snapshot was recorded this day. Snapshots are taken while the service runs, so a quiet or offline day can be skipped.'), 'day-note'));
      return;
    }
    const stats = element('dl', null, 'day-stats');
    [['active_30d', 'Active · 30 days'], ['active_7d', 'Active · 7 days'], ['online', 'Online at snapshot'], ['total', 'Registered']].forEach(([key, label]) => {
      const item = element('div');
      const value = element('dd', Number.isFinite(snapshot[key]) ? number(snapshot[key]) : '—');
      if (previous && previous.day !== snapshot.day && Number.isFinite(snapshot[key]) && Number.isFinite(previous[key]) && snapshot[key] !== previous[key]) {
        value.append(element('span', signed(snapshot[key] - previous[key]), `delta ${snapshot[key] > previous[key] ? 'up' : 'down'}`));
      }
      item.append(element('dt', I18n.message(label)), value);
      stats.append(item);
    });
    node.append(stats);
    node.append(element('p', previous ? I18n.message("Changes compare with {0}, the previous recorded snapshot.", [dayLabel(previous.day)]) : I18n.message('This is the earliest recorded snapshot.'), 'hint day-footnote'));
  }

  /* ---------- Distributions ---------- */

  // Device registration accepts numeric x.y.z versions, without prerelease suffixes.
  function newestReportedVersion(rows) {
    return rows.map(([name]) => name).filter(name => /^\d+\.\d+\.\d+$/.test(name)).sort((a, b) => {
      const left = a.split('.').map(Number), right = b.split('.').map(Number);
      for (let i = 0; i < 3; i++) if (left[i] !== right[i]) return right[i] - left[i];
      return 0;
    })[0];
  }

  function donut(parts, center, caption) {
    const radius = 46, circumference = 2 * Math.PI * radius, total = parts.reduce((sum, part) => sum + part.count, 0);
    const svg = svgElement('svg', {viewBox: '0 0 118 118', class: 'donut', 'aria-hidden': 'true'});
    svg.append(svgElement('circle', {cx: 59, cy: 59, r: radius, class: 'donut-track'}));
    let offset = 0;
    parts.forEach(part => {
      const length = part.count / total * circumference;
      svg.append(svgElement('circle', {cx: 59, cy: 59, r: radius, stroke: part.color, class: 'donut-arc',
        'stroke-dasharray': `${Math.max(length - (parts.length > 1 ? 2.5 : 0), 1.5)} ${circumference}`, 'stroke-dashoffset': -offset, transform: 'rotate(-90 59 59)'}));
      offset += length;
    });
    const value = svgElement('text', {x: 59, y: 61, 'text-anchor': 'middle', class: 'donut-value'});
    I18n.text(value, center, false);
    const label = svgElement('text', {x: 59, y: 77, 'text-anchor': 'middle', class: 'donut-caption'});
    I18n.text(label, caption);
    svg.append(value, label);
    return svg;
  }

  function legendRow(color, label, value, href, extra) {
    const row = element(href ? 'a' : 'div', null, 'legend-row');
    if (href) row.href = href;
    const swatch = element('span', null, 'swatch');
    swatch.style.backgroundColor = color;
    swatch.setAttribute('aria-hidden', 'true');
    row.append(swatch, label);
    if (extra) row.append(extra);
    row.append(element('span', value, 'legend-value'));
    return row;
  }

  function renderVersions(counts, total) {
    const node = target('version-counts');
    node.replaceChildren();
    const rows = sortedCounts(counts);
    if (!rows.length) {
      node.append(element('p', I18n.message('Firmware versions appear when clocks check in.'), 'empty'));
      return;
    }
    const newest = newestReportedVersion(rows);
    const [common, commonCount] = rows[0];
    const color = name => name === newest ? 'var(--series-1)' : name === common ? 'var(--series-3)' : 'var(--neutral)';
    const legend = element('div', null, 'legend');
    rows.forEach(([name, count]) => {
      const label = element('span', name, 'version-label mono');
      const badge = name === newest ? element('span', I18n.message('Newest reported'), 'version-badge') : name === common && rows.length > 1 ? element('span', I18n.message('Most common'), 'version-badge muted') : undefined;
      legend.append(legendRow(color(name), label, `${number(count)} · ${Math.round(percent(count, total))}%`, /^\d+\.\d+\.\d+$/.test(name) ? devicesLink(name) : undefined, badge));
    });
    const layout = element('div', null, 'donut-layout');
    layout.append(donut(rows.map(([name, count]) => ({count, color: color(name)})), `${Math.round(percent(commonCount, total))}%`, common), legend);
    node.append(layout, element('p', I18n.message("Firmware versions: {0}. Active installations: {1}.", [rows.length,number(total)]), 'hint panel-footnote'));
  }

  function renderSources(counts, total) {
    const node = target('source-counts');
    node.replaceChildren();
    const rows = sortedCounts(counts);
    if (!rows.length) {
      node.append(element('p', I18n.message('Data sources appear when clocks report their configuration.'), 'empty'));
      return;
    }
    const color = name => sourceColors[name] || sourceColors.unknown;
    const legend = element('div', null, 'legend');
    rows.forEach(([name, count]) => legend.append(legendRow(color(name), element('span', name === 'unknown' ? I18n.message('Unknown') : I18n.message(fleet.label(name))), `${number(count)} · ${Math.round(percent(count, total))}%`)));
    const layout = element('div', null, 'donut-layout');
    layout.append(donut(rows.map(([name, count]) => ({count, color: color(name)})), number(total), 'Devices'), legend);
    node.append(layout);
  }

  function renderFeatures(features, total) {
    const node = target('feature-counts');
    node.replaceChildren();
    if (!total) {
      node.append(element('p', I18n.message('No active clocks yet. Features appear after a clock checks in.'), 'empty'));
      return;
    }
    const rows = Object.entries(features || {}).sort((a, b) => b[1].enabled - a[1].enabled || (featureNames[a[0]] || a[0]).localeCompare(featureNames[b[0]] || b[0]));
    const unknowns = new Set(rows.map(([, counts]) => counts.unknown));
    const shared = unknowns.size === 1 ? [...unknowns][0] : undefined;
    const list = element('div', null, 'bar-list');
    rows.forEach(([key, counts]) => {
      const row = element('div', null, `bar-row${counts.enabled ? '' : ' is-zero'}`);
      const value = element('span', null, 'bar-value');
      value.append(element('strong', `${Math.round(percent(counts.enabled, total))}%`), document.createTextNode(` · ${number(counts.enabled)}`));
      if (shared === undefined && counts.unknown) value.append(element('span', I18n.message(" · {0} unknown", [number(counts.unknown)]), 'count-unknown'));
      row.append(element('span', I18n.message(featureNames[key] || fleet.label(key)), 'bar-label'), value, bar(counts.enabled, total));
      list.append(row);
    });
    node.append(list);
    node.append(element('p', shared ? I18n.message("Share of {0} active installations{1}.", [number(total),shared ? I18n.message("; {0} didn’t report configuration and count as unknown", [number(shared)]) : '']) : I18n.message("Share of {0} active installations. Unknown means the clock didn’t report that setting.", [number(total)]), 'hint panel-footnote'));
  }

  let openCountries = new Set(), locationsTouched = false;
  function renderLocations(counts, total) {
    const node = target('location-counts');
    node.replaceChildren();
    const rows = sortedCounts(counts);
    if (!rows.length) {
      node.append(element('p', I18n.message('No location reports yet.'), 'empty'));
      return;
    }
    let names;
    try { names = new Intl.DisplayNames(undefined, {type: 'region'}); } catch { names = undefined; }
    const groups = new Map();
    rows.forEach(([label, count]) => {
      const match = /^(.*), ([A-Z]{2})$/.exec(label);
      const key = match ? match[2] : label;
      if (!groups.has(key)) {
        let name = key;
        if (match) try { name = names?.of(key) || key; } catch { name = key; }
        groups.set(key, {name, count: 0, cities: []});
      }
      const group = groups.get(key);
      group.count += count;
      if (match) group.cities.push([match[1], count]);
    });
    const sorted = [...groups.entries()].sort((a, b) => b[1].count - a[1].count || a[1].name.localeCompare(b[1].name));
    if (!locationsTouched && sorted[0]?.[1].cities.length > 1) openCountries.add(sorted[0][0]); // Open the largest country until the user chooses.
    const peak = sorted[0][1].count;
    const list = element('div', null, 'bar-list');
    sorted.forEach(([key, group]) => {
      const expandable = group.cities.length > 1;
      const row = element(expandable ? 'button' : group.cities.length ? 'a' : 'div', null, 'bar-row location-row');
      if (expandable) {
        row.type = 'button';
        row.setAttribute('aria-expanded', String(openCountries.has(key)));
        row.addEventListener('click', () => {
          locationsTouched = true;
          if (openCountries.has(key)) openCountries.delete(key); else openCountries.add(key);
          renderLocations(counts, total);
        });
      } else if (group.cities.length) row.href = devicesLink(group.cities[0][0]); // Device labels may include a region, so search by city.
      const label = element('span', null, 'bar-label');
      label.append(element('span', group.name));
      if (group.cities.length) label.append(element('span', group.cities.length === 1 ? group.cities[0][0] : I18n.message("{0} cities", [group.cities.length]), 'bar-sublabel'));
      const value = element('span', null, 'bar-value');
      value.append(element('strong', number(group.count)), document.createTextNode(` · ${Math.round(percent(group.count, total))}%`));
      row.append(label, value, bar(group.count, peak, key === 'Unknown'));
      list.append(row);
      if (expandable && openCountries.has(key)) {
        const cities = element('div', null, 'city-list');
        group.cities.forEach(([city, count]) => {
          const link = element('a', null, 'bar-row city-row');
          link.href = devicesLink(city);
          link.append(element('span', city, 'bar-label'), element('span', number(count), 'bar-value'), bar(count, peak, true));
          cities.append(link);
        });
        list.append(cities);
      }
    });
    node.append(list);
  }

  function renderCapacity(counts, capacity) {
    const node = target('capacity-summary');
    node.replaceChildren();
    const ceiling = capacity?.ceiling;
    const ratio = ceiling > 0 ? counts.total / ceiling * 100 : 0;
    const used = ratio > 0 && ratio < .01 ? '<0.01' : ratio.toLocaleString(undefined, {maximumFractionDigits: 2});
    node.append(bar(counts.total, ceiling));
    const labels = element('div', null, 'capacity-labels mono');
    labels.append(element('span', I18n.message("{0} stored · {1}", [number(counts.total),ceiling ? I18n.message("{0}% used", [used]) : I18n.message('Capacity unavailable')])), element('span', ceiling ? number(ceiling) : '—'));
    const breakdown = element('div', null, 'capacity-counts');
    [['blocked', counts.blocked], ['retired', counts.retired], ['on legacy updates', counts.legacy]].forEach(([label, count]) => {
      const item = element('span');
      item.append(element('strong', number(count)), element('span', I18n.message(' ' + label)));
      breakdown.append(item);
    });
    node.append(labels, breakdown);
    if (capacity?.warning) node.append(element('p', I18n.message('Registration capacity is running low. Review abandoned registrations to free space.'), 'warning'));
  }

  /* ---------- Refresh & cleanup ---------- */

  let refreshing = false, cleaning = false, hasSummary = false;
  function updateControls() {
    target('refresh').disabled = refreshing || cleaning;
    target('cleanup').disabled = refreshing || cleaning || !hasSummary;
  }
  async function refreshOverview() {
    if (refreshing) return;
    refreshing = true;
    const button = target('refresh');
    const status = target('overview-status');
    const content = target('overview-content');
    updateControls();
    I18n.text(button, 'Refreshing…');
    content.setAttribute('aria-busy', 'true');
    I18n.text(status, 'Loading fleet summary…');
    status.classList.remove('is-error');
    try {
      const data = await fleet.request('/admin/api/overview', undefined, 'GET');
      const counts = data.counts;
      const days = snapshots(data.daily);
      renderMetrics(data, days);
      I18n.text(target('reporting-summary'), counts.active_30d ? I18n.message("Connections, enabled features, and firmware adoption. Feature reports come from {0} of {1} active installations; missing fields are unknown.", [number(counts.feature_reporting), number(counts.active_30d)]) : 'No clocks have checked in during the last 30 days.');
      renderFeatures(data.feature_counts, data.feature_denominator ?? counts.active_30d);
      renderVersions(data.version_counts, counts.active_30d);
      renderSources(data.source_counts, counts.active_30d);
      renderLocations(data.location_counts, counts.active_30d);
      renderHistory(data.daily);
      renderCapacity(counts, data.capacity);
      I18n.text(target('updated-at'), I18n.message("Updated {0}", [new Date().toLocaleTimeString()]));
      hasSummary = true;
      I18n.text(status, '', false);
    } catch (error) {
      I18n.text(status, I18n.message("Couldn’t refresh the summary. {0} Use Refresh to try again. Any displayed counts are from the last successful refresh.", [error.message]));
      status.classList.add('is-error');
    } finally {
      refreshing = false;
      updateControls();
      I18n.text(button, 'Refresh');
      content.setAttribute('aria-busy', 'false');
    }
  }

  target('refresh').addEventListener('click', () => { if (!cleaning) return refreshOverview(); });
  target('cleanup').addEventListener('click', async () => {
    if (refreshing || cleaning || !hasSummary) return;
    cleaning = true;
    updateControls();
    const output = target('cleanup-result');
    I18n.text(output, 'Cleaning up…');
    try {
      const result = await fleet.request('/admin/api/cleanup', {registration_only: true});
      const removed = result.deleted ?? result.removed ?? 0;
      I18n.text(output, I18n.message(removed === 1 ? "Removed {0} abandoned registration." : "Removed {0} abandoned registrations.", [number(removed)]));
      // A successful delete makes the previous totals stale until the next GET succeeds.
      hasSummary = false;
      await refreshOverview();
    } catch (error) {
      I18n.text(output, error.message, false);
    } finally {
      cleaning = false;
      updateControls();
    }
  });
  refreshOverview();
})();
