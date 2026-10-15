param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [string]$Commit = "8ece20377cd3f0aa0aadb9d61535a703c43f4612",

    [string]$Repo = "WebKit/WebKit"
)

$ErrorActionPreference = "Stop"

$manifest = Join-Path $PSScriptRoot "win-headers.txt"
$headers = Get-Content $manifest | Where-Object { $_.Trim() -ne "" }

$target = Join-Path $Destination "WebKit"
New-Item -ItemType Directory -Force -Path $target | Out-Null

$paths = @(
    "Source/WebKit/UIProcess/API/C",
    "Source/WebKit/UIProcess/API/C/win",
    "Source/WebKit/Shared/API/c",
    "Source/WebKit/Shared/API/c/win"
)

$base = "https://raw.githubusercontent.com/$Repo/$Commit"

foreach ($name in $headers) {
    $outFile = Join-Path $target $name
    $fetched = $false
    foreach ($dir in $paths) {
        $url = "$base/$dir/$name"
        try {
            Invoke-WebRequest -Uri $url -OutFile $outFile -MaximumRetryCount 3 -ErrorAction Stop
            $content = Get-Content $outFile -Raw
            if ($content -match "^404: Not Found") {
                Remove-Item $outFile -ErrorAction SilentlyContinue
                continue
            }
            $fetched = $true
            break
        } catch {
            continue
        }
    }
    if (-not $fetched) {
        throw "Failed to fetch header $name from $Repo@$Commit"
    }
}

Write-Host "Fetched $($headers.Count) WebKit C API headers into $target"
