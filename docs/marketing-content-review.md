# Marketing content review — September 12, 2026 (historical)

This records the September 12 review of merged PRs #20–28, #30, #32, and #33 against the repository, published releases, installer metadata, and public homepage/FAQ **at that time**. Its original findings are historical context, not a current publishing checklist. No website content, installer binaries, or deployment is changed by this document.

## Status rechecked November 8, 2026

Use these verified updates instead of the old release-dependent recommendations:

| Topic | Current evidence and guidance |
| --- | --- |
| Released features | [v0.2.13 release notes](https://github.com/cdemeke/SugarClock/releases/tag/v0.2.13) include LibreLinkUp (#33), seven companions (#28), the dashboard redesign (#30), and the mmol/L fix (#32). The [browser manifest](manifest.json) and [installer artifact metadata](installer-artifacts.json) now specify **v0.2.15**; the Mac app's bundled firmware matches it. These features are included in those installer images. **Do not label them source-only or wait for their first release.** |
| Release selection | The newest published non-prerelease is [v0.2.18](https://github.com/cdemeke/SugarClock/releases/tag/v0.2.18), published October 25. GitHub's [`/releases/latest`](https://github.com/cdemeke/SugarClock/releases/latest) currently selects v0.2.13. Use explicit tags and verify installer metadata separately; `VERSION` on `main` is not proof of distribution. |
| Mac download | Neither v0.2.18 nor the release currently selected by `/releases/latest` contains a DMG. The repository homepage still labels that URL **Download Mac App**. Replace that promise with a verified DMG when available, or explain the browser-install/source-build route. The [README](../README.md#quick-start) now gives the latter route. |
| Connection documentation | [PR #80](https://github.com/cdemeke/SugarClock/pull/80) merged November 8 and added a [guide hub](guides.html), [Dexcom guide](dexcom-blood-sugar-clock.html), [LibreLinkUp guide](freestyle-libre-display.html), and [Nightscout adapter guide](nightscout-clock.html). Preserve these guides and the README's adapter requirement rather than duplicating them or assuming raw Nightscout entries work. Repository content alone does not prove the public deployment is current. |
| Newer source capabilities | Preserve the current README's safety warning, stale-data/low-glucose behavior, setup and iPhone-companion requirements, plus the site's localization. Do not advertise unreleased Bluetooth firmware as part of v0.2.15 or infer iOS distribution from a source merge. Recheck actual releases, hardware qualification, and distribution status before changing availability copy. |

Installer verification for this refresh compared the committed binary hashes against `installer-artifacts.json` and the v0.2.15 release assets. The Mac setup app bundles the same firmware binary; it constructs its filesystem during setup. This checks artifact identity, not physical installation or CGM behavior.

## Original findings — September 12 only

| Location reviewed | Finding at the time | Original intent and subsequent status |
| --- | --- | --- |
| `docs/index.html`: hero, compatibility row, setup steps, install section | Libre was advertised while bundled firmware was v0.2.2 and stable firmware was v0.2.6, both predating Libre, seven companions, the dashboard redesign, and the mmol/L fix. | Make install availability visible and promote tested firmware before advertising it as installed. The source-only restriction from this review is superseded by v0.2.13 and the v0.2.15 installer refresh. Continue using the tested-candidate process for future binary/hash updates. |
| `docs/index.html`: Download Mac App | `/releases/latest` selected v0.2.6, with no DMG. | Point users to an actual downloadable artifact or explain another installation route. See the current Mac-download finding above. |
| `docs/index.html` and `docs/faq.html`: Nightscout claims | The custom parser expected a `glucose`/`timestamp`/`trend` object, with optional Bearer authentication; a standard Nightscout root/entries response did not work directly. The Mac Nightscout option saved the site root. | Explain the adapter requirement rather than promising “any Nightscout-compatible setup.” The current README and connection guides now explain this; [the custom parser](../src/http_client.cpp) remains the implementation reference. |
| `docs/faq.html`: Libre compatibility | Sensor-model lists implied guarantees beyond the follower integration. | Explain the unofficial LibreLinkUp follower API, person selection, and account-action recovery; avoid implying direct sensor connectivity. Preserve the newer setup/retry details and connection guide. |

Historical evidence: [v0.2.6 assets](https://github.com/cdemeke/SugarClock/releases/tag/v0.2.6), [the installer-refresh PR #61](https://github.com/cdemeke/SugarClock/pull/61), and [Libre PR #33](https://github.com/cdemeke/SugarClock/pull/33). The source version was 0.2.7 on September 12; that label did not establish which merges were released.

## Showcase ideas to re-evaluate for future copy

These ideas came from the original review. Check the current site and released behavior before scheduling more content; the listed feature PRs are evidence, not an outstanding implementation checklist.

- **Choose a glucose-aware pixel companion.** Seven characters, companion + text, companion + range icon, or companion alone. Explain that urgent readings replace the companion with the number and missing/stale warnings take priority. Existing artwork: [pixel-companions.png](images/pixel-companions.png). PRs #25, #27, #28.
- **See your clock from your browser.** Live LED mirror plus a separate latest glucose reading, delta, and age, even while a different view plays. Mention mobile layout and light/dark mode. [configuration-display-pet.png](images/configuration-display-pet.png) is a settings preview; use a current running dashboard capture when illustrating the mirror. PR #30.
- **Use your preferred units.** mg/dL or one-decimal mmol/L on the clock and dashboard, including deltas. PRs #32 and #30; included in v0.2.13 and v0.2.15 installers.
- **Keep firmware current over WiFi.** Signed updates, automatic-install controls, and rollback after failed startup validation. Older layouts require a one-time USB migration; preserve the current [migration guidance](BLE_MIGRATION.md). PR #21.

Useful setup/FAQ topics include the middle-button double-click address shortcut (#26), Libre person selection (#33), companion styles (#28), update controls (#21), and school/WPA2-Enterprise setup. Link existing answers and guides instead of duplicating them. The original gray stale-reading description is superseded: current source defaults to dim purple and uses a delivery allowance; preserve the README's current timing and safety caveats.

For fleet capabilities and reporting/privacy behavior, use the current [fleet documentation](../fleet/README.md); the original review predates later enrollment and reporting changes. For any future site update, preserve translations, safety notices, and current setup behavior. After publishing, verify the public homepage, FAQ, guides, installer manifest, firmware assets, and download targets together before announcing availability.
