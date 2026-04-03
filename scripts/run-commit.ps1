param(
    [Parameter(Mandatory = $false)]
    [string]$CommitMessage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    git add -A

    $stagedFiles = @(git diff --cached --name-only | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($stagedFiles.Count -eq 0) {
        Write-Host "Keine Änderungen zum Committen." -ForegroundColor Yellow
        return
    }

    if ([string]::IsNullOrWhiteSpace($CommitMessage)) {
        $areas = @($stagedFiles |
            ForEach-Object { ($_ -split '[\\/]')[0].ToLowerInvariant() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -Unique)

        $areaSummary = if ($areas.Count -gt 0) {
            ($areas | Select-Object -First 3) -join ", "
        }
        else {
            "project"
        }

        if ($areas.Count -gt 3) {
            $areaSummary = "$areaSummary +$($areas.Count - 3)"
        }

        $CommitMessage = "chore: update $areaSummary ($($stagedFiles.Count) files)"
    }

    git commit --no-verify -m $CommitMessage
    Write-Host "Commit erstellt: $CommitMessage" -ForegroundColor Green
}
finally {
    Pop-Location
}
