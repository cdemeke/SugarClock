'use strict';
window.fleet = {
  async request(url, body, method = 'POST') {
    const isRead = method === 'GET' || method === 'HEAD';
    const headers = isRead ? {} : {'Content-Type': 'application/json', 'X-CSRF-Token': window.fleetCsrf};
    const response = await fetch(url, {method, headers, ...(body === undefined ? {} : {body: JSON.stringify(body)})});
    const contentType = response.headers.get('content-type') || '';
    const value = contentType.includes('application/json') ? await response.json().catch(() => null) : null;
    if (!response.ok) throw new Error(value?.error?.message || `Request failed (${response.status})`);
    if (value === null) throw new Error(`Unexpected response (${response.status})`);
    return value;
  },
  element(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined && text !== null) I18n.text(element, text, typeof text === "object");
    if (className) element.className = className;
    return element;
  },
  date(value) { return value ? new Date(Number(value)*1000).toLocaleString() : 'Not reported'; },
  label(value) { const names={dexcom:'Dexcom',libre:'FreeStyle Libre',custom:'Custom source',demo:'Demo',data_source:'Data source',companion_enabled:'Companion',companion_character:'Companion character',weather_enabled:'Weather',timer_enabled:'Timer',stopwatch_enabled:'Stopwatch',countdown_enabled:'Countdown',auto_cycle_enabled:'Auto cycle',notifications_enabled:'Notifications',sysmon_enabled:'System monitor',night_mode_enabled:'Night mode',auto_brightness:'Auto brightness',time_display_enabled:'Time display'}; return names[value] || String(value).replaceAll('_', ' '); },
  async action(button, output, callback) {
    button.disabled = true; I18n.text(output, 'Saving…');
    try { await callback(); } catch (error) { I18n.text(output, error.message, false); }
    finally { button.disabled = false; }
  },
};
document.querySelectorAll('[data-time]').forEach(element => { I18n.text(element, fleet.date(element.dataset.time), false); });
