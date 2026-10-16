# Architecture

Mustela is a thin, opinionated layer over WebKit. The engine core is upstream WebKit; everything Mustela-specific lives above it and stays portable.

## Layers

```
+-----------------------------+
| Kiwi browser (UI)           |   separate repository: github.com/mel0k1/Kiwi
+-----------------------------+
| mustela-core                |   settings, privacy policy, browsing-data kinds, site rules, fingerprint budget, tracker blocklist, DoH codec, session store, perf budgets and sampling, history; later: profiles
+-----------------------------+
| mustela-shell               |   engine test harness and playground, not the product
+-----------------------------+
| WebKit                      |   upstream, kept unpatched as long as possible
+-----------------------------+
```

## Ports

| Port | Platform | WebKit flavor  | Status                    |
|------|----------|----------------|---------------------------|
| gtk  | Linux    | WebKitGTK 2.4x | working — tabs, url bar, privacy policies, site rules, fingerprint budget, popups, JS dialogs, find-in-page, context menu, fullscreen, session restore, GPC + blocklist, local proxy (blocking + DoH) |
| win  | Windows  | WinCairo       | working — tabs, url bar, privacy policies, fingerprint budget, history, hot-reload, popups, JS dialogs, find-in-page, context menu, F11 fullscreen, session restore, GPC + blocklist, local proxy via env hook |

Each port lives in its own directory under `src/shell/` and exposes the same entry point (`mustela_main`). Platform selection happens in CMake at configure time, so port sources are never mixed in one build.

`mustela-core` is plain C++20 with no UI or WebKit dependencies: both ports link it, and it stays unit-testable (`src/tests`, 468 checks under ctest on both platforms). Privacy decisions go through `PrivacyPolicy` in the core — a port never decides privacy on its own, it only asks. Cookie accept policy, tracking prevention and hardening switches come from the same policy object; the ports translate it into WebKit calls. Browsing-data kinds (`DataKind`, `ClearRequest`) describe what "clear site data" means without WebKit types; the GTK port maps them onto `WebKitWebsiteDataTypes`, the Windows port onto the `WKHTTPCookieStore` and data-store clearing calls available in the C API. Per-site overrides (`SiteRules`, `resolve_site`) resolve a host against the global policy in one call — subdomain matching and rule precedence included — which is the exact surface the Kiwi browser will drive. Permission requests resolve the same way: `permissions.hpp` maps the global `deny`/`ask`/`allow` choice per permission kind through the per-site `permissions` rule into a denied/prompt/granted decision, and the ports only translate the answer into `webkit_permission_request_*` or the `WKPageUIClient` permission callbacks. The fingerprint budget follows the same split: the core owns `FingerprintPolicy`, per-site resolution and the enforcement payloads as plain script data; the GTK port injects them through `WebKitUserContentManager`, the Windows port through `WKUserContentController` — the same payload strings, the same compiled content filter for remote fonts. History storage (`HistoryStore`) follows the same rule: the policy decides what gets recorded, private tabs never reach the store. URL normalization (`normalize_url`) lives in the core too — one search-or-address behavior for both ports and for Kiwi. Text that comes from the web but reaches the UI — JS dialog bodies, find queries and download file names — passes through `ui_text` (`sanitize_dialog_text`, `sanitize_find_query`, `sanitize_filename`): control characters are stripped, line endings normalized, and truncation happens on UTF-8 boundaries with a length cap, identically on both ports. The tracking blocklist (`blocklist.hpp`) keeps the domain data, suffix matching and content-rule JSON building in the core, so both ports compile the same rule set — GTK through `WebKitUserContentFilterStore`, Windows through `WKUserContentExtensionStore`. The session store (`session.hpp`) owns the on-disk format (JSON with per-tab URL, title, history lists and an opaque per-port state blob); the GTK port fills it with `WebKitWebViewSessionState` and restores full history, the Windows port stores back/forward URLs it can replay as plain loads. The DoH codec (`doh.hpp`) is pure bytes — query build, compressed response parse, base64url framing. The network layer goes one step further: `proxy_server.hpp` keeps a portable loopback proxy engine (CONNECT tunneling, plain-HTTP forwarding, blocklist enforcement, upstream chaining) and the `DohResolver` in the core; the ports only supply the platform detail — GTK a libsoup DoH transport plus `WebKitNetworkProxySettings` routing, Windows a WinHTTP transport plus proxy environment variables. Site-rule hosts resolve through the public-suffix layer (`psl.hpp`): registry suffixes are guarded out of `sites.toml` on load and save, rule lookup stops climbing at the effective top level, and `registrable_domain` is available for Kiwi. Settings loading returns a `SettingsReport` (schema version, applied/unknown/invalid keys) so embedders can surface a malformed file instead of silently loading defaults. The core suite is 468 checks under ctest on both platforms.

The performance module (`perf.hpp/cpp`) sits in the core for the same reason. `Stopwatch` and `LoadTimer` are the only timing primitives the ports use, so startup and page-load measurements are defined once. `SampleStats` (min/median/p95/max) and the JSONL record codec (`PerfRecord`, `PerfLog`) make samples portable between the shells, `mustela-bench` and `scripts/perf_check.py`. Budget and ceiling verdicts (`within_budget`, `ceiling_exceeded`) and the `/proc` RSS parser are pure functions the ports feed with platform data: the GTK port reads `/proc/<pid>/status` for the process tree and applies the process model through `WebKitWebContext`, the Windows port reads the same numbers via `GetProcessMemoryInfo` and Toolhelp and applies `WKContextSetProcessModel`. A port never invents its own perf semantics.

The product browser built on top of Mustela is [Kiwi](https://github.com/mel0k1/Kiwi) — a separate repository with its own UI and roadmap. Everything in `mustela-shell` is a test harness for the engine APIs Kiwi will consume.

## WebKit dependency strategy

- Build against upstream WebKit first: WebKitGTK via pkg-config on Linux, WinCairo from upstream build instructions on Windows.
- If a WebKit patch ever becomes unavoidable, it goes to `patches/` as a standalone diff with an upstream-first mindset.
- Mustela must always remain buildable against unmodified WebKit. This is a hard rule.

## Build system

Single CMake entry point:

```bash
cmake -S . -B build
cmake --build build
```

The Linux port links `webkit2gtk-4.1` through pkg-config. The Windows port links the WinCairo libraries through `MUSTELA_WEBKIT_SDK` (an include/ and lib/ pair produced by a WebKit build); without an SDK it falls back to a placeholder shell so the core and tests still build under MSVC. Tests live in `src/tests` and run under ctest.

## Continuous integration

`.github/workflows/ci.yml` runs three jobs on every push and pull request:

- **linux** — full GTK shell build against `webkit2gtk-4.1` (including the `mustela-webext` web-process extension module), ctest, then `mustela-bench` over the local fixture under `xvfb-run` with a generous `--max-ms` budget.
- **windows core** — core and tests under MSVC.
- **windows shell** — `shell_win.cpp` and `bench_win.cpp` compiled against 88 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`, pinned commit in the script), so the WinCairo embedding and the Windows bench are checked against reality without building WebKit.

The bench step in CI is a smoke budget, not a performance gate: runner hardware varies, so 10 seconds catches hangs and catastrophic regressions only. Real regression tracking happens locally with `scripts/perf_check.py` against a recorded baseline.
