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

## M2.5 — Fingerprint budget (done)

- Switches for WebGL, canvas readback, remote fonts and referrer trimming, enforced through user scripts and a compiled content filter. (done)
- A measurable fingerprint budget documented in PRIVACY.md: surface-by-surface trade-offs, presets and honest limits. (done)
- Per-site fingerprint rules on top of `SiteRules`: `allow` exempts a host, `deny` forces the strict set. (done)

## M3 — Windows port (done)

- Shell on the upstream WinCairo flavor of WebKit through its C API: tabs, URL bar with search fallback, window geometry persistence. (done)
- Privacy-core parity: cookie accept policy, ITP, hardening switches, per-site overrides, fingerprint budget via user scripts and a compiled content filter, history, clear-browsing-data, settings hot-reload. (done)
- Persistent profile under `%APPDATA%\mustela`, private tabs on non-persistent data stores. (done)
- Windows shell compiled against pinned upstream WebKit C API headers on every push. (done)

Known gaps, tracked for the next pass: per-site cookie denial works through the global accept policy (the C API has no per-origin cookie wipe on Windows), clear-data covers cookies and storage but not the disk cache, and there is no site-rules window — edit `sites.toml`, hot-reload applies it live.

## CI (done)

- Linux: full GTK shell build against `webkit2gtk-4.1` plus the core test suite on every push. (done)
- Windows: core and tests under MSVC. (done)
- Windows shell syntax check against 80 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`). (done)

## M4 — Performance (done)

- Startup budget: time-to-first-map on Linux, time-to-first-paint on Windows, enforced against `perf.startup_budget_ms` and logged. (done)
- Process model tuning: `perf.process_model` (`per-view` or `shared`) wired through `WebKitWebContext` and `WKContextSetProcessModel`. (done)
- Memory ceiling: process-tree RSS sampling every minute (`/proc` on Linux, `GetProcessMemoryInfo` + Toolhelp on Windows), `perf.memory_ceiling_mb` drives a status-bar warning, samples go to the log. (done)
- Page-load timing between provisional start and finish navigation, appended to `perf.jsonl` in the profile. (done)
- `mustela-bench` harness: real loads through a minimal offscreen WebKit shell, JSON stats (min/median/p95/max), budget exit code; runs under `xvfb-run` in CI on every push. (done)
- Regression tracking: `scripts/perf_check.py` summarizes `perf.jsonl` and compares medians against a recorded baseline with tolerance. (done)

Known gaps, tracked for the next pass: the ceiling only warns — there is no tab discarding (WebKitGTK and the WinCairo C API expose no per-view suspend), `perf.process_model` is applied at startup only, and the WinCairo bench compiles in CI but does not run there (GitHub Windows runners ship no WinCairo runtime).

## M4.5 — Windows bench and gap analysis (done)

- `mustela-bench-win`: the same page-load harness on the WinCairo C API — hidden host window, non-persistent data store, identical CLI (`--url/--runs/--max-ms/--out`), identical JSON stats and exit codes. (done)
- Built with the Windows SDK branch in CMake (`mustela-bench-win`) and compile-checked against the pinned upstream headers on every push. (done)
- Second full engine review at v0.8.0 recorded in [GAPS.md](GAPS.md): browsing-platform gaps (popups, dialogs, downloads, find, zoom, permissions, certificates), privacy deepening candidates (GPC, tracking blocklist, secure DNS), performance leftovers and the Kiwi API surface, with a suggested milestone order. (done)

## M5 — Kiwi browser

- The product browser lives in the separate [Kiwi](https://github.com/mel0k1/Kiwi) repository; mustela-shell stays the engine's test harness.
- Engine capabilities it consumes: stable settings API, privacy policy with per-site rules, profiles, packaging hooks.
