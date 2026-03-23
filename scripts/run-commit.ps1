param(
    [Parameter(Mandatory = $true)]
    [string]$CommitMessage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    git add -A
    git commit --no-verify -m $CommitMessage
    Write-Host "Commit erstellt: $CommitMessage" -ForegroundColor Green
}
finally {
    Pop-Location
}
