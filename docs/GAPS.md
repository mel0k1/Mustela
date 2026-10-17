# Engine gap analysis

A full review of the codebase, started at v0.8.0 and refreshed as the third review at v0.12.0: core, both shells, build and CI. The goal is a single honest list of what the engine is still missing, grouped by area, with priorities and a suggested milestone order. Items marked (port) are per-port work; items marked (core) belong in `src/core` and become public API for Kiwi.

## Method

Every source file was re-read against two questions: does the engine expose this capability, and does the shell apply it? Absence was verified by searching both shells for the relevant WebKit surface (GTK signals, WinCairo C API clients) and confirming it is not wired. Nothing below is speculative: each gap names the exact API that is currently unused.

## What exists today

- Core: TOML settings with a load report (schema version, applied/unknown/invalid key accounting), privacy policy with per-site rules, permission policy with per-site grants, fingerprint budget, tracking blocklist, DoH wire codec, DoH resolver and local proxy engine, the full official public-suffix list guarding site rules, session store, history, bookmark store, branded error-page builder, browsing-data kinds, URL normalization, perf module (budgets, sampling, stats, JSONL log), `ui_text` sanitization for dialog text, find queries and download names, a `Profile` handle (standard data-dir layout, per-profile policy, file-backed store clearing) with a `ProfileRegistry` over `profiles.toml`, search engines with keyword shortcuts (`resolve_entry`), an `AutofillStore` for form values (with per-domain lookup and the fill/capture script builders), per-site zoom/UA/CSS in `SiteRules` flowing through `SiteDecision`, and a declared public surface under `include/mustela/` (umbrella `mustela.hpp`, `MUSTELA_CORE_API_VERSION`, installable with the static core). 702-check test suite.
- GTK shell: tabs (with favicons, drag-reorderable), URL bar with keyword-shortcut search pipeline, drop-to-navigate on the window, private toggle, history window, bookmarks window, site-rules window (with per-site dressing editing), clear-data, settings/site-rules hot-reload, startup budget, memory ceiling, process model, popups as tabs, native JS dialogs, find-in-page, status link preview, downloads, zoom, permission prompts, TLS certificate prompt, HTTP auth prompt, crash recovery, Mustela context menu, fullscreen (F11 + HTML5), session restore, GPC script, tracker blocklist filter, engine routing through the local proxy (blocking + DoH + upstream), devtools, branded error pages, desktop web notifications through GNotification, print dialog, page screenshots, save page as MHTML, spellchecking, profile selection through `profiles.toml`, per-site zoom/UA/CSS applied at commit, form autofill fill-on-load and capture-on-submit.
- Win shell: same feature set on the WinCairo C API minus the site-rules window (edit `sites.toml`, hot-reload applies) and favicons (no C API surface), popups via `WKPageUIClient` V6, dialog listeners, `WKPageFindClient` find bar, link preview, downloads, zoom, permission prompts, HTTP auth prompt, crash recovery, Mustela context menu, F11 fullscreen, session restore, GPC script, tracker blocklist filter, engine routing through the local proxy via environment variables (WinHTTP DoH transport), devtools, branded error pages, bookmarks.
- Bench: `mustela-bench` (GTK, runs in CI under xvfb) and `mustela-bench-win` (WinCairo, hidden host window; compiled in CI, executed on demand).
- View surface for embedders (V1, v0.19.0): `include/mustela/view.hpp` — a port-neutral API with zero WebKit types (`ViewSession` owning the context pair, `View` per web view, session-level download callbacks, view-level navigation/loading/dialog/permission/popup callbacks, `native_widget()` handing back the `GtkWidget*`) — implemented by `libmustela_view_gtk.a` (`src/view/gtk/view_gtk.cpp`, `MUSTELA_BUILD_VIEW`): context-level privacy (cookie policy, ITP, TLS, proxy with DoH, Sec-GPC web extension, fonts/blocklist filters), per-view policy application at commit (hardening, JS, fingerprint payloads, per-site zoom/UA/CSS dressing), branded error pages, per-origin cookie wipe, favicon persistence, crash recovery and popups as related views; a `mustela_view_smoke` ctest runs under xvfb in CI. V2 (v0.20.0) grows the privacy-center surface: `ViewSession::fetch_website_data` (per-origin stored-data enumeration over `WebKitWebsiteDataManager`), `remove_website_data` (origins × `DataKind`) and `clear_website_data` (kinds × age), plus `View::active_site_policy()` reporting the dressing actually resolved for the view's current origin; view API version 1 → 2.
- CI: three green tracks — Linux full build + tests + bench, Windows core tests, Windows shell and bench compile against 88 pinned upstream headers — plus a strict ASan/UBSan track, an advisory clang-tidy track and a tag-triggered release job (deb + tarball via CPack).

