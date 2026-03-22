param(
    [Parameter(Mandatory = $true)]
    [string]$CommitMessage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    npm --prefix ui run build
    .\.venv\Scripts\pio.exe run
    .\.venv\Scripts\pio.exe run -t upload
    .\.venv\Scripts\pio.exe run -t uploadfs

    git add -A
    git commit -m $CommitMessage

    .\.venv\Scripts\pio.exe run
    .\.venv\Scripts\pio.exe run -t upload
    .\.venv\Scripts\pio.exe run -t uploadfs

    git push origin main
    node scripts/prepare-release.js

    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    $releaseArgs = @(
        'release', 'create', "v$versionStr",
        "release/v$versionStr/salzstand-v$versionStr-app.bin",
        "release/v$versionStr/salzstand-v$versionStr-web-ui.bin",
        "release/v$versionStr/salzstand-v$versionStr-bootloader.bin",
        "release/v$versionStr/salzstand-v$versionStr-partitions.bin",
        "release/v$versionStr/manifest.json",
        "release/v$versionStr/SHA256SUMS.txt",
        '--repo', 'Back-code/Salzstand',
        '--title', "Salzstand v$versionStr",
        '--notes-file', "release/v$versionStr/release-notes.txt"
    )
    & gh @releaseArgs
}
finally {
    Pop-Location
}