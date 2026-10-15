# Roadmap

Milestones are capability-based; dates are intentionally not assigned.

## M0 — Bootstrap (current)

- Repository, CMake build, LGPL-2.1 licensing.
- `mustela` shell: GTK port opens a web view, `--private` runs an ephemeral profile.
- Windows port stub, build docs for both platforms.

## M1 — Shell baseline (done)

- Navigation: back, forward, reload, URL bar, loading progress. (done)
- Search fallback for non-URL input. (done)
- Basic tab model: open, close, switch, private tabs behind policy. (done)
- Mustela-owned profile directory, separate from system defaults. (done)
- Settings file (TOML) loaded at startup, saved on exit. (done)

## M1.5 — Engine APIs and shell demos (current)

- History store in the core, private tabs excluded at the policy level. (done)
- Runtime privacy toggle in the shell UI. (done)
- History window in the shell (Ctrl+H) with clear action. (done)
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

## M5 — Kiwi browser

- The product browser lives in the separate [Kiwi](https://github.com/mel0k1/Kiwi) repository; mustela-shell stays the engine's test harness.
- Engine capabilities it consumes: stable settings API, privacy policy with per-site rules, profiles, packaging hooks.
