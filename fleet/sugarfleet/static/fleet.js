'use strict';
window.fleet = {
  async request(url, body, method = 'POST') {
    const response = await fetch(url, {method, headers: {'Content-Type':'application/json','X-CSRF-Token':window.fleetCsrf}, ...(body === undefined ? {} : {body:JSON.stringify(body)})});
    const value = await response.json();
    if (!response.ok) throw new Error(value.error?.message || `Request failed (${response.status})`);
    return value;
  },
  element(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined && text !== null) element.textContent = String(text);
    if (className) element.className = className;
    return element;
  },
  date(value) { return value ? new Date(Number(value)*1000).toLocaleString() : 'Not reported'; },
  label(value) { const names={dexcom:'Dexcom',libre:'FreeStyle Libre',custom:'Custom source',demo:'Demo',companion_enabled:'Companion',weather_enabled:'Weather',sysmon_enabled:'System monitor',notifications_enabled:'Notifications'}; return names[value] || String(value).replaceAll('_', ' '); },
  async action(button, output, callback) {
    button.disabled = true; output.textContent = 'Saving…';
    try { await callback(); } catch (error) { output.textContent = error.message; }
    finally { button.disabled = false; }
  },
};
document.querySelectorAll('[data-time]').forEach(element => { element.textContent = fleet.date(element.dataset.time); });
