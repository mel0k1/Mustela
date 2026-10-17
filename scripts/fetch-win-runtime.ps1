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
    [string] $RequirementsTag = "v2025.04.02",   # pinned by default; empty = latest via the GitHub API
    [string] $Builder = "Windows-64-bit-Release-Build",
    [int] $BuildNumber = 0            # 0 = latest green build
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function New-TempDir {
    $path = Join-Path ([IO.Path]::GetTempPath()) ("webkit-runtime-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
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
    # Resolve the builder, latest green build, then construct the public S3 archive URL
    # from the build properties (identifier = fullPlatform-archForUpload-configuration,
    # archive_revision = "NNNNNN@branch") and verify it with a HEAD request.
    $builders = Invoke-RestMethod -Headers @{ "User-Agent" = "mustela-fetch-win-runtime" } `
        -Uri "https://build.webkit.org/api/v2/builders?name=$([Uri]::EscapeDataString($Builder))"
    if (-not $builders.builders -or $builders.builders.Count -eq 0) {
        throw "builder '$Builder' not found on build.webkit.org"
    }
    $builderId = $builders.builders[0].builderid
    $query = "https://build.webkit.org/api/v2/builds?builderid=$builderId&order=-number&limit=20"
    if ($BuildNumber -gt 0) { $query += "&number=$BuildNumber" }
    $builds = Invoke-RestMethod -Headers @{ "User-Agent" = "mustela-fetch-win-runtime" } -Uri $query
    foreach ($build in $builds.builds) {
        if ($build.results -ne 0) { continue }   # 0 = success ("green")
        $props = Invoke-RestMethod -Headers @{ "User-Agent" = "mustela-fetch-win-runtime" } `
            -Uri "https://build.webkit.org/api/v2/builds/$($build.buildid)/properties"
        $p = $props.properties[0]
        $revision = $p.'archive_revision'[0]
        $identifier = "$($p.'fullPlatform'[0])-$($p.'archForUpload'[0])-$($p.'configuration'[0])"
        if (-not $revision) {
            throw "build #$($build.number) has no archive_revision property"
        }
        $base = "https://s3-us-west-2.amazonaws.com/archives.webkit.org"
        $candidates = @(
            "$base/$identifier/$revision.zip",
            "$base/$identifier-$Builder/$revision.zip"
        )
        foreach ($candidate in $candidates) {
            try {
                $head = Invoke-WebRequest -UseBasicParsing -Method Head -Uri $candidate -TimeoutSec 60
                if ($head.StatusCode -eq 200) { return $candidate }
            } catch {
                Write-Host "no archive at $candidate"
            }
        }
        throw "no archive URL found for build #$($build.number) (revision $revision)"
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
        Get-ChildItem -Path $root -Recurse -Directory -Filter *.resources |
            ForEach-Object {
                $target = Join-Path $runtime $_.Name
                if (Test-Path $target) { Remove-Item -Recurse -Force $target }
                Copy-Item $_.FullName $target -Recurse
            }
    }
    Write-Host ""
    Write-Host "runtime assembled in $runtime"
    Write-Host "smoke check: run $runtime\MiniBrowser.exe"
    Write-Host "then copy WebKit2.dll etc. next to mustela.exe or prepend the folder to PATH."
    Write-Host "note: install the VC++ redistributable (vc_redist.x64.exe) if missing."
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
