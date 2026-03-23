# Salzstand: Push
# Pusht alle lokalen Commits zu GitHub.
# Der pre-push-Hook erhöht dabei automatisch einmalig die Versionsnummer,
# baut die UI neu und erstellt einen Versions-Commit der mitgepusht wird.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    Write-Host "Push zu GitHub (pre-push-Hook erhöht Version einmalig)..." -ForegroundColor Cyan

    git push origin main

    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    Write-Host "Push abgeschlossen. Neue Version: v$versionStr" -ForegroundColor Green
    Write-Host "Naechster Schritt: 'Salzstand: Deploy' ausfuehren um v$versionStr auf den ESP zu flashen." -ForegroundColor Yellow
}
finally {
    Pop-Location
}
