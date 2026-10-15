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

Profile data (cookies, cache, storage) lives under `~/.local/share/mustela/profiles/default`; private tabs use an in-memory context instead. The compiled remote-font filter is cached under the same profile in `filters/`.

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

A rule for a domain covers its subdomains (`example.org` matches `www.example.org`). The same rules are editable at runtime in the site-rules window (`Ctrl+Shift+S`): buttons act on the selected row, or on the current site when nothing is selected. `fingerprint = "allow"` exempts the site from all fingerprint switches (use for maps, games, canvas apps); `fingerprint = "deny"` forces the strict set on it.

## Shortcuts

| Keys | Action |
|------|--------|
| Ctrl+T | new tab |
| Ctrl+Shift+P | new private tab |
| Ctrl+W | close tab |
| Ctrl+L | focus the address bar |
| Ctrl+H | history window |
| Ctrl+Shift+Del | clear browsing data (cookies, cache, storage, IndexedDB) |
| Ctrl+Shift+S | site rules window |
| Ctrl+R / F5 | reload |
| Alt+Left / Alt+Right | back / forward |
| Escape | stop loading |

Linux shortcuts apply to the GTK port. The Windows port shares Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+R, F5, Ctrl+H, Ctrl+Shift+Del, Alt+Left and Alt+Right; Escape and Ctrl+Shift+S (site rules window) are GTK-only for now.

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`mustela_core_tests` covers settings round-trips (including partial files and fallbacks), privacy mapping and gating invariants, `host_from_uri` parsing, site-rule suffix matching and persistence, `resolve_site` precedence, history escaping/limits/dedup, fingerprint presets and referrer substitution, browsing-data kinds and URL normalization. It runs on every push in CI on both Linux and Windows.

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

CI compiles `shell_win.cpp` against 80 pinned upstream headers on every push (job `windows shell`), so the Windows sources are never farther than one commit from reality. The header set is listed in `scripts/win-headers.txt` and fetched by `scripts/fetch-win-headers.ps1`.

Profile data on Windows lives under `%APPDATA%\mustela`: `settings.toml` and `sites.toml` at the root, `profiles\default\` for cookies, cache, storage and the compiled font filter. Both TOML files are watched with `ReadDirectoryChangesW` — edits apply live, same as on Linux.
