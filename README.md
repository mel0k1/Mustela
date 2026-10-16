# Mustela

![ci](https://github.com/mel0k1/Mustela/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![status](https://img.shields.io/badge/status-M4_performance-e0a03d)

Privacy-first, cross-platform browser engine built on WebKit.

Русская версия: [README.ru.md](README.ru.md)

## Why

Existing engines force a trade-off between privacy, platform coverage and speed. Mustela keeps the mature WebKit core and adds a tunable privacy layer on top of it, with Linux and Windows as first-class targets.

- **Privacy by choice.** Every storage, network and fingerprinting surface becomes a switch, not a promise. Off by default wherever it can be.
- **Cross-platform.** WebKitGTK on Linux, the WinCairo flavor of WebKit on Windows. One codebase, one build system.
- **Lean performance.** A thin embedder over WebKit: no bundled services, no background traffic, minimal startup cost.

## Status

`v0.8.1` — the bench went cross-platform. `mustela-bench-win` runs the same page-load benchmark on the WinCairo C API: a hidden host window with a non-persistent data store, identical CLI, JSON stats and budget exit code as the GTK bench; CI compile-checks it against pinned upstream WebKit headers on every push. The engine also got its second full gap review in [docs/GAPS.md](docs/GAPS.md) — browsing-platform gaps (popups, dialogs, downloads, find, permissions, certificates), privacy deepening candidates and the Kiwi API surface, prioritized.

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

The suite covers settings round-trips, privacy gating invariants, site-rule matching, history escaping, fingerprint presets, browsing-data kinds, URL normalization and the perf module (statistics, record codec, budgets). On Linux the full GTK shell and `mustela-bench` are built in the same run, and the bench loads the local fixture under xvfb; on Windows, CI additionally compiles `shell_win.cpp` and `bench_win.cpp` against pinned upstream WebKit C API headers.

## Layout

```
src/core/        settings, privacy policy, site rules, fingerprint budget, browsing-data kinds, url normalization (portable, no UI deps)
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
