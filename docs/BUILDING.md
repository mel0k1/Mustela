# Building

## Linux

Requirements: `build-essential`, `cmake >= 3.16`, `pkg-config`, `libwebkit2gtk-4.1-dev`.

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libwebkit2gtk-4.1-dev
```

Build:

```bash
./scripts/build-linux.sh
```

Or directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
```

Run:

```bash
./build/mustela                    # persistent profile
./build/mustela --private          # first tab private, ephemeral profile
./build/mustela https://example.org
```

## Configuration

Settings live in `~/.config/mustela/settings.toml`, created on first run and saved on exit.

| Key | Default | Meaning |
|-----|---------|---------|
| shell.homepage | https://example.org | start page for new tabs |
| shell.search_template | https://duckduckgo.com/?q=%s | `%s` is replaced with the query |
| shell.window_width | 1280 | window width, updated on exit |
| shell.window_height | 800 | window height, updated on exit |
| shell.zoom | 1.0 | default zoom for new tabs, 0.3–3.0 (Ctrl+plus/minus/0 changes the active tab) |
| shell.session_restore | true | reopen the previous session's tabs on start (normal tabs only) |
| downloads.dir | (empty) | download destination; empty means the platform Downloads folder |
| downloads.mode | auto | `auto` saves without asking, `ask` opens a save dialog per download |
| permissions.geolocation | deny | `deny`, `ask` or `allow` for location requests |
| permissions.camera | deny | `deny`, `ask` or `allow` for camera requests |
| permissions.microphone | deny | `deny`, `ask` or `allow` for microphone requests |
| permissions.notifications | deny | `deny`, `ask` or `allow` for notification permission (GTK; Windows auto-denies) |
| security.tls_strict | true | fail loads with invalid TLS certificates and prompt to continue; `false` ignores TLS errors |
| privacy.private_tabs_enabled | true | allow private tabs at all |
| privacy.start_in_private | false | open new tabs in private mode by default |
| privacy.history_enabled | true | record visited pages in history |
| privacy.cookie_policy | no-third-party | `always`, `no-third-party` or `never` |
| privacy.tracking_prevention | true | Intelligent Tracking Prevention on the persistent profile |
| privacy.javascript | true | engine-level JavaScript switch |
| privacy.hyperlink_auditing | false | allow `<a ping>` link auditing |
| privacy.dns_prefetching | false | speculative DNS lookups |
| privacy.block_autoplay | true | media playback requires a user gesture |
| privacy.fingerprint_webgl | false | `canvas.getContext('webgl*')` returns null — WebGL surfaces disappear |
| privacy.fingerprint_canvas | false | canvas readbacks (`toDataURL`, `toBlob`, `getImageData`, `readPixels`) return blank data |
| privacy.fingerprint_fonts | false | block every remote font download |
| privacy.fingerprint_referrer | same-origin | `off`, `same-origin` (no cross-origin Referer) or `no-referrer` |
| privacy.gpc | true | expose `navigator.globalPrivacyControl` and send `Sec-GPC: 1` on every request (GTK; Windows ships the JS side) |
| privacy.blocklist | true | block ~60 built-in tracker/ad domains through a compiled content filter |
| privacy.blocklist_file | (empty) | optional file with extra domains, one per line, `#` comments allowed |
| privacy.dns_over_https | (empty) | DoH resolver URL (RFC 8484); recorded for future runtime wiring — no WebKit flavor exposes a resolver hook yet |
| perf.process_model | per-view | `per-view` (one web process per view, upstream default) or `shared` (a single shared web process — lower memory, weaker site isolation, applies at startup) |
| perf.startup_budget_ms | 0 | startup budget; `0` disables the check, otherwise the shell flashes `startup Nms over budget` when the time to first mapped window exceeds it |
| perf.memory_ceiling_mb | 0 | process-tree RSS ceiling; `0` disables monitoring, otherwise the status bar shows `mem N MB` while the ceiling is exceeded |
| perf.log | true | append startup, page-load and memory samples to `perf.jsonl` in the profile directory |

Profile data (cookies, cache, storage) lives under `~/.local/share/mustela/profiles/default`; private tabs use an in-memory context instead. The compiled remote-font filter is cached under the same profile in `filters/`, and performance samples are appended to `perf.jsonl` there.

Both `settings.toml` and `sites.toml` are watched while the browser runs — edits apply to open tabs without a restart.

### Site rules

Per-host overrides live in `~/.config/mustela/sites.toml`:

