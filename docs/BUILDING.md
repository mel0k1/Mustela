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

Profile data (cookies, cache, storage) lives under `~/.local/share/mustela/profiles/default`; private tabs use an in-memory context instead.

## Shortcuts

| Keys | Action |
|------|--------|
| Ctrl+T | new tab |
| Ctrl+Shift+P | new private tab |
| Ctrl+W | close tab |
| Ctrl+L | focus the address bar |
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
