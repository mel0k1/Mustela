# Roadmap

Milestones are capability-based; dates are intentionally not assigned.

## M0 — Bootstrap (current)

- Repository, CMake build, LGPL-2.1 licensing.
- `mustela` shell: GTK port opens a web view, `--private` runs an ephemeral profile.
- Windows port stub, build docs for both platforms.

## M1 — Shell baseline (current)

- Navigation: back, forward, reload, URL bar, loading progress. (done)
- Search fallback for non-URL input. (done)
- Basic tab model: open, close, switch, private tabs behind policy. (done)
- Mustela-owned profile directory, separate from system defaults. (done)
- Settings file (TOML) loaded at startup, saved on exit. (done)
- Settings hot-reload on file change. (next)

## M2 — Privacy core

- Per-profile cookie and storage policies.
- Switches: JavaScript, WebGL, canvas, autoplay, remote fonts, referrer policy.
- First-party-only mode and third-party storage blocking.
- Per-site overrides on top of the global policy.

## M3 — Windows port

- WinCairo build scripted end-to-end from a clean machine.
- Shell and privacy-core parity with the GTK port.
- CI parity once both platforms build green.

## M4 — Performance

- Startup time budget with regression tracking.
- Process model tuning and memory ceiling measurements.
- Profiling harness for page-load benchmarks.

## M5 — Mustela browser

- Full UI on top of the engine: tabs, history, downloads, bookmarks.
- Packaging: deb, rpm, Flatpak on Linux; installer and portable zip on Windows.
- Extensions policy definition.
