# Level-Control: Deploy
# Baut UI und Firmware mit der aktuellen (gepushten) Version
# und flasht beides auf den ESP.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $version = Get-Content version.json | ConvertFrom-Json
    $versionStr = "$($version.major).$($version.minor).$($version.commit)"
    $venvPython = Join-Path $repoRoot ".venv\Scripts\python.exe"

    if (-not (Test-Path $venvPython)) {
        throw "Python aus .venv nicht gefunden: $venvPython"
    }

    Write-Host "Deploye v$versionStr auf ESP..." -ForegroundColor Cyan

    npm --prefix ui run build
    & $venvPython -m platformio run -t upload
    & $venvPython -m platformio run -t uploadfs

    Write-Host "Deploy v$versionStr abgeschlossen." -ForegroundColor Green
    Write-Host "Naechster Schritt: 'Level-Control: Release' ausfuehren um ein GitHub-Release zu erstellen." -ForegroundColor Yellow
}
finally {
    Pop-Location
}

