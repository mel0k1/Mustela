# Engine gap analysis

A second full review of the codebase at v0.8.0: core, both shells, build and CI. The goal is a single honest list of what the engine is still missing, grouped by area, with priorities and a suggested milestone order. Items marked (port) are per-port work; items marked (core) belong in `src/core` and become public API for Kiwi.

## Method

Every source file was re-read against two questions: does the engine expose this capability, and does the shell apply it? Absence was verified by searching both shells for the relevant WebKit surface (GTK signals, WinCairo C API clients) and confirming it is not wired. Nothing below is speculative: each gap names the exact API that is currently unused.

## What exists today

- Core: TOML settings, privacy policy with per-site rules, fingerprint budget, history, browsing-data kinds, URL normalization, perf module (budgets, sampling, stats, JSONL log), `ui_text` sanitization for dialog text and find queries. 197-check test suite.
- GTK shell: tabs, URL bar with search fallback, private toggle, history window, site-rules window, clear-data, settings/site-rules hot-reload, startup budget, memory ceiling, process model, popups as tabs, native JS dialogs, find-in-page, status link preview.
- Win shell: same feature set on the WinCairo C API minus the site-rules window (edit `sites.toml`, hot-reload applies), popups via `WKPageUIClient` V6, dialog listeners, `WKPageFindClient` find bar, link preview.
- Bench: `mustela-bench` (GTK, runs in CI under xvfb) and `mustela-bench-win` (WinCairo, hidden host window; compiled in CI, executed on demand).
- CI: three green tracks — Linux full build + tests + bench, Windows core tests, Windows shell and bench compile against 80 pinned upstream headers.

## Browsing platform gaps (priority 1)

These are the capabilities a browser-shaped consumer (Kiwi) will need first. Popups, JS dialogs, find-in-page and status link preview shipped in M5; the rest are absent today.

| Gap | Unused API surface | Notes |
|---|---|---|
| New windows and popups | ~~GTK `create` signal; Win `WKPageUIClient.createNewPage`~~ — done in M5 | `target=_blank` and `window.open` open as tabs; privacy context inherited from the originator |
| JavaScript dialogs | ~~GTK `script-dialog`; Win `WKPageUIClient` alert/confirm/prompt/beforeunload~~ — done in M5 | Native dialogs with sanitized text on both ports |
| Downloads | GTK `download-started`; Win `WKContextDownloadClient` | A download click does nothing today |
| Find in page | ~~GTK `WebKitFindController`; Win `WKPageFindClient`~~ — done in M5 | Ctrl+F bar, next/previous, match counts, wrap-around |
| Zoom | GTK `webkit_web_view_set_zoom_level`; Win `WKPageSetScaleFactor` | No UI or settings key |
| Fullscreen | GTK `run-as-modal`; Win `WKPageFullScreenClient` | HTML5 fullscreen requests fail silently |
| Context menu | GTK `context-menu` signal; Win `WKPageContextMenuClient` | Default menus appear (GTK) or nothing (Win); no Mustela items (open in private tab, copy link) |
| Permissions | GTK `permission-request`; Win `WKUserMediaPermissionRequest` / `WKGeolocationManager` | Geolocation, camera, microphone and notifications are auto-denied or unset — no policy surface, no per-site memory |
| Certificate and HTTP auth errors | GTK `accept-certificate` / `authenticate`; Win `didReceiveAuthenticationChallenge` | TLS failures just fail the load; no allow-once/allow-always decision, no credential prompt |
| Web process crash | GTK `web-process-terminated`; Win `webProcessDidCrash` | A crashed tab shows a blank view; no reload affordance or event for Kiwi |
| Session restore | (core) | Tabs are not persisted across restarts; no session file format in the core |
| Status link preview | ~~GTK `mouse-target-changed`; Win `mouseDidMoveOverElement`/`setStatusText`~~ — done in M5 | Hover URL in the status bar on both ports |

## Privacy engine gaps (priority 2)

The privacy core is the product's identity; these are the next honest wins.

