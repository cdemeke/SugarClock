/* Shared UI localization. Only authored copy is translated. */
(function () {
  'use strict';
  const script = document.currentScript;
  // Clock pages name a remote catalog folder so translations need no firmware
  // space. English is built in, so those pages still work without internet.
  const remote = Boolean(script.dataset.catalogs);
  const base = new URL(script.dataset.catalogs || 'locales/', script.src);
  const languages = [
    ['en', 'English'], ['zh', '中文'], ['hi', 'हिन्दी'], ['es', 'Español'],
    ['ar', 'العربية'], ['fr', 'Français'], ['bn', 'বাংলা'], ['pt', 'Português'],
    ['ru', 'Русский'], ['ur', 'اردو'], ['id', 'Bahasa Indonesia'], ['de', 'Deutsch'],
    ['ja', '日本語'], ['mr', 'मराठी'], ['vi', 'Tiếng Việt'], ['te', 'తెలుగు'],
    ['sw', 'Kiswahili'], ['ha', 'Hausa'], ['tr', 'Türkçe'], ['ta', 'தமிழ்']
  ];
  const supported = new Set(languages.map(([code]) => code));
  const storageKey = 'sugarclock.language';
  const bindings = new Map();
  let cleanupPending = false;
  const cache = new Map([['en', {}]]);
  let locale = 'en', dictionary = {}, request = 0, preference = '';
  try { preference = localStorage.getItem(storageKey) || ''; } catch (_) {}
  if (!supported.has(preference)) preference = '';
  function browserLanguage() {
    for (const language of navigator.languages || [navigator.language]) {
      const code = String(language).toLowerCase().split(/[-_]/)[0];
      if (supported.has(code)) return code;
    }
    return 'en';
  }
  function key(source) {
    let hash = 2166136261;
    for (let i = 0; i < source.length; i++) hash = Math.imul(hash ^ source.charCodeAt(i), 16777619);
    return (hash >>> 0).toString(36);
  }
  function message(source, values = []) { return {source, values}; }
  function t(value) {
    const entry = value && typeof value === 'object' && 'source' in value ? value : message(String(value ?? ''));
    const copy = dictionary[key(entry.source)] || entry.source;
    return copy.replace(/\{(\d+)\}/g, (token, index) => {
      if (index >= entry.values.length) return token;
      // Values are data: never translate them or put them through HTML parsing.
      const value = entry.values[index];
      if (value && typeof value === 'object' && 'source' in value) return t(value);
      const data = String(value ?? '');
      return document.documentElement.dir === 'rtl' ? '\u2068' + data + '\u2069' : data;
    });
  }
  function text(element, source, translate = true) {
    if (!element) return;
    if (translate) {
      bindings.set(element, source);
      if (!cleanupPending) {
        cleanupPending = true;
        queueMicrotask(() => {
          cleanupPending = false;
          for (const node of bindings.keys()) if (!node.isConnected) bindings.delete(node);
        });
      }
    }
    else bindings.delete(element);
    const value = translate ? t(source) : String(source ?? '');
    if (element.textContent !== value) element.textContent = value;
    return value;
  }
  function apply() {
    document.documentElement.lang = locale;
    document.documentElement.dir = ['ar', 'ur'].includes(locale) ? 'rtl' : 'ltr';
    document.querySelectorAll('[data-i18n]').forEach(element => {
      if (!bindings.has(element)) element.textContent = t(element.dataset.i18n);
    });
    for (const attr of ['title', 'placeholder', 'alt', 'aria-label']) {
      document.querySelectorAll(`[data-i18n-${attr}]`).forEach(element => {
        element.setAttribute(attr, t(element.getAttribute(`data-i18n-${attr}`)));
      });
    }
    for (const [element, source] of bindings) {
      if (element.isConnected) text(element, source);
      else bindings.delete(element);
    }
    renderPickers();
    document.dispatchEvent(new CustomEvent('languagechange', {detail: {language: locale}}));
  }
  async function setLanguage(value, persist = true) {
    const chosen = supported.has(value) ? value : '';
    const next = chosen || browserLanguage();
    const currentRequest = ++request;
    setBusy(true);
    try {
      if (!cache.has(next)) {
        const response = await fetch(new URL(next + '.json', base), {cache: remote ? 'default' : 'force-cache'});
        if (!response.ok) throw new Error('Catalog unavailable');
        const catalog = await response.json();
        if (!catalog || Array.isArray(catalog) || typeof catalog !== 'object' ||
            !Object.values(catalog).every(item => typeof item === 'string')) throw new Error('Invalid catalog');
        cache.set(next, catalog);
      }
      if (currentRequest !== request) return false;
      setBusy(false);
      locale = next; dictionary = cache.get(next); preference = chosen;
      if (persist) {
        try { if (chosen) localStorage.setItem(storageKey, chosen); else localStorage.removeItem(storageKey); } catch (_) {}
      }
      apply();
      return true;
    } catch (_) {
      if (currentRequest !== request) return false;
      setBusy(false);
      // Keep the working language and form values; allow retry from the selector.
      renderPickers();
      document.querySelectorAll('[data-language-status]').forEach(status => {
        status.textContent = t('Could not load this language. Please try again.');
      });
      return false;
    }
  }
  const nameOf = code => languages.find(([value]) => value === code)[1];
  const svg = (path, stroke = '1.8') => `<svg viewBox="0 0 20 20" aria-hidden="true" focusable="false" stroke-width="${stroke}">${path}</svg>`;
  const globe = svg('<circle cx="10" cy="10" r="7.25"/><path d="M2.75 10h14.5M10 2.75c2 2 3 4.4 3 7.25s-1 5.25-3 7.25c-2-2-3-4.4-3-7.25s1-5.25 3-7.25Z"/>', '1.9');
  const chevron = svg('<path d="m6 8 4 4 4-4"/>', '2');
  const check = svg('<path d="m5 10.5 3.2 3L15 6.5"/>', '2.2');
  function setBusy(busy) {
    document.querySelectorAll('.language-picker').forEach(picker => {
      picker.classList.toggle('is-loading', busy);
      picker.querySelector('.language-trigger').setAttribute('aria-busy', String(busy));
    });
  }
  // The selector is drawn in each language's own name, so anyone can find theirs.
  function renderPickers() {
    document.querySelectorAll('.language-picker').forEach(picker => {
      const trigger = picker.querySelector('.language-trigger');
      trigger.querySelector('.language-trigger-name').textContent = nameOf(locale);
      trigger.querySelector('.language-trigger-code').textContent = locale.toUpperCase();
      trigger.setAttribute('aria-label', t('Language') + ': ' + nameOf(locale));
      trigger.title = t('Language');
      picker.querySelector('.language-menu').setAttribute('aria-label', t('Language'));
      picker.querySelector('.language-menu-heading').textContent = t('Language');
      picker.querySelectorAll('[data-language-option]').forEach(item => {
        const optionVal = item.dataset.languageOption;
        const selected = optionVal === (preference || 'auto');
        item.setAttribute('aria-checked', String(selected));
        const isActiveLanguage = optionVal === locale;
        item.classList.toggle('is-current-locale', isActiveLanguage);
        if (optionVal === 'auto') {
          item.querySelector('.language-option-name > span').textContent = t('Use browser language');
          item.querySelector('.language-option-detail').textContent = nameOf(browserLanguage());
        }
      });
    });
  }
  function closeMenu(picker, restoreFocus = false) {
    const menu = picker.querySelector('.language-menu');
    if (menu.hidden) return;
    menu.hidden = true;
    menu.style.removeProperty('--language-menu-shift');
    picker.querySelector('.language-trigger').setAttribute('aria-expanded', 'false');
    if (restoreFocus) picker.querySelector('.language-trigger').focus();
  }
  function openMenu(picker, focus = 'checked') {
    document.querySelectorAll('.language-picker').forEach(other => { if (other !== picker) closeMenu(other); });
    const menu = picker.querySelector('.language-menu');
    menu.hidden = false;
    picker.querySelector('.language-trigger').setAttribute('aria-expanded', 'true');
    // Keep the panel on screen when the trigger sits near a viewport edge.
    const bounds = menu.getBoundingClientRect(), gutter = 12;
    const shift = bounds.left < gutter ? gutter - bounds.left
      : bounds.right > innerWidth - gutter ? innerWidth - gutter - bounds.right : 0;
    if (shift) menu.style.setProperty('--language-menu-shift', shift + 'px');
    const items = [...menu.querySelectorAll('[role="menuitemradio"]')];
    const target = focus === 'last' ? items[items.length - 1]
      : focus === 'first' ? items[0] : items.find(item => item.getAttribute('aria-checked') === 'true') || items[0];
    target.focus();
  }
  function choose(picker, value) {
    picker.querySelector('[data-language-status]').textContent = '';
    closeMenu(picker, true);
    setLanguage(value);
  }
  function build(host) {
    const id = 'language-menu-' + Math.random().toString(36).slice(2, 8);
    const picker = document.createElement('div');
    picker.className = 'language-picker';
    const trigger = document.createElement('button');
    trigger.type = 'button'; trigger.className = 'language-trigger'; trigger.dataset.languageTrigger = '';
    trigger.setAttribute('aria-haspopup', 'menu'); trigger.setAttribute('aria-expanded', 'false'); trigger.setAttribute('aria-controls', id);
    trigger.innerHTML = `<span class="language-globe">${globe}</span><span class="language-trigger-name"></span><span class="language-trigger-code"></span><span class="language-chevron">${chevron}</span>`;
    const menu = document.createElement('div');
    menu.id = id; menu.className = 'language-menu'; menu.setAttribute('role', 'menu'); menu.hidden = true;
    const heading = document.createElement('div');
    heading.className = 'language-menu-heading'; heading.setAttribute('role', 'presentation');
    const option = (value, name, detail) => {
      const item = document.createElement('button');
      item.type = 'button'; item.className = 'language-option'; item.tabIndex = -1;
      item.setAttribute('role', 'menuitemradio'); item.dataset.languageOption = value;
      item.innerHTML = `<span class="language-option-name"></span><span class="language-option-detail"></span><span class="language-check">${check}</span>`;
      // Isolate each name in its own script direction; the row layout follows the page.
      const label = document.createElement(value === 'auto' ? 'span' : 'bdi');
      if (value !== 'auto') label.lang = value;
      label.textContent = name;
      item.querySelector('.language-option-name').append(label);
      item.querySelector('.language-option-detail').textContent = detail;
      item.addEventListener('click', () => choose(picker, value));
      return item;
    };
    const list = document.createElement('div');
    list.className = 'language-options'; list.setAttribute('role', 'group');
    languages.forEach(([code, name]) => list.append(option(code, name, code.toUpperCase())));
    const separator = document.createElement('div');
    separator.className = 'language-separator'; separator.setAttribute('role', 'separator');
    menu.append(heading, option('auto', 'Use browser language', ''), separator, list);
    const status = document.createElement('span');
    status.dataset.languageStatus = ''; status.className = 'language-status'; status.setAttribute('role', 'status');
    trigger.addEventListener('click', () => menu.hidden ? openMenu(picker) : closeMenu(picker));
    trigger.addEventListener('keydown', event => {
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault(); openMenu(picker, event.key === 'ArrowUp' ? 'last' : 'checked');
      }
    });
    menu.addEventListener('keydown', event => {
      const items = [...menu.querySelectorAll('[role="menuitemradio"]')];
      const index = items.indexOf(document.activeElement);
      const rtl = document.documentElement.dir === 'rtl';
      const step = {ArrowDown: 1, ArrowUp: -1, ArrowRight: rtl ? -1 : 1, ArrowLeft: rtl ? 1 : -1}[event.key];
      if (step) items[(index + step + items.length) % items.length].focus();
      else if (event.key === 'Home') items[0].focus();
      else if (event.key === 'End') items[items.length - 1].focus();
      else if (event.key === 'Escape') closeMenu(picker, true);
      else if (event.key === 'Tab') { closeMenu(picker); return; }
      else return;
      event.preventDefault();
    });
    picker.append(trigger, menu, status);
    host.append(picker);
  }
  function mount() {
    document.querySelectorAll('[data-language-picker]').forEach(build);
    document.addEventListener('pointerdown', event => {
      document.querySelectorAll('.language-picker').forEach(picker => { if (!picker.contains(event.target)) closeMenu(picker); });
    });
    window.addEventListener('resize', () => document.querySelectorAll('.language-picker').forEach(picker => closeMenu(picker)));
    renderPickers();
    setLanguage(preference, false);
  }
  window.I18n = {t, text, message, setLanguage, languages, key, get language() { return locale; }};
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
  else mount();
  window.addEventListener('languagechange', () => { if (!preference) setLanguage('', false); });
})();
