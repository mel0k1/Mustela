# Architecture

Mustela is a thin, opinionated layer over WebKit. The engine core is upstream WebKit; everything Mustela-specific lives above it and stays portable.

## Layers

```
+-----------------------------+
| Kiwi browser (UI)           |   separate repository: github.com/mel0k1/Kiwi
+-----------------------------+
| mustela-core                |   settings, privacy policy, browsing-data kinds, site rules, fingerprint budget, history; later: profiles
+-----------------------------+
| mustela-shell               |   engine test harness and playground, not the product
+-----------------------------+
| WebKit                      |   upstream, kept unpatched as long as possible
+-----------------------------+
```

## Ports

| Port | Platform | WebKit flavor  | Status                    |
|------|----------|----------------|---------------------------|
| gtk  | Linux    | WebKitGTK 2.4x | working — tabs, url bar, privacy policies, site rules, fingerprint budget |
| win  | Windows  | WinCairo       | working — tabs, url bar, privacy policies, fingerprint budget, history, hot-reload |

Each port lives in its own directory under `src/shell/` and exposes the same entry point (`mustela_main`). Platform selection happens in CMake at configure time, so port sources are never mixed in one build.

`mustela-core` is plain C++17 with no UI or WebKit dependencies: both ports link it, and it stays unit-testable (`src/tests`, 125 checks under ctest on both platforms). Privacy decisions go through `PrivacyPolicy` in the core — a port never decides privacy on its own, it only asks. Cookie accept policy, tracking prevention and hardening switches come from the same policy object; the ports translate it into WebKit calls. Browsing-data kinds (`DataKind`, `ClearRequest`) describe what "clear site data" means without WebKit types; the GTK port maps them onto `WebKitWebsiteDataTypes`, the Windows port onto the `WKHTTPCookieStore` and data-store clearing calls available in the C API. Per-site overrides (`SiteRules`, `resolve_site`) resolve a host against the global policy in one call — subdomain matching and rule precedence included — which is the exact surface the Kiwi browser will drive. The fingerprint budget follows the same split: the core owns `FingerprintPolicy`, per-site resolution and the enforcement payloads as plain script data; the GTK port injects them through `WebKitUserContentManager`, the Windows port through `WKUserContentController` — the same payload strings, the same compiled content filter for remote fonts. History storage (`HistoryStore`) follows the same rule: the policy decides what gets recorded, private tabs never reach the store. URL normalization (`normalize_url`) lives in the core too — one search-or-address behavior for both ports and for Kiwi.

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

- **linux** — full GTK shell build against `webkit2gtk-4.1` plus ctest.
- **windows core** — core and tests under MSVC.
- **windows shell** — `shell_win.cpp` compiled against 80 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`, pinned commit in the script), so the WinCairo embedding is checked against reality without building WebKit.
