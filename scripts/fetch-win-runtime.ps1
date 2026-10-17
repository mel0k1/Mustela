# fetch-win-runtime.ps1 — assemble a runnable WinCairo WebKit runtime for Windows.
#
# The Mustela Windows port (src/shell/win) compiles against the pinned WebKit C API
# headers (scripts/fetch-win-headers.*), but running the shell, the bench or any
# WinCairo-based build requires the WebKit DLLs themselves. Upstream publishes them
# through two channels; this script combines both into a single runtime directory:
#
#   1. WebKitRequirementsWin64.zip — third-party dependency DLLs from the
#      WebKitForWindows/WebKitRequirements releases.
#   2. The WebKit build archive — a zip attached to a green WebKit Buildbot build
#      (builder "Windows-64-bit-Release-Build", formerly "WinCairo-64-bit-Release-Build").
#
# Result layout (default .\webkit-runtime):
#   runtime\bin64\           WebKit.dll + dependencies + MiniBrowser.exe (smoke check)
#
# After assembling, copy (or prepend to PATH) runtime\bin64 next to mustela.exe /
# mustela-bench-win.exe and install the Microsoft VC++ redistributable (vc_redist.x64).
#
# Usage:
#   .\scripts\fetch-win-runtime.ps1 [-Destination .\webkit-runtime]
#   .\scripts\fetch-win-runtime.ps1 -RequirementsTag <tag> [-Builder <name>] [-BuildNumber <n>]
#
# Manual fallback (if Buildbot scraping changes upstream):
#   * open https://build.webkit.org/#/builders , pick "Windows-64-bit-Release-Build",
#     click the latest green Build #, click "Archive" under compile-webkit, download;
#   * download WebKitRequirementsWin64.zip from
#     https://github.com/WebKitForWindows/WebKitRequirements/releases ;
#   * unzip both, merge their bin64 folders, put the DLLs next to the executable.
param(
    [string] $Destination = ".\webkit-runtime",
    [string] $RequirementsTag = "",   # empty = latest WebKitForWindows release
    [string] $Builder = "Windows-64-bit-Release-Build",
    [int] $BuildNumber = 0            # 0 = latest green build
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function New-TempDir {
    return Join-Path ([IO.Path]::GetTempPath()) ("webkit-runtime-" + [Guid]::NewGuid().ToString("N"))
}

function Get-LatestRequirementsAssetUrl {
    $headers = @{ "User-Agent" = "mustela-fetch-win-runtime" }
    if ($RequirementsTag -ne "") {
        return "https://github.com/WebKitForWindows/WebKitRequirements/releases/download/$RequirementsTag/WebKitRequirementsWin64.zip"
    }
    $rel = Invoke-RestMethod -Headers $headers `
        -Uri "https://api.github.com/repos/WebKitForWindows/WebKitRequirements/releases/latest"
    foreach ($asset in $rel.assets) {
        if ($asset.name -eq "WebKitRequirementsWin64.zip") { return $asset.browser_download_url }
    }
    throw "WebKitRequirementsWin64.zip not found in the latest release"
}

function Get-BuildbotArchiveUrl {
    # Resolve builder id, then the build, then look for an s3:// style artifact URL
    # in the build's properties/logs (the "upload"/"transfer-to-s3" step).
    $builders = Invoke-RestMethod -Uri "https://build.webkit.org/api/v2/builders?name=$([Uri]::EscapeDataString($Builder))"
    if (-not $builders.builders -or $builders.builders.Count -eq 0) {
        throw "builder '$Builder' not found on build.webkit.org"
    }
    $builderId = $builders.builders[0].builderid
    $query = "https://build.webkit.org/api/v2/builds?builderid=$builderId&order=-number&limit=20"
    if ($BuildNumber -gt 0) { $query += "&number=$BuildNumber" }
    $builds = Invoke-RestMethod -Uri $query
    foreach ($build in $builds.builds) {
        if ($build.results -ne 0) { continue }   # 0 = success ("green")
        $steps = Invoke-RestMethod -Uri "https://build.webkit.org/api/v2/steps?buildid=$($build.buildid)"
        foreach ($step in $steps.steps) {
            if ($step.name -notmatch "upload|transfer|archive|s3") { continue }
            $logs = Invoke-RestMethod -Uri "https://build.webkit.org/api/v2/logs?stepid=$($step.stepid)"
            foreach ($log in $logs.logs) {
                $body = (Invoke-WebRequest -UseBasicParsing -Uri $log.url).Content
                $match = [regex]::Match($body, "https?://[^\s`"']+\.zip")
                if ($match.Success) { return $match.Value }
            }
        }
        throw "no archive URL found in the latest green build (#$($build.number))"
    }
    throw "no green build found for builder '$Builder'"
}

$runtime = Join-Path $Destination "runtime\bin64"
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$temp = New-TempDir

try {
    $reqUrl = Get-LatestRequirementsAssetUrl
    Write-Host "downloading WebKitRequirements: $reqUrl"
    $reqZip = Join-Path $temp "WebKitRequirementsWin64.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $reqUrl -OutFile $reqZip
    Expand-Archive -Path $reqZip -DestinationPath (Join-Path $temp "req")

    Write-Host "resolving the WebKit build archive from build.webkit.org ..."
    $archiveUrl = Get-BuildbotArchiveUrl
    Write-Host "downloading build archive: $archiveUrl"
    $webZip = Join-Path $temp "webkit-build.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $webZip
    Expand-Archive -Path $webZip -DestinationPath (Join-Path $temp "web")

    # Merge everything that looks like a runtime payload into runtime\bin64.
    foreach ($root in @((Join-Path $temp "req"), (Join-Path $temp "web"))) {
        Get-ChildItem -Path $root -Recurse -Include *.dll, *.exe, *.pdb -File |
            ForEach-Object { Copy-Item $_.FullName $runtime -Force }
    }
    Write-Host ""
    Write-Host "runtime assembled in $runtime"
    Write-Host "smoke check: run $runtime\MiniBrowser.exe"
    Write-Host "then copy WebKit.dll etc. next to mustela.exe or prepend the folder to PATH."
    Write-Host "note: install the VC++ redistributable (vc_redist.x64.exe) if missing."
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
