# Mustela

![ci](https://github.com/mel0k1/Mustela/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![status](https://img.shields.io/badge/status-M10_platform_dressing-e0a03d)

Privacy-first, cross-platform browser engine built on WebKit.

Русская версия: [README.ru.md](README.ru.md)

## Why

Existing engines force a trade-off between privacy, platform coverage and speed. Mustela keeps the mature WebKit core and adds a tunable privacy layer on top of it, with Linux and Windows as first-class targets.

- **Privacy by choice.** Every storage, network and fingerprinting surface becomes a switch, not a promise. Off by default wherever it can be.
- **Cross-platform.** WebKitGTK on Linux, the WinCairo flavor of WebKit on Windows. One codebase, one build system.
- **Lean performance.** A thin embedder over WebKit: no bundled services, no background traffic, minimal startup cost.

## Status

`v0.15.0` closes the GTK leftovers from M10: Ctrl+S saves the current page as MHTML through `webkit_web_view_save_to_file` with a native save dialog (default name from the sanitized page title, fallback to the host, default folder from `downloads.dir`); and spellchecking arrives on GTK — `shell.spellcheck` (on by default) enables WebKit's spell checker on both the normal and the private context, `shell.spellcheck_languages` takes a comma-separated dictionary list like `en_US, ru_RU` (empty = the locale default), both hot-reload with settings. The suite is now 538 checks.

`v0.14.0` closes M10, the second round of platform dressing: web notifications display on GTK — the `show-notification` signal renders through `GNotification` on a registered `GApplication` (titles and bodies pass the dialog sanitizer, the permission gate stays the M5 policy); Ctrl+P opens the system print dialog through `WebKitPrintOperation`; Ctrl+Alt+S captures the visible page through `webkit_web_view_get_snapshot` into a timestamped PNG in the downloads directory; and the bundled public-suffix list becomes the full official PSL — 10,080 exact, 311 wildcard and 8 exception rules, IDN entries normalized to Punycode and refreshed by `scripts/gen_psl.py` — so `is_public_suffix` / `registrable_domain` and the `sites.toml` guard now follow the spec exactly, wildcards and exceptions included. The suite is now 532 checks.

`v0.13.0` closes M9, the browser dressing: devtools open with Ctrl+Shift+I / F12 on both ports (GTK toggles the WebKit inspector through `webkit_web_view_get_inspector` with `shell.devtools` as the switch, Windows shows it through `WKPageGetInspector` + `WKPageShowWebInspectorForTesting`); failed main-frame loads render Mustela-branded error pages instead of WebKit's stock page — heading, detail and failing URL come from the core (`error_page.hpp`), HTML-escaped with a Try-again action; bookmarks live in a core store (`bookmarks.tsv` in the profile, `BookmarkStore`) with a toolbar star (Ctrl+D) and a bookmarks window (Ctrl+Shift+O) on both ports; GTK tabs show site favicons from the WebKit favicon database, persisted as PNGs under the profile. The suite is now 513 checks.

`v0.10.0` closes M6: Global Privacy Control (`privacy.gpc`) sends the `Sec-GPC` header through a GTK web-process extension and exposes `navigator.globalPrivacyControl` on both ports; a ~60-domain tracker blocklist (`privacy.blocklist`, optional `privacy.blocklist_file`) compiles into content filters on both ports; context menus are Mustela-built on both ports (navigation, link/image/media, editing sections by hit-test); F11 toggles window fullscreen and HTML5 element fullscreen works on GTK; session restore reopens tabs after a restart — full back/forward history on GTK, tab URLs on Windows (`shell.session_restore`). The core grew a DoH codec (RFC 8484) as secure-DNS groundwork while neither WebKit flavor exposes a resolver hook yet; the suite is now 322 checks.

`v0.11.0` adds the local-proxy network layer: `proxy.mode = "local"` starts a loopback-only proxy engine in the core (`proxy_server.hpp`) that every page load goes through — tracker domains are refused at connection setup (subdomains included), plain-HTTP requests are checked per request, CONNECT tunnels carry no readable content (no TLS interception by design), and name resolution happens through the DoH resolver (`privacy.dns_over_https`) with a strict no-leak policy — if DoH fails, the connection fails. `proxy.mode = "upstream"` chains to an external proxy with optional Basic auth. GTK routes both profiles through `webkit_website_data_manager_set_network_proxy_settings` (live on hot-reload); Windows sets `http_proxy`/`https_proxy` before `WKContext` creation and resolves DoH through WinHTTP. The suite is now 386 checks with loopback integration tests for blocking, tunneling, upstream chaining and fail-closed DoH.

`v0.12.0` closes the last two privacy leftovers: a public-suffix guard and a settings load report. The core bundles a compact public-suffix list (`psl.hpp` — common gTLDs, popular ccTLD second levels, wildcard and exception rules): `sites.toml` sections for registry suffixes like `co.uk` or `com` are skipped on load and refused on save, so one entry can never silently cover a whole registry, and rule lookup stops climbing at the effective top level. Settings loading becomes observable: `load_settings` fills a report with the schema `version`, applied/unknown/invalid keys and a warning for files written by a newer engine, and both shells surface it — GTK on stdout, Windows through the debug output. The suite is now 468 checks.

`v0.9.1` completes M5: downloads land in `downloads.dir` (or the platform Downloads folder) with collision-free names and a status-bar progress line, `downloads.mode = "ask"` opens a save dialog; zoom works on Ctrl+plus/minus/0 (30%–300%, default `shell.zoom`); permission requests (geolocation, camera, microphone, notifications) resolve through a new core policy — global `deny`/`ask`/`allow` per kind plus a per-site `permissions` rule in `sites.toml` remembered from "always/never" prompt choices; invalid TLS certificates prompt on GTK (`security.tls_strict`) and HTTP auth prompts for credentials on both ports; crashed web processes auto-reload twice with a crash-loop guard on both ports. The Windows header pin grew to 86 with the auth/credential set; the suite is now 245 checks.

`v0.9.0` made the engine a real browsing platform: popups and `target=_blank` open as tabs and inherit the originating tab's privacy mode, `alert`/`confirm`/`prompt`/beforeunload show native dialogs with sanitized text, find-in-page (Ctrl+F) works with match counts and wrap-around on both ports, and hovering a link shows its destination in the status bar.

`v0.8.1` added the cross-platform bench: `mustela-bench-win` runs the same page-load benchmark on the WinCairo C API — a hidden host window with a non-persistent data store, identical CLI, JSON stats and budget exit codes; CI compile-checks it against pinned upstream WebKit headers on every push. The engine also got its second full gap review in [docs/GAPS.md](docs/GAPS.md).

The v0.7 Windows port remains the base: `src/shell/win/` embeds the upstream WinCairo flavor of WebKit through its C API — tabs, URL bar with search fallback, persistent profile under `%APPDATA%\mustela`, private tabs on non-persistent data stores, cookie accept policy, ITP, hardening switches, the full fingerprint budget (user scripts plus a compiled content filter), history, clear-browsing-data and settings hot-reload — and the v0.6 privacy core (cookie policy, ITP, hardening, per-site overrides, fingerprint budget, hot-reload) keeps resolving everything through `PrivacyPolicy` in the core.

See the [roadmap](docs/ROADMAP.md) for where this goes.

## Ecosystem

- [Kiwi](https://github.com/mel0k1/Kiwi) — the privacy-first browser built on top of Mustela. Mustela is the engine; Kiwi is the product.

## Build (Linux)

```bash
sudo apt install build-essential cmake pkg-config libwebkit2gtk-4.1-dev
./scripts/build-linux.sh
./build/mustela
```

Run with an ephemeral in-memory profile:

```bash
./build/mustela --private
```

Full guide, including Windows: [docs/BUILDING.md](docs/BUILDING.md).

## Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite covers settings round-trips, privacy gating invariants, permission-policy resolution, site-rule matching (including per-site permissions), history escaping, fingerprint presets, browsing-data kinds, URL normalization, the perf module (statistics, record codec, budgets) and the ui-text sanitization rules (dialog text, find queries, download file names). On Linux the full GTK shell and `mustela-bench` are built in the same run, and the bench loads the local fixture under xvfb; on Windows, CI additionally compiles `shell_win.cpp` and `bench_win.cpp` against pinned upstream WebKit C API headers.

## Layout

```
src/core/        settings, privacy policy, permission policy, site rules, fingerprint budget, browsing-data kinds, url normalization, ui-text sanitization (portable, no UI deps)
src/shell/       embedder shell, one directory per port
src/shell/gtk/   Linux port (WebKitGTK)
src/shell/win/   Windows port (WinCairo over the WebKit C API)
src/bench/       page-load benchmark harness (GTK and WinCairo variants)
src/tests/       core test suite (ctest)
docs/            architecture, privacy model, roadmap, building
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Privacy model](docs/PRIVACY.md)
- [Roadmap](docs/ROADMAP.md)
- [Building](docs/BUILDING.md)

## License

Mustela is licensed under the [GNU LGPL-2.1](LICENSE) — the same license WebKit uses, so engine code can flow in without license conflicts.
