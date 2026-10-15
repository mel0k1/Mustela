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

## Windows (in progress)

The Windows port targets the upstream [WinCairo](https://github.com/WebKit/WebKit) flavor of WebKit. The port source lives in `src/shell/win/` and currently builds into a placeholder binary.

Planned path:

1. Build WinCairo WebKit from upstream (WebKit wiki: BuildingCairoOnWindows).
2. Point CMake at the built WebKit libraries.
3. Replace the stub in `src/shell/win/shell_win.cpp` with a real shell over the WinCairo API.

Until then, CMake on Windows configures and builds the stub with MSVC:

```powershell
cmake -S . -B build
cmake --build build --config Release
```
