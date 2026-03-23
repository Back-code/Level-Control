# Salzstand: Deploy
# Baut UI und Firmware mit der aktuellen (gepushten) Version
# und flasht beides auf den ESP.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    Write-Host "Deploye v$versionStr auf ESP..." -ForegroundColor Cyan

    npm --prefix ui run build
    .\.venv\Scripts\platformio.exe run -t upload
    .\.venv\Scripts\platformio.exe run -t uploadfs

    Write-Host "Deploy v$versionStr abgeschlossen." -ForegroundColor Green
    Write-Host "Naechster Schritt: 'Salzstand: Release' ausfuehren um ein GitHub-Release zu erstellen." -ForegroundColor Yellow
}
finally {
    Pop-Location
}
