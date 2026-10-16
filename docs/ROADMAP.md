# Roadmap

Milestones are capability-based; dates are intentionally not assigned.

## M0 — Bootstrap (current)

- Repository, CMake build, LGPL-2.1 licensing.
- `mustela` shell: GTK port opens a web view, `--private` runs an ephemeral profile.
- Windows port stub, build docs for both platforms.

## M1 — Shell baseline (done)

- Navigation: back, forward, reload, URL bar, loading progress. (done)
- Search fallback for non-URL input. (done)
- Basic tab model: open, close, switch, private tabs behind policy. (done)
- Mustela-owned profile directory, separate from system defaults. (done)
- Settings file (TOML) loaded at startup, saved on exit. (done)

## M1.5 — Engine APIs and shell demos (done)

- History store in the core, private tabs excluded at the policy level. (done)
- Runtime privacy toggle in the shell UI. (done)
- History window in the shell (Ctrl+H) with clear action. (done)
- Settings hot-reload on file change. (moved to M2)

## M2 — Privacy core (done)

- Cookie accept policy per context: `always`, `no-third-party`, `never`. (done)
- Intelligent Tracking Prevention on the persistent profile. (done)
- Hardening switches: JavaScript, hyperlink auditing, DNS prefetching, autoplay gesture. (done)
- Browsing-data kinds (`DataKind`, `ClearRequest`) and a clear action in the shell (Ctrl+Shift+Del). (done)
- Settings hot-reload on file change. (done)
- Per-site overrides: `sites.toml`, JavaScript allow/deny, cookie deny with origin wipe, subdomain matching, `resolve_site` in the core, site-rules window in the shell. (done)

## M2.5 — Fingerprint budget (done)

- Switches for WebGL, canvas readback, remote fonts and referrer trimming, enforced through user scripts and a compiled content filter. (done)
- A measurable fingerprint budget documented in PRIVACY.md: surface-by-surface trade-offs, presets and honest limits. (done)
- Per-site fingerprint rules on top of `SiteRules`: `allow` exempts a host, `deny` forces the strict set. (done)

## M3 — Windows port (done)

- Shell on the upstream WinCairo flavor of WebKit through its C API: tabs, URL bar with search fallback, window geometry persistence. (done)
- Privacy-core parity: cookie accept policy, ITP, hardening switches, per-site overrides, fingerprint budget via user scripts and a compiled content filter, history, clear-browsing-data, settings hot-reload. (done)
- Persistent profile under `%APPDATA%\mustela`, private tabs on non-persistent data stores. (done)
- Windows shell compiled against pinned upstream WebKit C API headers on every push. (done)

Known gaps, tracked for the next pass: per-site cookie denial works through the global accept policy (the C API has no per-origin cookie wipe on Windows), clear-data covers cookies and storage but not the disk cache, and there is no site-rules window — edit `sites.toml`, hot-reload applies it live.

## CI (done)

