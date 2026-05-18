#Requires -Version 5.1

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pathTarget = "User"

$existing = [Environment]::GetEnvironmentVariable("PATH", $pathTarget)
$parts = if ([string]::IsNullOrWhiteSpace($existing)) {
    @()
} else {
    $existing.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
}

if ($parts -contains $scriptDir) {
    Write-Host "Zeri is already in your PATH ($scriptDir)."
    Write-Host "Open a new terminal and run 'zeri' from anywhere."
} else {
    $newPath = ($parts + $scriptDir) -join ';'
    [Environment]::SetEnvironmentVariable("PATH", $newPath, $pathTarget)
    Write-Host "Zeri has been added to your PATH."
    Write-Host "Open a new terminal and run 'zeri' from anywhere."
}

<#

Setup.ps1 for users who extract the release ZIP manually.
Resolves its own directory from $MyInvocation.MyCommand.Definition (not CWD) and
permanently adds it to the current user's PATH. Idempotent: safe to run multiple times.
Users run: .\setup.ps1 — then restart terminal — then 'zeri' works from any directory.

#>
