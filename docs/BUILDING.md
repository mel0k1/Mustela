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
./build/mustela --private          # ephemeral in-memory profile
./build/mustela https://example.org
```

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