```toml
[example.org]
javascript = "deny"

[tracker.example.org]
cookies = "deny"

[maps.example.com]
fingerprint = "allow"
```

A rule for a domain covers its subdomains (`example.org` matches `www.example.org`). The same rules are editable at runtime in the site-rules window (`Ctrl+Shift+S`): buttons act on the selected row, or on the current site when nothing is selected. `fingerprint = "allow"` exempts the site from all fingerprint switches (use for maps, games, canvas apps); `fingerprint = "deny"` forces the strict set on it. `permissions = "allow"` remembers a granted permission for the host (skip the prompt for future requests); `permissions = "deny"` blocks them without asking. Both are also written by the "Always allow" / "Never allow" choices in the permission prompt; private tabs never write rules.

## Shortcuts

| Keys | Action |
|------|--------|
| Ctrl+T | new tab |
| Ctrl+Shift+P | new private tab |
| Ctrl+W | close tab |
| Ctrl+L | focus the address bar |
| Ctrl+H | history window |
| Ctrl+F | find in page (Enter / Shift+Enter — next / previous match, Escape closes) |
| F11 | toggle window fullscreen (HTML5 element fullscreen follows on GTK) |
| Ctrl+plus / Ctrl+minus / Ctrl+0 | zoom in / out / reset (30%–300%) |
| Ctrl+Shift+Del | clear browsing data (cookies, cache, storage, IndexedDB) |
| Ctrl+Shift+S | site rules window |
| Ctrl+R / F5 | reload |
| Alt+Left / Alt+Right | back / forward |
| Escape | stop loading |

Linux shortcuts apply to the GTK port. The Windows port shares Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+R, F5, Ctrl+H, Ctrl+F, Ctrl+Shift+Del, Alt+Left, Alt+Right and the zoom trio; Escape and Ctrl+Shift+S (site rules window) are GTK-only for now.

## Popups, dialogs, find, downloads, permissions

Links with `target=_blank` and `window.open` open as new tabs and inherit the originating tab's privacy mode: a popup from a private tab stays on the private (ephemeral) context or data store, and per-site policies are resolved for the popup URL like for any other tab. `window.close()` inside a popup closes its tab.

`alert`, `confirm`, `prompt` and beforeunload confirmation show native dialogs (GTK message dialogs, Windows message boxes plus a modal prompt window). Text coming from the page is sanitized before display: control characters are stripped, line endings are normalized, and very long messages are cut on UTF-8 boundaries. Prompt cancellation returns an empty string.

Find in page (Ctrl+F) searches case-insensitively with wrap-around, highlights all matches (GTK scrollbar marks and highlighting; Windows match overlay), shows the match count, and re-runs the current query when you switch tabs while the bar is open.

Hovering a link shows its destination in the status bar on both ports.

Downloads resolve their destination from `downloads.dir` (empty = the platform Downloads folder), never overwrite — a conflicting file gets a `name (2).ext` suffix — and sanitize the suggested file name in the core (`ui_text::sanitize_filename`). `downloads.mode = "ask"` opens a save dialog per download; cancelling it cancels the download. While a download runs, the status bar shows its name and percentage.

Permission requests (geolocation, camera, microphone, notifications) resolve through the core policy: the global `permissions.*` choice decides deny/prompt/allow, and a per-site `permissions` rule in `sites.toml` overrides it — `allow` grants without asking, `deny` blocks silently. A prompt offers Allow / Deny / Always allow / Never allow; the last two write the per-site rule. Unknown permission kinds are denied.

Invalid TLS certificates prompt before continuing on GTK (allow once per load, strict policy by default); on Windows the C API has no certificate-acceptance call, so such loads fail. HTTP authentication prompts for user and password on both ports; credentials last for the session only.

If a web process dies, the tab reloads itself up to twice automatically (a crash-loop guard), the status bar shows the reason, and after the guard the usual Ctrl+R applies. The counter resets after a successful load.

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`mustela_core_tests` covers settings round-trips (including partial files and fallbacks), privacy mapping and gating invariants, `host_from_uri` parsing, site-rule suffix matching and persistence, `resolve_site` precedence, history escaping/limits/dedup, fingerprint presets and referrer substitution, browsing-data kinds, URL normalization, the perf module (statistics, record codec, log trimming, `/proc` RSS parsing, budget and ceiling verdicts) and the `ui_text` sanitization rules (control characters, CR/LF normalization, UTF-8-safe truncation for dialog text and find queries). It runs on every push in CI on both Linux and Windows.