## Third review: what separates Mustela from a real engine (v0.12.0)

The web platform itself is not the gap: rendering, CSS, JavaScript, media, storage and service workers are inherited whole from WebKit, and both ports already route every request through the engine's own privacy layer. What is missing is the browser-platform dressing around that core — the features users and embedders expect from a finished engine. Verified absences, by area:

**Browser features (user-visible):**

- ~~Bookmarks.~~ Nothing in the core or either shell... — done in M9: `bookmarks.hpp` ships a `BookmarkStore` (TSV, escaped fields, URL de-duplication) with a toolbar star (Ctrl+D) and a bookmarks window (Ctrl+Shift+O) on both ports. (core + ports)
- ~~Favicons.~~ GTK carries `webkit_web_context_get_favicon_database`... — done in M9 on GTK: the favicon database is enabled per profile, tabs show icons through `notify::favicon`, and non-private tabs persist them as PNGs under the profile; the WinCairo C API has no favicon surface, so Windows stays text-only. (port, GTK-only)
- ~~Devtools / Web Inspector.~~ — done in M9: GTK toggles the inspector through `webkit_web_view_get_inspector` (Ctrl+Shift+I/F12, `shell.devtools` switch); Windows shows it through `WKPageGetInspector` + `WKPageShowWebInspectorForTesting` (one-way — the pin has no `WKInspectorShow`/`IsVisible`). (ports)
- ~~Branded error pages.~~ — done in M9: the core builds the page (`error_page.hpp`, sanitized + HTML-escaped heading/detail/URL with a Try-again action), GTK loads it with `webkit_web_view_load_alternate_html`, Windows with `WKPageLoadHTMLString`; cancellations are skipped and the error page itself never reaches history/perf. (core + ports)
- ~~Printing.~~ GTK `WebKitPrintOperation` — done in M10: Ctrl+P runs the GTK print dialog against the active view; the pinned WinCairo C API has no print surface. (port, GTK-only)
- ~~Screenshots.~~ GTK `webkit_web_view_get_snapshot` — done in M10: Ctrl+Alt+S saves the visible region as a timestamped PNG into the downloads directory; the pinned WinCairo headers lack `WKPageGetSnapshot`. (port, GTK-only today)
- ~~Save page (MHTML).~~ GTK `webkit_web_view_save_to_file` — done in M10.5: Ctrl+S opens a native save dialog (default name from the sanitized page title, fallback to the host) and writes the page through `WEBKIT_SAVE_MODE_MHTML`. (port, GTK-only)
- ~~Spellchecking.~~ GTK `webkit_web_context_set_spell_checking_enabled`/`_languages` — done in M10.5: `shell.spellcheck` enables the checker on both contexts, `shell.spellcheck_languages` takes a comma-separated dictionary list; dictionaries come from the system enchant setup. (port, GTK-only)
- ~~Web notification display.~~ The GTK shell resolved the `notifications` permission but never handled `webkit_notification` — done in M10: the `show-notification` signal renders through `GNotification` on a registered `GApplication`, titles and bodies sanitized; the permission gate stays the M5 policy. Win already auto-denies (no C API decision call). (port)
- ~~Download pause/resume.~~ No resume surface on either port (GTK 4.1 has none; `WKDownloadResume` is absent from the pin — re-verified against the 2.54 header set in M11). Accepted upstream limitation. (ports, upstream-dependent)
- ~~Drag and drop.~~ No DnD wiring — done in M11 on GTK: the shell window accepts `text/uri-list` and `text/plain` drops (links, plain text and files resolve through the same entry pipeline as the URL bar and navigate the active tab), and notebook tabs are drag-reorderable with the tab vector resynced on `page-reordered`; drops onto the page area stay owned by WebKit's internal DnD (editable-field drags keep working). Windows has no drag surface in the C API pin. (port, GTK-only)
- ~~Form autofill and passwords.~~ Nothing exists — done in M11 (core storage): `autofill.hpp` ships an `AutofillStore` (TSV under the profile as `autofill.tsv`, escaped fields, domain + field + value entries with use counters and timestamps, subdomain-aware lookup ordered by use/recency) exposed through `ProfileStore::Autofill` and the public surface. M12 closed the Kiwi-side UI in the reference harness: the GTK shell fills visible form fields on load (`shell.autofill`, off by default) and captures submitted non-password fields (`shell.autofill_save`, off by default) through a script-message handler; private tabs never fill or capture, and the same core surface (`by_domain`, `autofill_fill_script`, `autofill_capture_script`) is what Kiwi consumes. Passwords deliberately stay out of the core — plaintext secrets are a non-goal; Kiwi is expected to use the OS keyring. (core + GTK harness done)
- ~~Multiple profiles and search engines.~~ One implicit default profile; one search template with no keyword shortcuts — done in M11: `ProfileRegistry` reads/writes `profiles.toml` (id, display name, root, default id; missing file falls back to the single default layout) and the GTK harness resolves its data dir through it, selected by the `shell.profile` setting; search grows `[search.engines]` templates with per-engine keywords (`!g query`), `search.default` and `resolve_entry` as the single entry-pipeline function used by both shells. (core)

