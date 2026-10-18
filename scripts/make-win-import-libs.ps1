# make-win-import-libs.ps1 — generate MSVC import libraries from the WinCairo runtime DLLs.
#
# The buildbot runtime archive ships DLLs only, while linking mustela.exe /
# mustela_view_win with MSVC needs import libraries. The .def files are built by
# scripts/pe_exports.py (a dependency-free PE export directory parser — dumpbin's
# text format proved too brittle to scrape) and lib.exe produces the .lib files.
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

$peExports = Join-Path $PSScriptRoot "pe_exports.py"
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    throw "python is required on PATH (it ships with the GitHub Windows runners)"
}

$names = @("WebKit2", "JavaScriptCore", "WebKit", "WebCore", "WTF", "PAL")
$found = @()
foreach ($name in $names) {
    if (Test-Path (Join-Path $RuntimeDir "$name.dll")) { $found += $name }
}
if ($found.Count -eq 0) {
    throw "none of the expected WebKit runtime DLLs found in $RuntimeDir"
}

foreach ($name in $found) {
    $dll = Join-Path $RuntimeDir "$name.dll"
    $listing = & python $peExports $dll
    if ($LASTEXITCODE -ne 0) {
        throw "pe_exports.py failed for $dll"
    }
    $symbols = @($listing | Where-Object { $_ -and $_.Trim() -ne "" })
    if ($symbols.Count -eq 0) {
        throw "no exports found in $dll"
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
Write-Host "import libraries for $($found -join ', ') written to ${OutDir}"
