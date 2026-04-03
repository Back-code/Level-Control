# Salzstand: Test
# Flasht den aktuellen Arbeitsstand auf den ESP – ohne Versionserhöhung.
# Zum Prüfen von Änderungen vor dem Push.

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

    Write-Host "Test-Deploy (Arbeitsstand, keine Versionserhöhung, aktuell: v$versionStr)..." -ForegroundColor Cyan

    npm --prefix ui run build
    & $venvPython -m platformio run -t upload
    & $venvPython -m platformio run -t uploadfs

    Write-Host "Test-Deploy abgeschlossen." -ForegroundColor Green
}
finally {
    Pop-Location
}
