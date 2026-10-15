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
| win  | Windows  | WinCairo       | stub                      |

Each port lives in its own directory under `src/shell/` and exposes the same entry point (`mustela_main`). Platform selection happens in CMake at configure time, so port sources are never mixed in one build.

`mustela-core` is plain C++17 with no UI or WebKit dependencies: both ports link it, and it stays unit-testable. Privacy decisions go through `PrivacyPolicy` in the core — a port never decides privacy on its own, it only asks. Cookie accept policy, tracking prevention and hardening switches come from the same policy object; the ports translate it into WebKit calls. Browsing-data kinds (`DataKind`, `ClearRequest`) describe what "clear site data" means without WebKit types; the ports map them onto `WebKitWebsiteDataTypes`. Per-site overrides (`SiteRules`, `resolve_site`) resolve a host against the global policy in one call — subdomain matching and rule precedence included — which is the exact surface the Kiwi browser will drive. The fingerprint budget follows the same split: the core owns `FingerprintPolicy`, per-site resolution and the enforcement payloads as plain script data; the ports inject them through `WebKitUserContentManager` and compile content filters. History storage (`HistoryStore`) follows the same rule: the policy decides what gets recorded, private tabs never reach the store.

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

The Linux port links `webkit2gtk-4.1` through pkg-config. The Windows port compiles the stub with MSVC today and will link WinCairo libraries once the M3 stage of the [roadmap](ROADMAP.md) starts.
