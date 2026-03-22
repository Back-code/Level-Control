param(
    [Parameter(Mandatory = $true)]
    [string]$CommitMessage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    npm --prefix ui run build
    .\.venv\Scripts\pio.exe run
    .\.venv\Scripts\pio.exe run -t upload
    .\.venv\Scripts\pio.exe run -t uploadfs

    git add -A
    git commit --no-verify -m $CommitMessage
}
finally {
    Pop-Location
}