- Linux: full GTK shell build against `webkit2gtk-4.1` plus the core test suite on every push. (done)
- Windows: core and tests under MSVC. (done)
- Windows shell syntax check against 86 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`). (done)

## M4 — Performance (done)

- Startup budget: time-to-first-map on Linux, time-to-first-paint on Windows, enforced against `perf.startup_budget_ms` and logged. (done)
- Process model tuning: `perf.process_model` (`per-view` or `shared`) wired through `WebKitWebContext` and `WKContextSetProcessModel`. (done)
- Memory ceiling: process-tree RSS sampling every minute (`/proc` on Linux, `GetProcessMemoryInfo` + Toolhelp on Windows), `perf.memory_ceiling_mb` drives a status-bar warning, samples go to the log. (done)
- Page-load timing between provisional start and finish navigation, appended to `perf.jsonl` in the profile. (done)
- `mustela-bench` harness: real loads through a minimal offscreen WebKit shell, JSON stats (min/median/p95/max), budget exit code; runs under `xvfb-run` in CI on every push. (done)
- Regression tracking: `scripts/perf_check.py` summarizes `perf.jsonl` and compares medians against a recorded baseline with tolerance. (done)

Known gaps, tracked for the next pass: the ceiling only warns — there is no tab discarding (WebKitGTK and the WinCairo C API expose no per-view suspend), `perf.process_model` is applied at startup only, and the WinCairo bench compiles in CI but does not run there (GitHub Windows runners ship no WinCairo runtime).

## M4.5 — Windows bench and gap analysis (done)

- `mustela-bench-win`: the same page-load harness on the WinCairo C API — hidden host window, non-persistent data store, identical CLI (`--url/--runs/--max-ms/--out`), identical JSON stats and exit codes. (done)
- Built with the Windows SDK branch in CMake (`mustela-bench-win`) and compile-checked against the pinned upstream headers on every push. (done)
- Second full engine review at v0.8.0 recorded in [GAPS.md](GAPS.md): browsing-platform gaps (popups, dialogs, downloads, find, zoom, permissions, certificates), privacy deepening candidates (GPC, tracking blocklist, secure DNS), performance leftovers and the Kiwi API surface, with a suggested milestone order. (done)

## M5 — Browsing basics (done)

- New windows and popups open as tabs on both ports: GTK `create` signal with related views, Windows `WKPageUIClient` V6 `createNewPage`; privacy context and data store are inherited from the originating tab, `ready-to-show`/`showPage` focuses the popup, JS `window.close()` closes the tab. (done)
- JavaScript dialogs are native: alert, confirm, prompt (with editable default value) and beforeunload confirmation on both ports — GTK `script-dialog`, Windows `WKPageUIClient` async dialog listeners. (done)
- Dialog text, find queries and download file names pass through `mustela::ui_text`: control characters stripped, CR/LF normalized, byte-capped with UTF-8-safe truncation — unit-tested in the core. (done)
- Find in page: find bar (Ctrl+F) with next/previous (Enter/Shift+Enter), match counts, case-insensitive wrap-around search, re-search on tab switch — GTK `WebKitFindController`, Windows `WKPageFindClient` + `WKPageFindString` with the match overlay. (done)
- Status-bar link preview on both ports: GTK `mouse-target-changed`, Windows `mouseDidMoveOverElement`/`setStatusText`. (done)
- Downloads on both ports: GTK `download-started`, Windows `WKContextDownloadClient`; destination resolved from `downloads.dir` (platform Downloads folder by default) with collision-free file names, `downloads.mode = "ask"` opens a save dialog (GtkFileChooser / GetSaveFileName); progress in the status bar, cancel on dialog refusal. (done)
- Zoom on both ports: Ctrl+plus/minus/0 steps 30%–300% — GTK `zoom_level`, Windows `WKPageSetScaleFactor` re-applied on commit; default from `shell.zoom`. (done)
- Permission policy in the core (`permissions.hpp`): global choices `deny`/`ask`/`allow` per permission kind (`permissions.geolocation`, `.camera`, `.microphone`, `.notifications`, all deny by default) resolved against a per-site `permissions` rule in `sites.toml`; prompts offer allow/deny/always/never, "always" and "never" are remembered per host (private tabs never write rules). GTK handles geolocation, camera/microphone and notifications via `permission-request`; Windows covers geolocation and camera/microphone through `WKPageUIClient` V6, notifications stay auto-denied (the pinned C API exposes no per-request decision call). (done)
- Certificate and auth surface: GTK `accept-certificate` prompts before continuing on invalid certificates (strict TLS policy default, `security.tls_strict = false` ignores), `authenticate` prompts for user/password with session credentials; Windows `didReceiveAuthenticationChallenge` (pinned headers extended with the auth/credential set) prompts for basic auth and rejects server-trust challenges — the WinCairo C API has no certificate-acceptance call. (done)
- Web process crash recovery on both ports: GTK `web-process-terminated`, Windows `webProcessDidTerminate`; up to two automatic reloads per tab with a crash-loop guard, status message with the reason, manual Ctrl+R afterwards; the counter resets on a successful load. (done)
- Known gaps kept for later: HTML5 fullscreen, context menu, session restore (see [GAPS.md](GAPS.md)).

## M6 — Privacy deepening and browsing completion (done)

- Global Privacy Control on both ports: `privacy.gpc` (on by default) injects a document-start user script exposing `navigator.globalPrivacyControl`, and a GTK web-process extension (`src/webext/gtk`) adds the `Sec-GPC: 1` header to every request; the WinCairo C API cannot mutate request headers, so Windows ships the JS surface only. (done)
- Tracking-domain blocklist in the core (`blocklist.hpp`): ~60 built-in ad/tracker domains plus an optional `privacy.blocklist_file`, compiled per profile into content rules on both ports (GTK `WebKitUserContentFilterStore`, Windows `WKUserContentExtensionStore`) and enforced through the same user-content controllers as the fingerprint filters; suffix matching, domain validation and rule-set building are unit-tested. (done)
- Secure DNS groundwork in the core (`doh.hpp`): RFC 8484 wire codec — query build, response parse with name compression, A/AAAA/CNAME answers, base64url framing (`query_uri`) — plus the `privacy.dns_over_https` setting; neither WebKit flavor exposes a DoH resolver hook through its public API, so runtime wiring waits for a proxy milestone and the gap stays tracked in [GAPS.md](GAPS.md). (done, runtime deferred)
- Context menu on both ports: GTK `context-menu` handler rebuilds the menu from curated stock actions (navigation, link/image/media sections, editing actions by hit-test); Windows `WKPageContextMenuClient` V2 builds the same menu from `WKContextMenuItem` stock tags with the same hit-test sections. (done)
- Fullscreen on both ports: F11 toggles window fullscreen (GTK `gtk_window_fullscreen`, Windows borderless-on-monitor with placement restore); HTML5 element fullscreen wires through GTK `enter-fullscreen`/`leave-fullscreen` — the pinned WinCairo C API exposes no production fullscreen client, so element requests stay GTK-only. (done)
- Session restore in the core (`session.hpp`): JSON session file with tabs (url, title, history lists, opaque per-port state) and the active index; GTK persists `WebKitWebViewSessionState` blobs and restores full back/forward history per tab, Windows restores tab URLs (the C API has no session-state restore); private tabs are never saved, `shell.session_restore` switches the feature. (done)
- Known gaps kept: public-suffix guard in `SiteRules` and a settings load report (M6 leftovers), per-origin cookie wipe and disk cache clear on Windows (upstream-dependent), secure-DNS runtime hook (blocked on upstream APIs).

## M7 — Local proxy network layer (done)

- Local proxy engine in the core (`proxy_server.hpp`): loopback-only HTTP proxy with CONNECT tunneling and plain-HTTP forwarding; tracker domains are refused at connection setup with 403 (suffix match through the shared blocklist), plain-HTTP requests are additionally checked per request; the engine never intercepts TLS, so tunnel payloads stay opaque to it. (done)
- Upstream chaining: `proxy.mode = "upstream"` routes the engine through an external proxy directly, and `proxy.upstream` (with `user`/`password` for Basic auth) chains the local proxy through it when both are configured. (done)
- DoH resolver in the core (`DohResolver`): the RFC 8484 codec from M6 gets a runtime hook — the transport is injected as a callable, A answers are TTL-cached (30–600 s), IP literals bypass DNS, and a failed DoH lookup fails the connection instead of leaking the name to the system resolver. GTK supplies a libsoup transport, Windows a WinHTTP transport. (done)
- GTK wiring: `proxy.mode = "local"` starts the engine-internal proxy and points both data managers at it via `webkit_website_data_manager_set_network_proxy_settings` (custom mode, loopback bypass list); `proxy.mode = "upstream"` points WebKit at the upstream directly; settings hot-reload restarts the proxy with the new config. (done)
- Windows wiring: `http_proxy`/`https_proxy`/`no_proxy` are set before `WKContext` creation (the WinCairo curl backend reads the environment); DoH goes through WinHTTP; proxy settings apply at startup, a restart is required after changes. (done)
- Test suite 322 → 386 checks: proxy config keys, proxy URL parsing, resolver behavior (cache, failure, NXDOMAIN) and loopback integration tests — blocked CONNECT and GET, subdomain blocking, bidirectional tunnel echo, request forwarding with origin-form rewrite, upstream-chained tunnel and forward, fail-closed 502 on DoH failure. (done)
- Honest limits: HTTPS blocking is connection-level (authority from CONNECT — no URL inspection without interception), plain HTTP blocking is request-level; `upstream`-only mode bypasses the local engine (no blocking, no DoH there); the Windows curl proxy hook is compile-verified but cannot execute in CI (no WinCairo runtime on runners).

## M8 — Settings hygiene and suffix guard (done)

- Public-suffix guard in the core (`psl.hpp`): a compact bundled public-suffix list — common gTLDs, popular ccTLD second levels, wildcard (`*.ck`, `*.kobe.jp`) and exception (`!www.ck`) rules — with `is_public_suffix` and `registrable_domain`. Registry-suffix sections in `sites.toml` are skipped on load, `set()` refuses them and rule lookup stops climbing at the effective top level, so one entry can never cover a whole registry; IP literals and `localhost` remain addressable. (done)
- Settings load report in the core (`SettingsReport`): `load_settings` reports the schema `version` (a file written by a newer engine warns), applied/unknown/invalid key counts and the offending keys themselves; `save_settings` stamps `version = 1`; GTK prints issues on stdout, Windows through the debug output — on startup and on every hot-reload. (done)
- Test suite 386 → 468 checks: PSL matching (exact, wildcard, exception, unlisted TLD, IP and localhost cases), the site-rules guard (load skip, set refusal, save round-trip) and the load report (unknown keys, invalid values, version warning, clean round-trip, missing file). (done)

## K1 — Kiwi browser

- The product browser lives in the separate [Kiwi](https://github.com/mel0k1/Kiwi) repository; mustela-shell stays the engine's test harness.
- Engine capabilities it consumes: stable settings API, privacy policy with per-site rules, profiles, packaging hooks.
