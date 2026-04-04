$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseRoot = Join-Path $repoRoot 'release'
Push-Location $repoRoot

try {
    # Level-Control: Release
    # Baut Firmware + LittleFS frisch fuer die aktuelle Version,
    # erstellt dann Release-Artefakte und veröffentlicht ein GitHub-Release.
    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    Write-Host "Erstelle GitHub-Release v$versionStr..." -ForegroundColor Cyan

    $venvPython = Join-Path $repoRoot ".venv\Scripts\python.exe"
    if (-not (Test-Path $venvPython)) {
        throw "Python aus .venv nicht gefunden: $venvPython"
    }

    Write-Host "Baue Release-Artefakte frisch fuer v$versionStr..." -ForegroundColor Cyan
    npm --prefix ui run build
    & $venvPython -m platformio run
    & $venvPython -m platformio run -t buildfs

    node scripts/prepare-release.js

    $releaseArgs = @(
        'release', 'create', "v$versionStr",
        "release/v$versionStr/level-control-v$versionStr-app.bin",
        "release/v$versionStr/level-control-v$versionStr-web-ui.bin",
        "release/v$versionStr/level-control-v$versionStr-bootloader.bin",
        "release/v$versionStr/level-control-v$versionStr-partitions.bin",
        "release/v$versionStr/manifest.json",
        "release/v$versionStr/SHA256SUMS.txt",
        '--repo', 'Back-code/Level-Control',
        '--title', "Level-Control v$versionStr",
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