**Engine core (embedder-facing):**

- ~~The bundled public-suffix list is a compact subset (~230 rules: common gTLDs, popular ccTLD second levels, classic wildcards and exceptions).~~ — done in M10: the core embeds the full official PSL (10,080 exact, 311 wildcard, 8 exception rules, Punycode-normalized, refreshed by `scripts/gen_psl.py`), so `registrable_domain` is spec-accurate for every registry. (core)
- ~~The K1 list stands unchanged: declared public headers, a `Profile` handle, per-site zoom/UA/CSS next to `resolve_site`.~~ — done in K1: the declared surface lives under `include/mustela/` (umbrella `mustela.hpp` over `settings`/`privacy`/`permissions`/`site_rules`/`fingerprint`/`browsing_data`/`perf`/`history`/`url`/`bookmarks`/`profile` plus `version.hpp` with `MUSTELA_CORE_API_VERSION`), CMake installs the headers with the static core; `profile.hpp` ships the `Profile` handle (root + standard layout `history.dat`/`bookmarks.tsv`/`session.json`/`perf.jsonl`/`filters`/`favicons`, per-profile `PrivacyPolicy`, `ensure_dirs`, `has`/`clear` per `ProfileStore`); `SitePolicy` grows `zoom` (clamped 0.3–3.0), `user_agent` and `css`, all parsed, saved and tested through the same `sites.toml` store.
- Settings now carry a schema `version` and a load report; the migration path itself is still unwritten (nothing to migrate yet).

**Upstream-dependent leftovers (unchanged):** per-origin cookie wipe and disk cache clear on Windows, ITP visibility, HTML5 element fullscreen on Win, `Sec-GPC` header on Win (no request-header surface in the C API).

**Performance and CI leftovers (unchanged):** process-model hot-reload, cold/warm startup distinction, executing `mustela-bench-win` (needs WinCairo runtime). ~~Tab discarding above the memory ceiling~~ — done in M13. Sanitizers (ASan/UBSan strict, clang-tidy advisory) and the Linux release/packaging job shipped in M12.

## Browsing platform gaps (priority 1)

These are the capabilities a browser-shaped consumer (Kiwi) will need first. Popups, JS dialogs, find-in-page, status link preview, downloads, zoom, permissions, certificate/auth and crash recovery shipped in M5; fullscreen, the context menu and session restore shipped in M6; the rest are absent today.

