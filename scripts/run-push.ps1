# Salzstand: Push
# Bumpt die Version, erstellt einen Versions-Commit und pusht alles zu GitHub.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    Write-Host "Version erhoehen..." -ForegroundColor Cyan
    node scripts/bump-version.js

    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    Write-Host "Neue Version: v$versionStr" -ForegroundColor Cyan

    npm --prefix ui run build
    git add version.json data/index.html data/assets
    git commit --no-verify -m "v$versionStr"

    git push --no-verify origin main

    Write-Host "Push abgeschlossen: v$versionStr" -ForegroundColor Green
    Write-Host "Naechster Schritt: 'Salzstand: Deploy' fuer v$versionStr auf ESP." -ForegroundColor Yellow
}
finally {
    Pop-Location
}
