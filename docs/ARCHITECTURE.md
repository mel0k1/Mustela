# Architecture

Mustela is a thin, opinionated layer over WebKit. The engine core is upstream WebKit; everything Mustela-specific lives above it and stays portable.

## Layers

```
+-----------------------------+
| Kiwi browser (UI)           |   separate repository: github.com/mel0k1/Kiwi
+-----------------------------+
| mustela-view (GTK port)     |   embedder view library: session/view objects, context-level privacy, dressing, error pages, downloads, dialogs, permissions, popups (no WebKit types in its header)
+-----------------------------+
| mustela-core                |   settings, privacy policy, browsing-data kinds, site rules, fingerprint budget, tracker blocklist, DoH codec, session store, bookmarks, error pages, perf budgets and sampling, history, profiles, search engines, autofill store
+-----------------------------+
| mustela-shell               |   engine test harness and playground, not the product
+-----------------------------+
| WebKit                      |   upstream, kept unpatched as long as possible
+-----------------------------+
```

## Ports

| Port | Platform | WebKit flavor  | Status                    |
|------|----------|----------------|---------------------------|
| gtk  | Linux    | WebKitGTK 2.4x | working — tabs (favicons), url bar, privacy policies, site rules, fingerprint budget, popups, JS dialogs, find-in-page, context menu, fullscreen, session restore, GPC + blocklist, local proxy (blocking + DoH), devtools, branded error pages, bookmarks, notifications, print, screenshots, save-page (MHTML), spellchecking, profile handle, search keyword shortcuts, drop-to-navigate, tab drag-reorder |
| win  | Windows  | WinCairo       | working — tabs, url bar, privacy policies, fingerprint budget, history, hot-reload, popups, JS dialogs, find-in-page, context menu, F11 fullscreen, session restore, GPC + blocklist, local proxy via env hook, devtools, branded error pages, bookmarks |

Each port lives in its own directory under `src/shell/` and exposes the same entry point (`mustela_main`). Platform selection happens in CMake at configure time, so port sources are never mixed in one build.

`mustela-core` is plain C++20 with no UI or WebKit dependencies: both ports link it, and it stays unit-testable (`src/tests`, 702 checks under ctest on both platforms). Privacy decisions go through `PrivacyPolicy` in the core — a port never decides privacy on its own, it only asks. Cookie accept policy, tracking prevention and hardening switches come from the same policy object; the ports translate it into WebKit calls. Browsing-data kinds (`DataKind`, `ClearRequest`) describe what "clear site data" means without WebKit types; the GTK port maps them onto `WebKitWebsiteDataTypes`, the Windows port onto the `WKHTTPCookieStore` and data-store clearing calls available in the C API. Per-site overrides (`SiteRules`, `resolve_site`) resolve a host against the global policy in one call — subdomain matching and rule precedence included — which is the exact surface the Kiwi browser will drive. Permission requests resolve the same way: `permissions.hpp` maps the global `deny`/`ask`/`allow` choice per permission kind through the per-site `permissions` rule into a denied/prompt/granted decision, and the ports only translate the answer into `webkit_permission_request_*` or the `WKPageUIClient` permission callbacks. The fingerprint budget follows the same split: the core owns `FingerprintPolicy`, per-site resolution and the enforcement payloads as plain script data; the GTK port injects them through `WebKitUserContentManager`, the Windows port through `WKUserContentController` — the same payload strings, the same compiled content filter for remote fonts. History storage (`HistoryStore`) follows the same rule: the policy decides what gets recorded, private tabs never reach the store. URL normalization (`normalize_url`) and the entry pipeline (`resolve_entry`: keyword search shortcuts like `!g query`, `://` passthrough, host-like text, percent-encoded queries against the configured `[search.engines]`) live in the core too — one search-or-address behavior for both ports and for Kiwi. Text that comes from the web but reaches the UI — JS dialog bodies, find queries and download file names — passes through `ui_text` (`sanitize_dialog_text`, `sanitize_find_query`, `sanitize_filename`): control characters are stripped, line endings normalized, and truncation happens on UTF-8 boundaries with a length cap, identically on both ports. The tracking blocklist (`blocklist.hpp`) keeps the domain data, suffix matching and content-rule JSON building in the core, so both ports compile the same rule set — GTK through `WebKitUserContentFilterStore`, Windows through `WKUserContentExtensionStore`. The session store (`session.hpp`) owns the on-disk format (JSON with per-tab URL, title, history lists and an opaque per-port state blob); the GTK port fills it with `WebKitWebViewSessionState` and restores full history, the Windows port stores back/forward URLs it can replay as plain loads. The DoH codec (`doh.hpp`) is pure bytes — query build, compressed response parse, base64url framing. The network layer goes one step further: `proxy_server.hpp` keeps a portable loopback proxy engine (CONNECT tunneling, plain-HTTP forwarding, blocklist enforcement, upstream chaining) and the `DohResolver` in the core; the ports only supply the platform detail — GTK a libsoup DoH transport plus `WebKitNetworkProxySettings` routing, Windows a WinHTTP transport plus proxy environment variables. Site-rule hosts resolve through the public-suffix layer (`psl.hpp`): the bundled data is the full official PSL (`psl_data.inc`, generated by `scripts/gen_psl.py` from publicsuffix.org — exact, wildcard and exception rules with IDN entries normalized to Punycode, `psl_version()` reports the list version), registry suffixes are guarded out of `sites.toml` on load and save, rule lookup stops climbing at the effective top level, and `registrable_domain` is available for Kiwi. Settings loading returns a `SettingsReport` (schema version, applied/unknown/invalid keys) so embedders can surface a malformed file instead of silently loading defaults. The embedder-facing surface is declared, not implied: `include/mustela/` holds the umbrella `mustela.hpp` (settings, privacy, permissions, site rules, fingerprint, browsing data, perf, history, url, bookmarks, profile, search, autofill) and a `version.hpp` with `MUSTELA_CORE_API_VERSION`, installed alongside the static core by CMake. `profile.hpp` ships the `Profile` handle — a root directory expanded into the standard data layout (`history.dat`, `bookmarks.tsv`, `session.json`, `perf.jsonl`, `filters/`, `favicons/`, `autofill.tsv`) with its own `PrivacyPolicy` copy and file-backed `has`/`clear` per `ProfileStore`, backed by a `ProfileRegistry` over `profiles.toml`; the GTK harness builds its profile paths through it, while cookies, cache and web storage stay port-owned behind `DataKind`/`ClearRequest`. Per-site dressing (`zoom`, `user_agent`, `css`) rides on `SitePolicy` in the same `sites.toml` store, clamped and guard-checked like the policy fields, and `resolve_site` hands it to the caller on `SiteDecision` — the GTK harness applies zoom, the user-agent override (with fallback to the per-tab default) and the CSS style sheet when a page commits, and Kiwi gets the identical one-call surface. Autofill ships as data plus payloads: `AutofillStore::by_domain` picks the entries and `autofill_fill_script` / `autofill_capture_script` build the JS, so an embedder wires fill-on-load and capture-on-submit without embedding policy in the shell. Browser-side data stays portable too: `bookmarks.hpp` owns a `BookmarkStore` (escaped TSV, URL de-duplication) and `error_page.hpp` builds the branded error page both ports render, with every external string sanitized and HTML-escaped in one place. Form values for Kiwi's future autofill UI live in `AutofillStore` (`autofill.hpp`: domain + field + value entries with use counters, subdomain-aware lookup, escaped TSV in the profile). The core suite is 702 checks under ctest on both platforms.

