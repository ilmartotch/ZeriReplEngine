param()

$ErrorActionPreference = "Stop"

Write-Host "install.ps1 is deprecated."
Write-Host "Install Zeri with Scoop:"
Write-Host "  scoop bucket add zeri https://github.com/ilmartotch/scoop-zeri"
Write-Host "  scoop install zeri"
Write-Host ""
Write-Host "After installation, open a new terminal and run: zeri --version"
