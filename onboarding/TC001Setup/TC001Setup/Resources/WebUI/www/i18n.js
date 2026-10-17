/* Shared, offline-capable UI localization. Only authored copy is translated. */
(function () {
  'use strict';
  const script = document.currentScript;
  const base = new URL('locales/', script.src);
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
    document.querySelectorAll('[data-language-select]').forEach(select => {
      select.value = preference || 'auto';
      select.setAttribute('aria-label', t('Language'));
      select.options[0].textContent = languages.find(([code]) => code === locale)[1] + ' · ' + t('Use browser language');
    });
    document.dispatchEvent(new CustomEvent('languagechange', {detail: {language: locale}}));
  }
  async function setLanguage(value, persist = true) {
    const chosen = supported.has(value) ? value : '';
    const next = chosen || browserLanguage();
    const currentRequest = ++request;
    try {
      if (!cache.has(next)) {
        const response = await fetch(new URL(next + '.json', base), {cache: 'force-cache'});
        if (!response.ok) throw new Error('Catalog unavailable');
        const catalog = await response.json();
        if (!catalog || Array.isArray(catalog) || typeof catalog !== 'object' ||
            !Object.values(catalog).every(item => typeof item === 'string')) throw new Error('Invalid catalog');
        cache.set(next, catalog);
      }
      if (currentRequest !== request) return false;
      locale = next; dictionary = cache.get(next); preference = chosen;
      if (persist) {
        try { if (chosen) localStorage.setItem(storageKey, chosen); else localStorage.removeItem(storageKey); } catch (_) {}
      }
      apply();
      return true;
    } catch (_) {
      if (currentRequest !== request) return false;
      // Keep the working language and form values; allow retry from the selector.
      document.querySelectorAll('[data-language-select]').forEach(select => { select.value = locale; });
      document.querySelectorAll('[data-language-status]').forEach(status => {
        status.textContent = t('Could not load this language. Please try again.');
      });
      return false;
    }
  }
  function mount() {
    document.querySelectorAll('[data-language-picker]').forEach(host => {
      const label = document.createElement('label');
      label.className = 'language-picker';
      const caption = document.createElement('span');
      caption.dataset.i18n = 'Language'; caption.textContent = 'Language';
      const select = document.createElement('select');
      select.dataset.languageSelect = ''; select.dir = 'ltr';
      select.append(new Option('Use browser language', 'auto'));
      languages.forEach(([code, name]) => {
        const option = new Option(name, code); option.lang = code; select.append(option);
      });
      const status = document.createElement('span');
      status.dataset.languageStatus = ''; status.className = 'language-status'; status.setAttribute('role', 'status');
      select.addEventListener('change', () => { status.textContent = ''; setLanguage(select.value); });
      label.append(caption, select); host.append(label, status);
    });
    setLanguage(preference, false);
  }
  window.I18n = {t, text, message, setLanguage, languages, key, get language() { return locale; }};
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
  else mount();
  window.addEventListener('languagechange', () => { if (!preference) setLanguage('', false); });
})();