| Gap | Unused API surface | Notes |
|---|---|---|
| New windows and popups | ~~GTK `create` signal; Win `WKPageUIClient.createNewPage`~~ — done in M5 | `target=_blank` and `window.open` open as tabs; privacy context inherited from the originator |
| JavaScript dialogs | ~~GTK `script-dialog`; Win `WKPageUIClient` alert/confirm/prompt/beforeunload~~ — done in M5 | Native dialogs with sanitized text on both ports |
| Downloads | ~~GTK `download-started`; Win `WKContextDownloadClient`~~ — done in M5 | Destination from `downloads.dir`, ask mode, status-bar progress |
| Find in page | ~~GTK `WebKitFindController`; Win `WKPageFindClient`~~ — done in M5 | Ctrl+F bar, next/previous, match counts, wrap-around |
| Zoom | ~~GTK `webkit_web_view_set_zoom_level`; Win `WKPageSetScaleFactor`~~ — done in M5 | Ctrl+plus/minus/0, `shell.zoom` default |
| Fullscreen | ~~GTK `enter-fullscreen`/`leave-fullscreen`; Win F11 window fullscreen~~ — done in M6 | HTML5 element fullscreen works on GTK; the pinned WinCairo C API exposes no production fullscreen client, so element requests stay GTK-only |
| Context menu | ~~GTK `context-menu` signal; Win `WKPageContextMenuClient` V2~~ — done in M6 | Mustela-built menus on both ports: navigation, link/image/media sections by hit-test, editing actions |
| Permissions | ~~GTK `permission-request`; Win geolocation/usermedia in `WKPageUIClient` V6~~ — done in M5 | Global `permissions.*` choices + per-site `permissions` rule; GTK adds notifications; Win notifications stay auto-denied (no C API decision call) |
| Certificate and HTTP auth errors | ~~GTK `accept-certificate` / `authenticate`; Win `didReceiveAuthenticationChallenge`~~ — done in M5 | GTK: allow-once cert prompt + credential prompt; Win: credential prompt, server-trust auto-rejected (C API has no cert acceptance) |
| Web process crash | ~~GTK `web-process-terminated`; Win `webProcessDidTerminate`~~ — done in M5 | Two guarded auto-reloads, reason in the status bar |
| Session restore | ~~(core) session file + GTK `WebKitWebViewSessionState` + Win back/forward URLs~~ — done in M6 | Tabs reopen after a restart; GTK restores full back/forward history, Windows restores tab URLs (no C API session-state restore); private tabs never saved |
| Status link preview | ~~GTK `mouse-target-changed`; Win `mouseDidMoveOverElement`/`setStatusText`~~ — done in M5 | Hover URL in the status bar on both ports |
| View: fullscreen requests | ~~GTK `enter-fullscreen`/`leave-fullscreen`~~ — done in V4 (view API) | The view surface forwards element fullscreen requests to the embedder; the harness fullscreens its window, Kiwi does the same; WinCairo C API has no client (F11 only) |

## Privacy engine gaps (priority 2)

The privacy core is the product's identity; these are the next honest wins.

- Global Privacy Control. ~~Neither port sends a `Sec-GPC` header nor exposes `navigator.globalPrivacyControl`.~~ — done in M6: `privacy.gpc` (on by default) injects the JS property through a document-start user script on both ports, and a GTK web-process extension (`src/webext/gtk`) adds `Sec-GPC: 1` to every request; the WinCairo C API has no request-header surface, so Windows ships the JS side only.
- Tracking-domain blocklist. ~~Both ports already run compiled content filters...~~ — done in M6: `blocklist.hpp` ships ~60 built-in tracker domains plus an optional `privacy.blocklist_file`, compiled per profile through the existing content-filter machinery on both ports.
- DNS-over-HTTPS. The core carries an RFC 8484 codec (`doh.hpp`) and now a runtime resolver (`DohResolver` in `proxy_server.hpp`): when the engine routes through the local proxy (`proxy.mode = "local"`) and `privacy.dns_over_https` is set, every name the proxy connects to resolves through the configured DoH server — TLS to the resolver comes from the port transport (libsoup on GTK, WinHTTP on Windows), failures fail closed, and results are TTL-cached. Content-filter-based blocking additionally stays active on both ports. — done in M7
- Proxy. ~~No proxy settings surface despite both WebKit flavors supporting one.~~ — done in M7: the core ships a loopback proxy engine (`proxy.mode = "local"`) and upstream chaining (`proxy.mode = "upstream"`, `proxy.upstream` with optional Basic auth); GTK applies it through `webkit_website_data_manager_set_network_proxy_settings`, Windows through the curl backend's proxy environment variables.
- Public suffix awareness in `SiteRules`. ~~`host_from_uri` does plain suffix matching: a rule for `example.org` correctly covers `www.example.org`, but a rule for a registry suffix (e.g. `co.uk`) would over-match. The core needs a bundled public-suffix list or a conservative "no registry-suffix rules" guard.~~ — done in M8: the core bundles a compact public-suffix list (`psl.hpp`, wildcard and exception rules included); registry-suffix sections in `sites.toml` are skipped on load, `set()` refuses them, and rule lookup stops climbing at the effective top level. IP literals and `localhost` stay addressable. (core)
- Per-origin cookie wipe parity. GTK wipes cookies for a denied origin; the WinCairo C API has no per-origin removal, so Windows denial rides the global accept policy. Recheck upstream: if `WKWebsiteDataStoreRemoveData` grows origin filtering, port it. (port, upstream-dependent)
- Disk cache clearing on Windows. `ClearRequest` covers cookies and storage on Win but not the network cache; the C API lacks the call. (port, upstream-dependent)
- ITP visibility. ITP is enabled but nothing reports what it did; no counters, no per-site view. WebKitGTK offers no API either — document as accepted limitation. (core/docs)
- Fingerprint leftovers, already documented in PRIVACY.md and still true: main-world user scripts are detectable, canvas is blanked rather than noised, no font-enumeration randomization, no timezone/screen spoofing, referrer meta-injection loses to CSP. (core, accepted budget)

