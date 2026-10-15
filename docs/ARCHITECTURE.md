# Architecture

Mustela is a thin, opinionated layer over WebKit. The engine core is upstream WebKit; everything Mustela-specific lives above it and stays portable.

## Layers

```
+-----------------------------+
| mustela-browser (UI)        |   planned: the full browser product
+-----------------------------+
| mustela-core                |   today: settings, privacy policy; later: profiles, per-site rules
+-----------------------------+
| mustela-shell               |   today: embedder shell, one port per platform
+-----------------------------+
| WebKit                      |   upstream, kept unpatched as long as possible
+-----------------------------+
```

## Ports

| Port | Platform | WebKit flavor  | Status                    |
|------|----------|----------------|---------------------------|
| gtk  | Linux    | WebKitGTK 2.4x | working — tabs, url bar   |
| win  | Windows  | WinCairo       | stub                      |

Each port lives in its own directory under `src/shell/` and exposes the same entry point (`mustela_main`). Platform selection happens in CMake at configure time, so port sources are never mixed in one build.

`mustela-core` is plain C++17 with no UI or WebKit dependencies: both ports link it, and it stays unit-testable. Privacy decisions go through `PrivacyPolicy` in the core — a port never decides privacy on its own, it only asks.

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
