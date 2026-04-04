# Level-Control: Push
# Bumpt nur dann die Version, wenn firmware-/ui-relevante Dateien geändert wurden.
# Bei reinen Doku/Repo-Aenderungen wird ohne Versionsbump nur Git gepusht.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $aheadCount = [int](git rev-list --count origin/main..HEAD)
    $pendingFiles = @(git diff --name-only origin/main..HEAD)

    $deviceChangePatterns = @(
        '^src/',
        '^include/',
        '^ui/',
        '^data/',
        '^lib/',
        '^platformio\.ini$',
        '^partitions\.csv$'
    )

    $hasDeviceChanges = $false
    foreach ($file in $pendingFiles) {
        foreach ($pattern in $deviceChangePatterns) {
            if ($file -match $pattern) {
                $hasDeviceChanges = $true
                break
            }
        }
        if ($hasDeviceChanges) { break }
    }

    if ($hasDeviceChanges) {
        Write-Host "Firmware/UI-Aenderungen erkannt: Version wird erhoeht..." -ForegroundColor Cyan
        node scripts/bump-version.js

        $version = Get-Content version.json | ConvertFrom-Json
        $versionStr = "$($version.major).$($version.minor).$($version.commit)"
        Write-Host "Neue Version: v$versionStr" -ForegroundColor Cyan

        npm --prefix ui run build
        git add version.json data/index.html data/assets
        git commit --no-verify -m "v$versionStr"

        git push --no-verify origin main

        Write-Host "Push abgeschlossen: v$versionStr" -ForegroundColor Green
        Write-Host "Naechster Schritt: 'Level-Control: Deploy' fuer v$versionStr auf ESP." -ForegroundColor Yellow
    }
    else {
        if ($aheadCount -eq 0) {
            Write-Host "Keine lokalen Commits zum Pushen vorhanden." -ForegroundColor Yellow
        }
        else {
            Write-Host "Keine Firmware/UI-Aenderungen erkannt: push ohne Versionsbump..." -ForegroundColor Cyan
            git push --no-verify origin main
            Write-Host "Push ohne Versionsbump abgeschlossen." -ForegroundColor Green
        }
    }
}
finally {
    Pop-Location
}

