# Marketing content review — September 12, 2026

Reviewed merged PRs #20–28, #30, #32, and #33 against current `main`, published GitHub releases, the installer metadata, and the public homepage/FAQ. README changes cover the new user-facing behavior. Website changes below are recommendations; no site content or deployment was changed in this review.

## Fix before promoting the new features

| Location | Finding | Recommended change |
| --- | --- | --- |
| `docs/index.html`: hero, compatibility row, setup steps, install section | Libre is advertised, but the bundled installer is v0.2.2. Latest stable v0.2.6 also predates Libre, seven companions, the dashboard redesign, and the clock mmol/L fix. | Add a visible version/availability note beside Install. Until a release containing these changes is published and installer artifacts are refreshed, label them as available in latest source, not included in the current installer. Follow the existing tested-candidate promotion process before refreshing binaries and hashes. |
| `docs/index.html`: Download Mac App | Links to `/releases/latest`, but v0.2.6 has no DMG. | Supply a verified DMG download when one is published; meanwhile explain the browser-install route instead of promising an app download. |
| `docs/index.html` and `docs/faq.html`: Nightscout claims | `src/http_client.cpp` expects an object containing `glucose`, `timestamp`, and `trend`, with optional Bearer authentication. A standard Nightscout URL/entries response is not supported directly. The Mac Nightscout option only saves the site root. | Say “Custom JSON endpoint; Nightscout requires an adapter” and link to the README's format explanation. Avoid “any Nightscout-compatible setup.” |
| `docs/faq.html`: Libre compatibility | Sensor-model lists imply broader guarantees than the follower integration itself establishes. | Explain that SugarClock reads data shared to a LibreLinkUp follower account through an unofficial API. Include person selection, account-action recovery, and the firmware availability requirement. |

Evidence: [stable v0.2.6 assets](https://github.com/cdemeke/SugarClock/releases/tag/v0.2.6), [installer metadata](installer-artifacts.json), [Libre PR #33](https://github.com/cdemeke/SugarClock/pull/33), and [custom URL parser](../src/http_client.cpp). The source `VERSION` is 0.2.7; that label alone does not prove a release contains these merges.

## Showcase what users can do

Add a short feature section to the homepage using existing artwork, with availability labels until the corresponding release ships:

- **Choose a glucose-aware pixel pet.** Seven characters, pet + text, pet + range icon, or pet alone. Explain that urgent readings replace the pet with the number and missing/stale warnings take priority. Use [pixel-companions.png](images/pixel-companions.png). PRs #25, #27, #28.
- **See your clock from your browser.** Live LED mirror plus a separate latest glucose reading, delta, and age, even while a different view plays. Mention mobile layout and light/dark mode. Use [configuration-display-pet.png](images/configuration-display-pet.png) for the settings preview; capture a dedicated dashboard image for the live mirror. PR #30.
- **Use your preferred units.** mg/dL or one-decimal mmol/L on the clock and dashboard, including deltas. PR #32 and #30. Avoid implying this fix is already in v0.2.6.
- **Keep firmware current over WiFi.** Signed updates, automatic-install controls, and rollback after failed startup validation. Explain that older layouts require a one-time USB migration. PR #21; this capability is already in stable releases.

## Make setup and FAQ more useful

Add answers covering the middle-button double-click shortcut (#26), gray stale readings and their configurable color (#20), Libre person selection (#33), companion styles (#28), and where to find update controls (#21). Explain that the redesigned settings hide buzzer controls while retaining existing settings; avoid presenting the quiet buzzer as a prominent feature (#30).

The repository FAQ already documents school/WPA2-Enterprise setup, but the homepage does not highlight it. Link to that answer rather than duplicating the detailed instructions. Treat remote fleet management (#22–24) as an optional administrator feature requiring a deployed service and approved enrollment, not a standard consumer cloud dashboard.

After publishing, verify the homepage, FAQ, installer manifest, firmware assets, and download targets together. The public FAQ response retrieved during this review lagged the Libre wording already present in `main`; confirm deployment/cache state before announcing availability.
