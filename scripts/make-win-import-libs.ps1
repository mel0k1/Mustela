# make-win-import-libs.ps1 — generate MSVC import libraries from the WinCairo runtime DLLs.
#
# The buildbot runtime archive ships DLLs only (bin64), while linking mustela.exe /
# mustela_view_win.lib with MSVC needs import libraries. This script extracts the
# export tables (dumpbin), writes .def files and produces .lib files with lib.exe.
#
# Usage:
#   .\scripts\make-win-import-libs.ps1 [-RuntimeDir .\webkit-runtime\runtime\bin64] [-OutDir .\build\sdk\lib]
param(
    [string] $RuntimeDir = ".\webkit-runtime\runtime\bin64",
    [string] $OutDir = ".\build\sdk\lib"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $RuntimeDir)) {
    throw "runtime directory not found: $RuntimeDir"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$names = @("WebKit2", "JavaScriptCore", "WebKit", "WebCore", "WTF", "PAL")
$found = @()
foreach ($name in $names) {
    $dll = Join-Path $RuntimeDir "$name.dll"
    if (Test-Path $dll) { $found += $name }
}
if ($found.Count -eq 0) {
    throw "none of the expected WebKit runtime DLLs found in $RuntimeDir"
}
foreach ($name in $found) {
    $dll = Join-Path $RuntimeDir "$name.dll"
    if (-not (Test-Path $dll)) {
        Write-Host "skip $name.dll (not in runtime)"
        continue
    }
    $exports = & dumpbin /nologo /exports $dll 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $dll"
    }
    $symbols = @()
    foreach ($line in ($exports -split "`r?`n")) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]{4,}\s+[0-9A-Fa-f]+\s+(\S+?)\s*$') {
            $symbols += $matches[1]
        }
    }
    if ($symbols.Count -eq 0) {
        throw "no exports parsed from $dll"
    }
    $def = Join-Path $OutDir "$name.def"
    Set-Content -Path $def -Value "LIBRARY $name"
    Add-Content -Path $def -Value "EXPORTS"
    foreach ($symbol in $symbols) {
        Add-Content -Path $def -Value "    $symbol"
    }
    $lib = Join-Path $OutDir "$name.lib"
    & lib /nologo /def:$def /machine:x64 /out:$lib
    if ($LASTEXITCODE -ne 0) {
        throw "lib.exe failed for $def"
    }
    Write-Host "$name.lib <- $($symbols.Count) exports"
}

Write-Host ""
Write-Host "import libraries written to $OutDir: $($found -join ', ')"