## Performance gaps (priority 3)

Leftovers from M4 plus what the bench variant opened up.

- ~~The memory ceiling only warns. There is no tab discarding or per-view suspend; WebKitGTK and the WinCairo C API expose no per-view suspend today. Possible core-side mitigation: track per-tab idle/load state and close the oldest inactive tab above the ceiling, behind a `perf.memory_action` switch.~~ — done in M13: a core policy (`discard.hpp`) picks the least-recently-active, non-loading, 10+-minutes-idle background tab when the ceiling is exceeded and `perf.memory_action = "discard"`; both ports destroy the view (session state captured first), keep the tab in the strip and rebuild it with its history on reactivation. Per-view suspend remains impossible upstream — discard is destroy-and-recreate. (core + ports)
- `perf.process_model` is applied at startup only. A settings change requires a restart; hot-reload does not re-apply it. (ports)
- Bench coverage. `mustela-bench` runs in CI; `mustela-bench-win` compiles in CI but cannot run — GitHub Windows runners have no WinCairo runtime DLLs. `scripts/fetch-win-runtime.ps1` now assembles a runnable runtime from the two upstream channels (WebKitRequirementsWin64.zip + the Buildbot archive of `Windows-64-bit-Release-Build`), so manual runs and a self-hosted runner are one script away; a release bundle job would call the same script.
- Startup budget has no cold/warm distinction; first run after install (profile creation, filter compilation) is measured against the same number as a warm start. (core)
- `perf_check.py` compares medians only, with tolerance plus slack; no trend tracking, no per-run scatter. Fine for now, revisit when baselines accumulate. (tooling)

## API gaps for Kiwi (priority 2 for K1)

- ~~The core is a static library with headers that double as implementation detail. Kiwi needs a declared public surface: a `include/mustela/` set with `settings`, `privacy`, `site_rules`, `fingerprint`, `browsing_data`, `perf`, `history`, `url`, versioned independently. (build)~~ — done in K1, see above; consuming needs only `-Iinclude` + a link against `libmustela_core`.
- ~~No profile object. The profile directory is implicit; a `Profile` handle (path, data kinds, per-profile privacy policy) is the natural Kiwi API and maps 1:1 to both WebKit data managers. (core)~~ — done in K1: `Profile` covers the path layout and the file-backed stores; cookies/cache/storage stay port-owned (WebKit data managers), so ports keep their `ClearRequest` mapping on top.
- ~~`resolve_site` returns JavaScript/cookies/fingerprint decisions only. Per-site zoom, user-agent override and CSS belong next to it, backed by the same `SiteRules` store. (core)~~ — done in K1: the fields live on `SitePolicy` (wholesale closest-rule matching, same as the policy fields). M12 finished the loop: `SiteDecision` carries the dressing and the GTK harness applies zoom (per-site override, else the tab default), user-agent (override with fallback to the captured default) and CSS (a user style sheet) at commit; the site-rules window edits all three. The Kiwi embedder gets the identical one-call surface.
- ~~Settings have no schema version and no unknown-key reporting; a malformed `settings.toml` loads silently with defaults for missing keys. A `load_report` return would let Kiwi surface it.~~ — done in M8: `load_settings` fills a `SettingsReport` (schema `version`, applied/unknown/invalid keys, warnings for newer files), saved settings carry `version = 1`, and both shells surface the report. (core)
- ~~Kiwi must never touch WebKit, but the public surface is data/policy only — a browser UI needs a web view, and that capability has to land in the engine first.~~ — done in V1 (v0.19.0): `include/mustela/view.hpp` + `libmustela_view_gtk.a` give the embedder session/view objects with the full privacy pipeline and zero WebKit types; Kiwi builds its UI on GTK alone and links the engine. (build + gtk)

