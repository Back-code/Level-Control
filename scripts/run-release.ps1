$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseRoot = Join-Path $repoRoot 'release'
Push-Location $repoRoot

try {
    # Salzstand: Release
    # Erstellt Release-Artefakte aus den zuletzt gebauten Binaries und veröffentlicht ein GitHub-Release.
    # Voraussetzung: Schritt "Deploy" wurde bereits erfolgreich ausgeführt (Firmware gebaut und geflasht).
    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    Write-Host "Erstelle GitHub-Release v$versionStr..." -ForegroundColor Cyan

    node scripts/prepare-release.js

    $releaseArgs = @(
        'release', 'create', "v$versionStr",
        "release/v$versionStr/salzstand-v$versionStr-app.bin",
        "release/v$versionStr/salzstand-v$versionStr-web-ui.bin",
        "release/v$versionStr/salzstand-v$versionStr-bootloader.bin",
        "release/v$versionStr/salzstand-v$versionStr-partitions.bin",
        "release/v$versionStr/manifest.json",
        "release/v$versionStr/SHA256SUMS.txt",
        '--repo', 'Back-code/Level-Control',
        '--title', "Salzstand v$versionStr",
        '--notes-file', "release/v$versionStr/release-notes.txt"
    )
    & gh @releaseArgs

    if (Test-Path $releaseRoot) {
        Remove-Item -Path $releaseRoot -Recurse -Force
    }

    Write-Host "Release v$versionStr erfolgreich veröffentlicht." -ForegroundColor Green
}
finally {
    Pop-Location
}