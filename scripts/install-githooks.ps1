Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

git config core.hooksPath githooks
Write-Host "Configured git hooks path: githooks"
Write-Host "Note: ensure githooks/pre-commit is executable (Git Bash handles this on Windows)."

