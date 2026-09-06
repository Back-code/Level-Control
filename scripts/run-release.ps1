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
    node scripts/test-ota-release.mjs

    $ghCommand = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $ghCommand) {
        $ghCandidates = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\GitHub.cli_*\bin\gh.exe') -File -ErrorAction SilentlyContinue
        $ghCommand = $ghCandidates | Select-Object -First 1
    }
    if (-not $ghCommand) {
        throw "GitHub CLI 'gh' nicht gefunden. Bitte gh installieren und authentifizieren."
    }
    $ghExecutable = if ($ghCommand -is [System.Management.Automation.CommandInfo]) {
        $ghCommand.Source
    }
    else {
        $ghCommand.FullName
    }

    $releaseArgs = @(
        'release', 'create', "v$versionStr",
        "release/v$versionStr/level-control-v$versionStr-image.bin",
        "release/v$versionStr/level-control-v$versionStr-image.sig",
        "release/v$versionStr/level-control-v$versionStr-filesystem.bin",
        "release/v$versionStr/level-control-v$versionStr-filesystem.sig",
        "release/v$versionStr/level-control-v$versionStr-bootloader.bin",
        "release/v$versionStr/level-control-v$versionStr-partitions.bin",
        "release/v$versionStr/level-control-v$versionStr-app.bin",
        "release/v$versionStr/level-control-v$versionStr-web-ui.bin",
        "release/v$versionStr/SHA256SUMS.txt",
        '--repo', 'Back-code/Level-Control',
        '--title', "Level-Control v$versionStr",
        '--notes-file', "release/v$versionStr/release-notes.txt"
    )
    & $ghExecutable @releaseArgs
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub-Release konnte nicht veröffentlicht werden (Exitcode $LASTEXITCODE)."
    }

    node scripts/test-ota-hardware.mjs

    # Behalte nur die letzten 5 Versionen lokal, lösche älter Versionen
    if (Test-Path $releaseRoot) {
        $versions = Get-ChildItem -Path $releaseRoot -Directory | 
            Where-Object { $_.Name -match '^v\d+\.\d+\.\d+$' } |
            Sort-Object -Property Name -Descending

        if ($versions.Count -gt 5) {
            $versionsToDelete = $versions[5..($versions.Count - 1)]
            foreach ($version in $versionsToDelete) {
                Write-Host "Lösche alte Version: $($version.Name)" -ForegroundColor Yellow
                Remove-Item -Path $version.FullName -Recurse -Force
            }
        }

        $retainedVersions = $versions[0..([Math]::Min(4, $versions.Count - 1))] | 
            ForEach-Object { $_.Name } | 
            Sort-Object
        Write-Host "Behalten Versionen: $($retainedVersions -join ', ')" -ForegroundColor Cyan
    }

    Write-Host "Release v$versionStr erfolgreich veröffentlicht." -ForegroundColor Green
}
finally {
    Pop-Location
}