- Global Privacy Control. Neither port sends a `Sec-GPC` header nor exposes `navigator.globalPrivacyControl`. WebKitGTK allows request headers per context; the C API allows custom user scripts for the JS side. A `privacy.gpc` switch is cheap and visible. (core policy + both ports)
- Tracking-domain blocklist. Both ports already run compiled content filters (fonts on GTK, `WKUserContentExtensionStore` on Win). Shipping a privacy blocklist (tracker domains, fingerprinting endpoints) as data in the core, compiled per profile, reuses that machinery end to end. (core data + both ports)
- DNS-over-HTTPS. `dns_prefetching` is handled, but there is no secure-DNS setting exposed on either port. (port)
- Proxy. No proxy settings surface despite both WebKit flavors supporting one. (port)
- Public suffix awareness in `SiteRules`. `host_from_uri` does plain suffix matching: a rule for `example.org` correctly covers `www.example.org`, but a rule for a registry suffix (e.g. `co.uk`) would over-match. The core needs a bundled public-suffix list or a conservative "no registry-suffix rules" guard. (core)
- Per-origin cookie wipe parity. GTK wipes cookies for a denied origin; the WinCairo C API has no per-origin removal, so Windows denial rides the global accept policy. Recheck upstream: if `WKWebsiteDataStoreRemoveData` grows origin filtering, port it. (port, upstream-dependent)
- Disk cache clearing on Windows. `ClearRequest` covers cookies and storage on Win but not the network cache; the C API lacks the call. (port, upstream-dependent)
- ITP visibility. ITP is enabled but nothing reports what it did; no counters, no per-site view. WebKitGTK offers no API either — document as accepted limitation. (core/docs)
- Fingerprint leftovers, already documented in PRIVACY.md and still true: main-world user scripts are detectable, canvas is blanked rather than noised, no font-enumeration randomization, no timezone/screen spoofing, referrer meta-injection loses to CSP. (core, accepted budget)

## Performance gaps (priority 3)

Leftovers from M4 plus what the bench variant opened up.

- The memory ceiling only warns. There is no tab discarding or per-view suspend; WebKitGTK and the WinCairo C API expose no per-view suspend today. Possible core-side mitigation: track per-tab idle/load state and close the oldest inactive tab above the ceiling, behind a `perf.memory_action` switch. (core + ports)
- `perf.process_model` is applied at startup only. A settings change requires a restart; hot-reload does not re-apply it. (ports)
- Bench coverage. `mustela-bench` runs in CI; `mustela-bench-win` compiles in CI but cannot run — GitHub Windows runners have no WinCairo runtime DLLs. Options: a release job that downloads a WinCairo runtime build, or a self-hosted Windows runner. Until then Windows numbers come from manual runs.
- Startup budget has no cold/warm distinction; first run after install (profile creation, filter compilation) is measured against the same number as a warm start. (core)
- `perf_check.py` compares medians only, with tolerance plus slack; no trend tracking, no per-run scatter. Fine for now, revisit when baselines accumulate. (tooling)

## API gaps for Kiwi (priority 2 for K1)

- The core is a static library with headers that double as implementation detail. Kiwi needs a declared public surface: a `include/mustela/` set with `settings`, `privacy`, `site_rules`, `fingerprint`, `browsing_data`, `perf`, `history`, `url`, versioned independently. (build)
- No profile object. The profile directory is implicit; a `Profile` handle (path, data kinds, per-profile privacy policy) is the natural Kiwi API and maps 1:1 to both WebKit data managers. (core)
- `resolve_site` returns JavaScript/cookies/fingerprint decisions only. Per-site zoom, user-agent override and CSS belong next to it, backed by the same `SiteRules` store. (core)
- Settings have no schema version and no unknown-key reporting; a malformed `settings.toml` loads silently with defaults for missing keys. A `load_report` return would let Kiwi surface it. (core)

## Platform parity matrix

| Capability | GTK | Win |
|---|---|---|
| Tabs, URL bar, search fallback, shortcuts | yes | yes |
| Private tabs, privacy toggle | yes | yes |
| Cookie policy, ITP, hardening | yes | yes |
| Per-site rules: JS, cookies, fingerprint | yes | yes (via `sites.toml`) |
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
| Bench | GTK, runs in CI | WinCairo, compile-checked in CI |

## Build and CI gaps

- No release job: no Linux package (deb/AppImage/Flatpak) and no Windows bundle (exe + required WebKit DLLs). The WinCairo runtime requirements are the main unknown for a Windows release.
- No sanitizer runs (ASan/UBSan on the core suite) and no static analysis (clang-tidy) in CI. The core is small; both are cheap wins.
- The Windows header pin (WebKit 8ece203) floats conceptually over an unpinned runtime; a release must pin both.
- MinGW is a verified local toolchain (win32-thread variant; posix threads conflict with `WKBaseWin.h`'s `pid_t`) but CI uses MSVC only. Fine while MSVC is the supported Windows compiler.

## Suggested order

1. M5 — browsing platform basics: popups, JS dialogs and find-in-page shipped; the remaining items are downloads, zoom, certificate/auth surface, crash recovery, permissions policy. Mostly port work with thin core additions (permission policy, session types).
2. M6 — privacy deepening: GPC, tracking-domain blocklist, secure DNS, public-suffix guard in site rules, settings load report.
3. K1 — Kiwi consumes the declared public core surface (profiles, per-site policy UI) while the shell harness keeps parity work behind it.
4. Ongoing: release/packaging job as soon as Windows runtime story settles.
