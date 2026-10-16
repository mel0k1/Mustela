# Engine gap analysis

A second full review of the codebase at v0.8.0: core, both shells, build and CI. The goal is a single honest list of what the engine is still missing, grouped by area, with priorities and a suggested milestone order. Items marked (port) are per-port work; items marked (core) belong in `src/core` and become public API for Kiwi.

## Method

Every source file was re-read against two questions: does the engine expose this capability, and does the shell apply it? Absence was verified by searching both shells for the relevant WebKit surface (GTK signals, WinCairo C API clients) and confirming it is not wired. Nothing below is speculative: each gap names the exact API that is currently unused.

## What exists today

- Core: TOML settings with a load report (schema version, applied/unknown/invalid key accounting), privacy policy with per-site rules, permission policy with per-site grants, fingerprint budget, tracking blocklist, DoH wire codec, DoH resolver and local proxy engine, bundled public-suffix list guarding site rules, session store, history, browsing-data kinds, URL normalization, perf module (budgets, sampling, stats, JSONL log), `ui_text` sanitization for dialog text, find queries and download names. 468-check test suite.
- GTK shell: tabs, URL bar with search fallback, private toggle, history window, site-rules window, clear-data, settings/site-rules hot-reload, startup budget, memory ceiling, process model, popups as tabs, native JS dialogs, find-in-page, status link preview, downloads, zoom, permission prompts, TLS certificate prompt, HTTP auth prompt, crash recovery, Mustela context menu, fullscreen (F11 + HTML5), session restore, GPC script, tracker blocklist filter, engine routing through the local proxy (blocking + DoH + upstream).
- Win shell: same feature set on the WinCairo C API minus the site-rules window (edit `sites.toml`, hot-reload applies), popups via `WKPageUIClient` V6, dialog listeners, `WKPageFindClient` find bar, link preview, downloads, zoom, permission prompts, HTTP auth prompt, crash recovery, Mustela context menu, F11 fullscreen, session restore, GPC script, tracker blocklist filter, engine routing through the local proxy via environment variables (WinHTTP DoH transport).
- Bench: `mustela-bench` (GTK, runs in CI under xvfb) and `mustela-bench-win` (WinCairo, hidden host window; compiled in CI, executed on demand).
- CI: three green tracks — Linux full build + tests + bench, Windows core tests, Windows shell and bench compile against 88 pinned upstream headers.

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

- The memory ceiling only warns. There is no tab discarding or per-view suspend; WebKitGTK and the WinCairo C API expose no per-view suspend today. Possible core-side mitigation: track per-tab idle/load state and close the oldest inactive tab above the ceiling, behind a `perf.memory_action` switch. (core + ports)
- `perf.process_model` is applied at startup only. A settings change requires a restart; hot-reload does not re-apply it. (ports)
- Bench coverage. `mustela-bench` runs in CI; `mustela-bench-win` compiles in CI but cannot run — GitHub Windows runners have no WinCairo runtime DLLs. Options: a release job that downloads a WinCairo runtime build, or a self-hosted Windows runner. Until then Windows numbers come from manual runs.
- Startup budget has no cold/warm distinction; first run after install (profile creation, filter compilation) is measured against the same number as a warm start. (core)
- `perf_check.py` compares medians only, with tolerance plus slack; no trend tracking, no per-run scatter. Fine for now, revisit when baselines accumulate. (tooling)

## API gaps for Kiwi (priority 2 for K1)

- The core is a static library with headers that double as implementation detail. Kiwi needs a declared public surface: a `include/mustela/` set with `settings`, `privacy`, `site_rules`, `fingerprint`, `browsing_data`, `perf`, `history`, `url`, versioned independently. (build)
- No profile object. The profile directory is implicit; a `Profile` handle (path, data kinds, per-profile privacy policy) is the natural Kiwi API and maps 1:1 to both WebKit data managers. (core)
- `resolve_site` returns JavaScript/cookies/fingerprint decisions only. Per-site zoom, user-agent override and CSS belong next to it, backed by the same `SiteRules` store. (core)
- ~~Settings have no schema version and no unknown-key reporting; a malformed `settings.toml` loads silently with defaults for missing keys. A `load_report` return would let Kiwi surface it.~~ — done in M8: `load_settings` fills a `SettingsReport` (schema `version`, applied/unknown/invalid keys, warnings for newer files), saved settings carry `version = 1`, and both shells surface the report. (core)

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
| Permission prompts: notifications | yes | auto-deny (no C API call) |
| TLS certificate prompt | yes (allow once) | no (C API limit; auto-reject) |
| HTTP auth prompt | yes | yes |
| Web process crash recovery | yes | yes |
| Fullscreen (F11) | yes | yes |
| HTML5 element fullscreen | yes | no (C API limit; GTK signals only) |
| Mustela context menu | yes | yes |
| Session restore | yes (full history) | yes (tab URLs) |
| GPC (Sec-GPC header + JS) | yes (web extension) | JS only (no C API header surface) |
| Tracker blocklist | yes | yes |
| Local proxy: network blocking + DoH | yes (`proxy.mode`) | yes (env hook, startup only) |
| Bench | GTK, runs in CI | WinCairo, compile-checked in CI |

## Build and CI gaps

- No release job: no Linux package (deb/AppImage/Flatpak) and no Windows bundle (exe + required WebKit DLLs). The WinCairo runtime requirements are the main unknown for a Windows release.
- No sanitizer runs (ASan/UBSan on the core suite) and no static analysis (clang-tidy) in CI. The core is small; both are cheap wins.
- The Windows header pin (WebKit 8ece203, 86 headers) floats conceptually over an unpinned runtime; a release must pin both.
- MinGW is a verified local toolchain (win32-thread variant; posix threads conflict with `WKBaseWin.h`'s `pid_t`) but CI uses MSVC only. Fine while MSVC is the supported Windows compiler.

## Suggested order

1. M5 — browsing platform basics: shipped in full — popups, JS dialogs, find-in-page, link preview, downloads, zoom, permission policy with per-site memory, certificate/auth surface, crash recovery.
2. M6 — privacy deepening: shipped — GPC (JS both ports, Sec-GPC header on GTK via a web extension), tracking-domain blocklist compiled into content filters on both ports, DoH wire codec + setting in the core, fullscreen, Mustela context menus and session restore on both ports; leftovers (public-suffix guard, settings load report) closed in M8.
3. M7 — local proxy network layer: shipped — loopback proxy engine in the core (CONNECT tunneling, request-level and connection-level blocklist enforcement, upstream chaining, DoH resolver with injected transport), GTK routing through `WebKitNetworkProxySettings`, Windows routing through the curl proxy environment, 386-check suite with loopback integration tests.
4. M8 — settings hygiene and suffix guard: shipped — bundled public-suffix list with a `sites.toml` guard (load skips, save refuses, lookup stops at the effective top level) and a `SettingsReport` load report (schema version, applied/unknown/invalid keys) surfaced by both shells; 468-check suite.
5. K1 — Kiwi consumes the declared public core surface (profiles, per-site policy UI) while the shell harness keeps parity work behind it.
6. Ongoing: release/packaging job as soon as Windows runtime story settles.