The performance module (`perf.hpp/cpp`) sits in the core for the same reason. `Stopwatch` and `LoadTimer` are the only timing primitives the ports use, so startup and page-load measurements are defined once. `SampleStats` (min/median/p95/max) and the JSONL record codec (`PerfRecord`, `PerfLog`) make samples portable between the shells, `mustela-bench` and `scripts/perf_check.py`. Budget and ceiling verdicts (`within_budget`, `ceiling_exceeded`) and the `/proc` RSS parser are pure functions the ports feed with platform data: the GTK port reads `/proc/<pid>/status` for the process tree and applies the process model through `WebKitWebContext`, the Windows port reads the same numbers via `GetProcessMemoryInfo` and Toolhelp and applies `WKContextSetProcessModel`. A port never invents its own perf semantics.

The view library (`src/view/gtk/view_gtk.cpp`, `libmustela_view_gtk.a`, built behind `MUSTELA_BUILD_VIEW`) is the embedder path Kiwi consumes. Its public header `include/mustela/view.hpp` contains zero WebKit types: `ViewConfig` points at the live `Settings`/`PrivacyPolicy`/`SiteRules`/`Profile` objects, `ViewSession` owns the normal and ephemeral private contexts plus everything context-level (cookie policy, ITP, TLS policy, the local/upstream proxy with its libsoup DoH transport, the Sec-GPC web extension wiring, the fonts and tracker-blocklist filter store, download tracking), and each `View` wraps one WebKitWebView behind `native_widget()` — navigation, zoom and user-agent controls, per-view callbacks for navigation/loading/commit/error-page/favicon/link-hover/process-termination, JS dialogs, permission prompts resolved through the engine policy, certificate and auth prompts, popup handoff and close. Policy application lives in the library: hardening, fingerprint payloads, the GPC script and per-site dressing (JS, zoom, user-agent, CSS) re-apply on every commit, branded error pages render through `load_alternate_html`, denied origins get their cookies wiped, crashes recover with two guarded reloads. The header is portable by construction (`MUSTELA_VIEW_API_VERSION` in the file), so a future WinCairo view library slots under the same API; the shell harness stays the engine's playground and does not use the view library.

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

`.github/workflows/ci.yml` runs five jobs on every push and pull request:

- **linux** — full GTK shell build against `webkit2gtk-4.1` (including the `mustela-webext` web-process extension module), ctest, then `mustela-bench` over the local fixture under `xvfb-run` with a generous `--max-ms` budget.
- **windows core** — core and tests under MSVC.
- **windows shell** — `shell_win.cpp` and `bench_win.cpp` compiled against 88 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`, pinned commit in the script), so the WinCairo embedding and the Windows bench are checked against reality without building WebKit.
- **sanitizers** — the core suite rebuilt with `-fsanitize=address,undefined` and run strict (leak detection on, halt on first finding).
- **clang-tidy** — static analysis over `src/core` with the curated `.clang-tidy` set; advisory (`continue-on-error`) until findings are burned down.

On a `v*` tag a **release** job runs after all four check tracks: it rebuilds the GTK tree with `MUSTELA_WEBEXT_DIR=/usr/lib/mustela/webext`, runs ctest, packages a deb (shlib-deps-generated) and a tarball through CPack and attaches both to the GitHub Release. The Windows bundle stays unbundled until a WinCairo runtime source is pinned.

The bench step in CI is a smoke budget, not a performance gate: runner hardware varies, so 10 seconds catches hangs and catastrophic regressions only. Real regression tracking happens locally with `scripts/perf_check.py` against a recorded baseline.
