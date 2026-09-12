# Marketing site analytics

The public homepage and FAQ use PostHog. Device dashboards and credentials are outside this integration. Mixpanel has been removed; historical Mixpanel data is not migrated.

## Activate

Edit `js/analytics-config.js` with the **public project token** (`phc_…`) from PostHog project settings. Never use a personal API key. The configured production project is **Default project (334814), US Cloud**, verified in PostHog project settings. Setting the token to an empty string disables all analytics requests.

US Cloud uses `https://us.i.posthog.com` and asset host `https://us-assets.i.posthog.com`. EU Cloud uses `https://eu.i.posthog.com` and `https://eu-assets.i.posthog.com`. For self-hosting, configure both ingestion and SDK asset hosts. Production hosts are explicitly allowed; add a preview hostname locally only when testing with a separate test project.

Configuration follows [PostHog's JavaScript SDK documentation](https://posthog.com/docs/libraries/js/config). We disable autocapture, replay, profiles, surveys, exception capture, heatmaps, and performance capture. Anonymous IDs persist in localStorage. SDK URL/referrer properties (including initial and session-entry keys in `properties`, `$set`, and `$set_once`, including nested person maps) have query strings/fragments stripped before sending, and campaign/referrer persistence is disabled. No input values, FAQ text, device identifiers, glucose readings, or credentials are explicitly collected.

## Event contract (schema_version = 2)

Every event includes `page` (home/faq), `web_serial_supported`, and `secure_context`, plus PostHog's standard browser/session metadata.

| Event | Meaning / additional properties |
| --- | --- |
| `$pageview` | Once per page load; replaces Mixpanel `page_viewed` |
| `section_viewed` | First intersection of 15% of why/process/install per page load; `section` |
| `install_cta_clicked` | Link to install section; `location` |
| `firmware_install_clicked` | Browser install button click; `method=web` |
| `installer_download_clicked` | Click to releases page from Mac app CTA; `method=mac`, `location` |
| `purchase_link_clicked` | Hardware vendor link; `vendor` |
| `demo_video_requested` | Demo thumbnail click; `video_id`; replaces `youtube_video_played` |
| `github_viewed` | Repository link; `location` |
| `faq_link_clicked` | FAQ link; `location` |
| `faq_answer_opened` | Each closed-to-open action; stable `question_id` |
| `support_link_clicked` | Issue tracker link; `location` |

A browser installer click is **not a flash start or success**. The former `firmware_flash_started` event overstated intent. ESP Web Tools v10 does not expose a documented install lifecycle on its install button; this integration does not inspect its private dialog internals. Download and video events similarly measure requests, not completion. Navigation is never delayed; events during SDK loading are buffered (up to 100), but very early exits, blockers, or network failures may lose events. Deferred loading also means a click before the analytics script executes is not captured; the queue only covers interactions after the listener attaches. This is an accepted tradeoff for nonblocking page parsing.

Add `data-track-event` to clickable markup and optional `data-track-location`, `data-track-vendor`, `data-track-method`. The demo event reads the same `data-video-id` used for playback. Other attributes and DOM text are not collected. Keep names and properties static. FAQ IDs should remain stable when question wording changes.

## Suggested PostHog insights

- Funnel: `$pageview` → `section_viewed` filtered to install → `firmware_install_clicked`; break down by Web Serial support.
- Separate funnel for `installer_download_clicked`, since it measures a different installation route.
- CTA clicks by location; purchase clicks by vendor; popular FAQ answers by question_id.
- Browser install intent / unique homepage visitors. Do not label this an installation success rate.

## Validate before merging

Run `node --test tests/test_site_analytics.cjs` from the repository root; the dedicated `site` CI job also runs this suite with Node 22. Both analytics scripts are deferred in document order, keeping configuration in one shared file without blocking parsing. Configure a test project and allow a local hostname, serve `docs` with `python3 -m http.server --directory docs 8000`, then check PostHog live events while opening both pages, clicking each CTA, opening/closing FAQs, and scrolling through sections. Expect one pageview per load and one section event per section per load. Verify nested icon clicks and keyboard activation. Block the SDK request and confirm navigation, video, FAQs, and the installer remain usable. Restore production config before committing.

The project token and US region have been configured. Live browser ingestion still needs verification before merging. Existing anonymous Mixpanel IDs are not transferred, so visitor counts restart at migration.

### URL payload verification

SDK source review confirms that `capture` can attach top-level `$set_once` before `before_send`. In the reviewed SDK, automatic initial/session person properties are skipped when person processing is disabled; sanitization nevertheless covers these maps defensively. Regression tests include `$session_entry_url`, `$initial_current_url`, and session referrers in all supported maps. This source review and synthetic payload coverage do not replace a live ingestion check.

Before merging, use a synthetic landing URL such as `?privacy_probe=test-only&utm_source=probe-only#test-only` in the browser test above. Inspect the actual event in PostHog's event debugger, including `$set_once` and initial/session URL fields, and confirm that no query or fragment sentinel reaches any URL property. Also inspect all event/person property maps for `utm` keys, including `utm_source`, `$utm_source`, `$initial_utm_source`, and `$session_entry_utm_source`, and record whether any appear. The URL sanitizer does not remove standalone campaign properties; preventing SDK campaign persistence relies on `save_campaign_params: false`, which must be checked against the actual SDK payload. Do not put real sensitive data in the test URL.
