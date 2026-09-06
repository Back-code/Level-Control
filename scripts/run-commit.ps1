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
        Write-Host 'Keine Aenderungen zum Committen.' -ForegroundColor Yellow
        return
    }

    if ([string]::IsNullOrWhiteSpace($CommitMessage)) {
        $areas = @($stagedFiles |
            ForEach-Object { ($_ -split '[\\/]')[0].ToLowerInvariant() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -Unique)

        $areaSummary = if ($areas.Count -gt 0) {
            ($areas | Select-Object -First 3) -join ', '
        }
        else {
            'project'
        }

        if ($areas.Count -gt 3) {
            $areaSummary = '{0} +{1}' -f $areaSummary, ($areas.Count - 3)
        }

        $stagedFileCount = $stagedFiles.Count
        $CommitMessage = 'chore: update {0} - {1} files' -f $areaSummary, $stagedFileCount
    }

    git commit --no-verify -m $CommitMessage
    Write-Host ('Commit erstellt: {0}' -f $CommitMessage) -ForegroundColor Green
}
finally {
    Pop-Location
}