## Benchmarks

`mustela-bench` measures real page loads through a minimal offscreen shell. It is built on Linux (WebKitGTK) with the default options:

```bash
cmake --build build --target mustela-bench
xvfb-run -a env WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 \
  ./build/mustela-bench --url "file://$PWD/tests/fixtures/bench.html" --runs 5 --max-ms 10000
```

Options: `--url` (required), `--runs N` (default 5), `--max-ms M` — fail with exit code 2 when the median exceeds the budget, `--out FILE` — write the JSON result to a file. The output is one JSON line: `n`, `min_ms`, `median_ms`, `p95_ms`, `max_ms`. Exit codes: 0 ok, 2 budget exceeded or no completed loads, 3 load failed, 4 load timed out. CI runs exactly this command (with `--max-ms 10000`) on every push; the sandbox environment variable is only needed inside headless containers.

On Windows the same harness exists as `mustela-bench-win` (WinCairo C API): it loads pages inside a hidden host window with a non-persistent data store, so a benchmark run leaves no profile behind. Build it against a WebKit SDK directory:

```bat
cmake -S . -B build -DMUSTELA_WEBKIT_SDK=C:/path/to/sdk
cmake --build build --config Release --target mustela-bench-win
build\Release\mustela-bench-win.exe --url "file:///C:/tests/fixtures/bench.html" --runs 5 --max-ms 10000
```

CLI, JSON output and exit codes are identical to the GTK bench. CI compile-checks `mustela-bench-win` against the pinned upstream headers on every push; running it requires a WinCairo runtime (`WebKit.dll` and its dependencies) next to the executable.

Page-load samples recorded by the shells and by `mustela-bench`-style runs land in `perf.jsonl` in the profile directory. `scripts/perf_check.py` summarizes them and compares against a baseline for regression tracking:

```bash
python3 scripts/perf_check.py ~/.local/share/mustela/profiles/default/perf.jsonl --update --baseline perf-baseline.json
python3 scripts/perf_check.py ~/.local/share/mustela/profiles/default/perf.jsonl --baseline perf-baseline.json --tolerance 1.25 --slack-ms 100
```

The first run records the baseline (per-kind median/p95/max), the second fails with exit code 1 when a median grows beyond `baseline * tolerance + slack`. Memory samples are compared as absolute bytes with the same slack logic.

## Windows

The Windows port embeds the upstream [WinCairo](https://github.com/WebKit/WebKit) flavor of WebKit through its C API (`WebKit2_C.h` umbrella plus the win `WKView` and data-store headers). The shell source lives in `src/shell/win/shell_win.cpp`.

Two paths:

1. **Without a WebKit SDK** CMake still configures and builds the core, the tests and a placeholder shell:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

2. **With a WinCairo SDK** the real shell builds. Build WebKit from upstream first (WebKit wiki: BuildingCairoOnWindows), then point CMake at a directory containing the WebKit C API headers and import libraries:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUSTELA_WEBKIT_SDK=path/to/sdk
cmake --build build --config Release
```

The SDK directory is expected to have `include/WebKit/*.h` (the C API headers from the WebKit source tree: `Source/WebKit/UIProcess/API/C`, `Source/WebKit/UIProcess/API/C/win`, `Source/WebKit/Shared/API/c`) and `lib/` with the WebKit import libraries (`WebKit`, `WebCore`, `WTF`, `JavaScriptCore`, `PAL`). At runtime the WebKit DLLs, helper processes and the WinCairo dependency DLLs (from `WinCairoRequirements`) must sit next to `mustela.exe`.

CI compiles `shell_win.cpp` against 86 pinned upstream headers on every push (job `windows shell`), so the Windows sources are never farther than one commit from reality. The header set is listed in `scripts/win-headers.txt` and fetched by `scripts/fetch-win-headers.ps1`.

Profile data on Windows lives under `%APPDATA%\mustela`: `settings.toml` and `sites.toml` at the root, `profiles\default\` for cookies, cache, storage, the compiled font filter and `perf.jsonl`. Both TOML files are watched with `ReadDirectoryChangesW` — edits apply live, same as on Linux. The perf module works identically on both ports: startup is measured to first paint (`WM_PAINT`), memory is sampled from the process tree via `GetProcessMemoryInfo`, and page loads are timed between the provisional-start and finish navigation callbacks.

The only perf key that is not applied live is `perf.process_model` — the process model is fixed when the contexts are created, so changing it requires a restart.
