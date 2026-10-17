# Website and admin languages

The marketing home page and FAQ, device configuration/dashboard/diagnostics/notification pages, and fleet administration use the same local translation runtime. English remains the source language and the fallback. Browser language preferences are checked in order; regional preferences such as `es-MX`, `pt-BR`, and `ar-EG` match their supported base language. A selection in the header takes precedence and is saved per origin. “Use browser language” restores automatic selection. Marketing, local-device, and fleet hosts intentionally have separate browser storage.

The 20 written-language locales are English, Mandarin Chinese (Simplified), Hindi, Spanish, standard written Arabic, French, Bengali, Portuguese, Russian, Urdu, Indonesian, German, Japanese, Marathi, Vietnamese, Telugu, Swahili, Hausa, Turkish, and Tamil. This is a broad total-speaker-oriented set rather than an exact reproduction of a ranking that counts Arabic dialects or Nigerian Pidgin separately. The [total-speaker ranking](https://en.wikipedia.org/wiki/List_of_languages_by_total_number_of_speakers) explains those distinctions. Arabic covers the standard written interface used across Arabic-speaking regions. Arabic and Urdu set the document direction to right-to-left.

Translations were generated with Google Translate neural machine translation during development, with AI review and corrections for interface copy and protected product/provider names. They are checked into the repository; visitors never send text to a translation service. These are machine translations, not a claim of professional native-language review of every locale.

## Boundaries and behavior

- Translate authored headings, help text, control labels, accessibility labels, placeholders, validation/confirmation copy, and action feedback.
- Keep input values, credentials, SSIDs, nicknames, notifications, readings, units, version strings, URLs, API property names, and enum values intact. External error/detail messages and reported device data remain as provided.
- Changing language updates text in place, preserving unsaved forms, focus, selected tabs, and action handlers. A failed catalog request keeps the current language and shows a retryable message. A slow older request cannot override a newer selection.
- Templates interpolate values as text, never HTML. RTL values are isolated from surrounding copy. Physical clock previews, technical inputs, brightness scales, and numeric data retain their order.
- English content is readable before translations load and when JavaScript is disabled. The custom selector requires JavaScript. Third-party installer dialogs, video content, developer demo studio, firmware pixel text, and native applications are outside this change.

## Editing translations

`web/i18n/i18n.js` and `i18n.css` are the shared source. `sources.json` inventories English copy by surface. `locales/*.json` contains editable English-keyed translations. Static HTML uses explicit `data-i18n` spans or attributes so live data is never swept into a page-wide translation pass. Dynamic UI uses `I18n.text(element, copy)` or `I18n.message(copy, values)`; pass `false` as the third `I18n.text` argument for data. `fleet.element` translates only an explicit message object.

When adding copy, add its English key to the relevant surface and each catalog. Preserve numbered `{0}` parameters, product names, API syntax, and units. Use whole sentences and complete singular/plural variants rather than assembling translated suffixes. Then run:

```sh
python3 scripts/build_i18n.py
python3 scripts/pre_build.py
python3 -m unittest discover -s tests
npm ci --prefix tests/browser
npx --prefix tests/browser playwright install chromium
npm test --prefix tests/browser
pio run
pio run -t buildfs
python3 scripts/check_layout.py
```

The build script creates compact surface-specific catalogs, copies the shared runtime/styles, and synchronizes the installer’s device UI snapshot. Device catalogs are embedded directly into firmware at `/locales/*.json`, separately from the LittleFS overlay, so they work offline and update atomically with firmware. The firmware asset generator sorts directories and uses deterministic gzip. Rebuilding these checked-in assets must leave no diff.

Set `CAPTURE_SCREENSHOTS=1` when running browser tests to refresh `docs/pr/languages/`. Browser tests use simulated device responses and a disposable fleet database; no production device receives commands. The browser tests cover all supported languages, desktop and mobile layouts, fallback, persistence, failed loads, race handling, unchanged saves, navigation, search, and CSRF-protected actions. Host tests validate translation coverage, parameter preservation, product names, bundle synchronization, and embedded assets.
