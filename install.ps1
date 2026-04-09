$ErrorActionPreference = "Stop"
$Repo = "ilmartotch/ReplZeriEmgine"
$InstallDir = "$env:LOCALAPPDATA\Zeri"

Write-Host "Installazione Zeri..."

$Release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
$Asset = $Release.assets | Where-Object { $_.name -like "*windows*" } | Select-Object -First 1
if (-not $Asset) {
    Write-Error "Nessuna release Windows trovata per $Repo"
    exit 1
}

$TmpZip = Join-Path $env:TEMP "zeri-install.zip"
Write-Host "Download da $($Asset.browser_download_url)"
Invoke-WebRequest $Asset.browser_download_url -OutFile $TmpZip

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Expand-Archive $TmpZip -DestinationPath $InstallDir -Force
Remove-Item $TmpZip

$UserPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($UserPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("PATH", "$UserPath;$InstallDir", "User")
    Write-Host "Aggiunto $InstallDir al PATH utente."
}

Write-Host ""
Write-Host "Installazione completata."
Write-Host "Apri un nuovo terminale e scrivi: zeri"
