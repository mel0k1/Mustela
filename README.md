# Mustela

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-17-00599C)
![status](https://img.shields.io/badge/status-M2.5_fp_budget-e0a03d)

Privacy-first, cross-platform browser engine built on WebKit.

Русская версия: [README.ru.md](README.ru.md)

## Why

Existing engines force a trade-off between privacy, platform coverage and speed. Mustela keeps the mature WebKit core and adds a tunable privacy layer on top of it, with Linux and Windows as first-class targets.

- **Privacy by choice.** Every storage, network and fingerprinting surface becomes a switch, not a promise. Off by default wherever it can be.
- **Cross-platform.** WebKitGTK on Linux, the WinCairo flavor of WebKit on Windows. One codebase, one build system.
- **Lean performance.** A thin embedder over WebKit: no bundled services, no background traffic, minimal startup cost.

## Status

`v0.6.0` — fingerprint budget added to the privacy core: switches for WebGL, canvas readback, remote fonts and referrer trimming (user scripts plus a compiled WebKit content filter — upstream API, no patches), a documented surface-by-surface budget in [PRIVACY.md](docs/PRIVACY.md), and per-site fingerprint rules in `sites.toml` (`allow` exempts a host, `deny` forces the strict set). Cookie accept policy, ITP, hardening switches, browsing-data clearing, per-site overrides and hot-reload from v0.5 remain the base. `resolve_site` in the core is the API the [Kiwi](https://github.com/mel0k1/Kiwi) browser will drive.

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

## Layout

```
src/core/        settings, privacy policy, site rules, fingerprint budget, browsing-data kinds (portable, no UI deps)
src/shell/       embedder shell, one directory per port
src/shell/gtk/   Linux port (WebKitGTK)
src/shell/win/   Windows port (WinCairo, in progress)
docs/            architecture, privacy model, roadmap, building
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Privacy model](docs/PRIVACY.md)
- [Roadmap](docs/ROADMAP.md)
- [Building](docs/BUILDING.md)

## License

Mustela is licensed under the [GNU LGPL-2.1](LICENSE) — the same license WebKit uses, so engine code can flow in without license conflicts.
