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

    # Testbuild-Suffix verwalten (A-Z)
    $suffixFile = Join-Path $repoRoot ".testbuild-suffix"
    $suffix = "A"
    if (Test-Path $suffixFile) {
        $oldSuffix = Get-Content $suffixFile -Raw
        if ($oldSuffix -match '^[A-Z]$') {
            if ($oldSuffix -eq 'Z') {
                $suffix = 'A'
            } else {
                $suffix = [char](([byte][char]$oldSuffix) + 1)
            }
        }
    }
    Set-Content $suffixFile $suffix
    $versionStrWithSuffix = "$versionStr|$suffix"
    Write-Host "Test-Deploy (Arbeitsstand, keine Versionserhöhung, aktuell: v$versionStrWithSuffix)..." -ForegroundColor Cyan


    # Version mit Suffix in Header schreiben
    python scripts/inject_version.py $suffix

    npm --prefix ui run build
    .\.venv\Scripts\platformio.exe run -t upload
    .\.venv\Scripts\platformio.exe run -t uploadfs

    Write-Host "Test-Deploy abgeschlossen." -ForegroundColor Green
}
finally {
    Pop-Location
}