## Platform parity matrix

| Capability | GTK | Win |
|---|---|---|
| Tabs, URL bar, search fallback, shortcuts | yes | yes |
| Private tabs, privacy toggle | yes | yes |
| Cookie policy, ITP, hardening | yes | yes |
| Per-site rules: JS, cookies, fingerprint, permissions | yes | yes (via `sites.toml`) |
| Site-rules window | yes | no (file editing + hot-reload) |
| History, clear-data | yes | yes |
| Per-origin cookie wipe on deny | yes | no (C API limit) |
| Disk cache clear | yes | no (C API limit) |
| Startup budget, memory ceiling, process model | yes | yes |
| Page-load timing, perf log | yes | yes |
| Popups / target=_blank as tabs | yes | yes |
| JS dialogs (alert/confirm/prompt/beforeunload) | yes | yes |
| Find in page (Ctrl+F) | yes | yes |
| Status link preview | yes | yes |
| Downloads (auto dir / ask) | yes | yes |
| Zoom (Ctrl+plus/minus/0, 30–300%) | yes | yes |
| Permission prompts: geolocation, camera, mic | yes | yes |
| Permission prompts: notifications | yes (shown via GNotification) | auto-deny (no C API call) |
| Print dialog (Ctrl+P) | yes | no (C API limit) |
| Page screenshot (Ctrl+Alt+S) | yes | no (C API limit) |
| Save page (Ctrl+S, MHTML) | yes | no (C API limit) |
| Spellchecking | yes (system enchant dictionaries) | no (C API limit) |
| Drag & drop: drop-to-navigate, tab reorder | yes (drop on chrome area) | no (C API limit) |
| Search engines + `!keyword` shortcuts | yes | yes |
| Profile registry (`profiles.toml`) | yes | yes (core, no UI switch) |
| Per-site zoom/UA/CSS dressing | yes (applied at commit, editable) | no (sites.toml + hot-reload only) |
| Form autofill fill/capture | yes (`shell.autofill`, `shell.autofill_save`) | no (core storage only) |
| TLS certificate prompt | yes (allow once) | no (C API limit; auto-reject) |
| HTTP auth prompt | yes | yes |
| Web process crash recovery | yes | yes |
| Fullscreen (F11) | yes | yes |
| HTML5 element fullscreen | yes | no (C API limit; GTK signals only) |
| Mustela context menu | yes | yes |
| Devtools (Web Inspector) | yes (toggle) | yes (show) |
| Branded error pages | yes | yes |
| Bookmarks (star + window) | yes | yes |
| Bookmark import from other browsers | yes (core importer) | yes (core importer) |
| Favicons in tabs | yes | no (C API limit) |
| Session restore | yes (full history) | yes (tab URLs) |
| Tab discarding under the memory ceiling | yes (full history on restore) | yes (tab URL on restore) |
| GPC (Sec-GPC header + JS) | yes (web extension) | JS only (no C API header surface) |
| Tracker blocklist | yes | yes |
| Local proxy: network blocking + DoH | yes (`proxy.mode`) | yes (env hook, startup only) |
| Bench | GTK, runs in CI | WinCairo, compile-checked in CI |

## Build and CI gaps

