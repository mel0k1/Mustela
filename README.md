# Mustela

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-17-00599C)
![status](https://img.shields.io/badge/status-bootstrap-d95f5f)

Privacy-first, cross-platform browser engine built on WebKit.

Русская версия: [README.ru.md](README.ru.md)

## Why

Existing engines force a trade-off between privacy, platform coverage and speed. Mustela keeps the mature WebKit core and adds a tunable privacy layer on top of it, with Linux and Windows as first-class targets.

- **Privacy by choice.** Every storage, network and fingerprinting surface becomes a switch, not a promise. Off by default wherever it can be.
- **Cross-platform.** WebKitGTK on Linux, the WinCairo flavor of WebKit on Windows. One codebase, one build system.
- **Lean performance.** A thin embedder over WebKit: no bundled services, no background traffic, minimal startup cost.

## Status

`v0.1.0` — bootstrap. A minimal shell that opens a web view on Linux; the Windows port is stubbed and lands next.

See the [roadmap](docs/ROADMAP.md) for where this goes.

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
