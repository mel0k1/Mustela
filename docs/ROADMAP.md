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

## M1.5 — Engine APIs and shell demos (done)

- History store in the core, private tabs excluded at the policy level. (done)
- Runtime privacy toggle in the shell UI. (done)
- History window in the shell (Ctrl+H) with clear action. (done)
- Settings hot-reload on file change. (moved to M2)

## M2 — Privacy core (done)

- Cookie accept policy per context: `always`, `no-third-party`, `never`. (done)
- Intelligent Tracking Prevention on the persistent profile. (done)
- Hardening switches: JavaScript, hyperlink auditing, DNS prefetching, autoplay gesture. (done)
- Browsing-data kinds (`DataKind`, `ClearRequest`) and a clear action in the shell (Ctrl+Shift+Del). (done)
- Settings hot-reload on file change. (done)
- Per-site overrides: `sites.toml`, JavaScript allow/deny, cookie deny with origin wipe, subdomain matching, `resolve_site` in the core, site-rules window in the shell. (done)

## M2.5 — Fingerprint budget (next)

- Switches for WebGL, canvas, remote fonts and referrer trimming.
- A measurable fingerprint budget: document which combinations reduce uniqueness and by how much.
- Per-site extension of the fingerprint policy on top of `SiteRules`.

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