- ~~No release job.~~ — done in M12 for Linux: a tag-triggered release job builds the full GTK tree, runs ctest, packages a deb (shlib-deps generated) and a tarball through CPack and attaches them to the GitHub Release; the Windows bundle (exe + WebKit DLLs) stays unbundled until a WinCairo runtime source is pinned — the release notes say so explicitly. The runtime side is now scripted (`scripts/fetch-win-runtime.ps1`); the bundle job itself is the M14 deliverable together with the Windows view library.
- ~~No sanitizer runs.~~ — done in M12: the core test suite runs under ASan + UBSan (strict, leak detection on) in its own CI track; clang-tidy runs as an advisory track (findings reported, not enforced) until they are burned down.
- The Windows header pin (WebKit 8ece203, 93 headers) floats conceptually over an unpinned runtime; a Windows release must pin both. The Linux release is not affected (distro WebKitGTK).
- MinGW is a verified local toolchain (win32-thread variant; posix threads conflict with `WKBaseWin.h`'s `pid_t`) but CI uses MSVC only. Fine while MSVC is the supported Windows compiler.

## Suggested order

1. M5 — browsing platform basics: shipped in full — popups, JS dialogs, find-in-page, link preview, downloads, zoom, permission policy with per-site memory, certificate/auth surface, crash recovery.
2. M6 — privacy deepening: shipped — GPC (JS both ports, Sec-GPC header on GTK via a web extension), tracking-domain blocklist compiled into content filters on both ports, DoH wire codec + setting in the core, fullscreen, Mustela context menus and session restore on both ports; leftovers (public-suffix guard, settings load report) closed in M8.
3. M7 — local proxy network layer: shipped — loopback proxy engine in the core (CONNECT tunneling, request-level and connection-level blocklist enforcement, upstream chaining, DoH resolver with injected transport), GTK routing through `WebKitNetworkProxySettings`, Windows routing through the curl proxy environment, 386-check suite with loopback integration tests.
4. M8 — settings hygiene and suffix guard: shipped — bundled public-suffix list with a `sites.toml` guard (load skips, save refuses, lookup stops at the effective top level) and a `SettingsReport` load report (schema version, applied/unknown/invalid keys) surfaced by both shells; 468-check suite.
5. M9 — browser dressing: shipped — devtools on both ports (`shell.devtools`, toggle on GTK, show on Windows), branded error pages built by the core and rendered through `load_alternate_html`/`WKPageLoadHTMLString`, a core `BookmarkStore` with toolbar star and bookmarks window on both ports, favicons on GTK through the favicon database; 513-check suite.
6. M10 — platform dressing II: shipped — web notifications on GTK through `GNotification`, printing (Ctrl+P) and screenshots (Ctrl+Alt+S) on GTK, the full official public-suffix list in the core; the M10.5 follow-up closed the remaining GTK candidates: save page (Ctrl+S, MHTML) and spellchecking.
7. K1 — Kiwi consumes the declared public core surface: shipped — `include/mustela/` umbrella + independent API version, installable core, `Profile` handle, per-site zoom/UA/CSS; the GTK harness builds its profile paths through `Profile`.
8. M11 — remaining GAPS leftovers: shipped — search engines with keyword shortcuts and `resolve_entry` (both shells), `ProfileRegistry` over `profiles.toml` with `shell.profile` selection (GTK wired, core API), `AutofillStore` for form values in the profile, GTK drag & drop (window drop-to-navigate, tab drag-reorder); download pause/resume re-confirmed as an upstream absence.
9. M12 — CI levers and harness closure (v0.18.0): shipped — ASan/UBSan track on the core suite (strict) plus an advisory clang-tidy track, a tag-triggered release job packaging the Linux deb + tarball through CPack, per-site zoom/UA/CSS applied by the GTK harness at commit (with a dressing editor in the site-rules window) and the autofill fill-on-load / capture-on-submit loop behind `shell.autofill` / `shell.autofill_save`; 702-check suite; API version 2 → 3.
10. V1 — embedding view surface (v0.19.0): shipped — `include/mustela/view.hpp` (port-neutral, zero WebKit types) with `libmustela_view_gtk.a` implementing context-level privacy, per-view dressing, error pages, downloads, dialogs, permissions, popups and crash recovery; `mustela_view_smoke` in ctest under xvfb.
11. V2 — privacy-center view surface (v0.20.0): shipped — `fetch_website_data` / `remove_website_data` / `clear_website_data` on `ViewSession` (per-origin enumeration, targeted removal, aged clear through the WebKit data manager) and `View::active_site_policy()` for embedder indicators; smoke coverage extended.
12. Kiwi K2 — the privacy center (Kiwi 0.2.0 on the v0.20.0 surface): shipped — per-site override editor with a live toolbar indicator (`View::active_site_policy`) and stored-data management screens (`fetch`/`remove`/`clear_website_data`).
13. Kiwi K3 — library and settings (Kiwi 0.3.0): shipped — a bookmarks manager over the core `BookmarkStore`, a search engines editor over `Settings.search_engines` with `resolve_entry` `!keyword` shortcuts, and a profile manager over `ProfileRegistry` (per-profile data roots, restart-safe switching) — pure embedder work, zero engine changes. Remaining engine leftovers after M13 (bench-win execution, Windows runtime pin, process-model hot-reload, cold/warm startup) stay documented here.
14. V3 — view slice 2 (v0.21.0, view API 2 → 3): shipped — find-in-page (`View::find`/`find_next`/`find_previous`/`find_finish`/`find_text` over `WebKitFindController`, `find_result` callback), devtools (`View::show_devtools`/`hide_devtools`/`devtools_attached` docking `WebKitWebInspector` with developer extras enabled on demand), and the context menu surface (`ViewCallbacks::context_menu` with `ContextMenuInfo` hit-test flags, returning true suppresses the default menu) plus action backing for embedder menus (`View::execute_edit_command`, `View::selected_text` through `evaluate_javascript`); smoke coverage extended to find hit/miss, selected-text callback, devtools dock/close and context-menu interception. Kiwi 0.4.0 layers the MD3 find bar, the MD3 context menu and a devtools toggle on top.
15. M13 — tab discarding (v0.22.0): shipped — a core discard policy (`src/core/discard.hpp`): when the process-tree RSS exceeds `perf.memory_ceiling_mb` and `perf.memory_action = "discard"`, the least-recently-active, non-loading, 10+-minutes-idle background tab is discarded — both ports destroy the live view (GTK replaces the notebook page with a placeholder, Windows releases the WKView/WKPage) after capturing the session state, keep the tab in the strip, and rebuild it through the normal tab-creation path with the captured back/forward history restored when the user returns; session save/restore treats discarded tabs as URLs so they survive a restart; 730-check suite (discard policy unit tests included).
16. K2 leftover — import from other browsers (v0.24.0): shipped bookmark import — `src/core/import.hpp`: Chromium-family `Bookmarks` JSON (Chrome/Chromium/Edge/Brave/Vivaldi/Opera profile detection on Linux and Windows), Firefox `bookmarkbackups/*.jsonlz4` (built-in mozLZ4 decoder, newest backup wins) with a `bookmarks.html` fallback, and universal Netscape HTML exports; dedup through `BookmarkStore::contains`, `ImportReport` counts. Deliberately dependency-free: history and password import stay out (places/History are SQLite, passwords are DPAPI/keyring-encrypted) — a future slice can add a SQLite3-gated history importer. Kiwi 0.7.0 surfaces it as a Settings page.
17. M14 — Windows runtime story (v0.24.0): shipped — `src/view/win/view_win.cpp` implements `include/mustela/view.hpp` over the pinned WinCairo C API (navigation, state client, popups, dialogs, find, downloads, geolocation/user-media permissions with site-rule memory, cookie/ITP policy, filters, error pages, custom UA through `WKPageSetCustomUserAgent`, per-view scale). Honest parity notes: `ViewConfig.parent_window` (view API 4 → 5) carries the embedder HWND — on Windows the view parents there or into a hidden helper window; fullscreen/notifications/MHTML/JS-eval/edit-commands/devtools-docking/per-origin-cookie-wipe/website-data-records have no production C API surface in the pin and degrade honestly (no-ops, `save_mhtml` reports failure, `fetch_website_data` returns an empty list, loading is proxied through estimated progress). The tag-triggered `windows-bundle` CI job assembles the runtime (`fetch-win-runtime.ps1`), generates import libraries from the DLLs (`scripts/make-win-import-libs.ps1`: dumpbin → .def → lib.exe), links `mustela.exe` + `mustela_view_win.lib` with MSVC and attaches an experimental zip to the release — the first time linking (not just compiling) is verified off a self-hosted pass.
