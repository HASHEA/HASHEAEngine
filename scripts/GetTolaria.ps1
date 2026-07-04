# Downloads a pinned Tolaria release installer into tools/tolaria/ (gitignored).
# Tolaria is a standalone AGPL-3.0 markdown knowledge-base app used as a local
# frontend for this repo's docs/ vault. It is intentionally NOT vendored into
# the repo: we only fetch the official binary on demand and pin version + hash.
# Update = bump $Version/$Sha256 below from https://github.com/refactoringhq/tolaria/releases

$ErrorActionPreference = "Stop"

$Version   = "2026.7.1"
$Tag       = "v2026-07-01"
$AssetName = "Tolaria_${Version}_x64-setup.exe"
$Url       = "https://github.com/refactoringhq/tolaria/releases/download/$Tag/$AssetName"
$Sha256    = "1791af3f18e8820c7263e719e0a59d93c5e07d9a7164cbb1a0eaaceab06a9314"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutDir   = Join-Path $RepoRoot "tools/tolaria"
$OutFile  = Join-Path $OutDir $AssetName

function Test-Hash([string]$Path) {
    (Get-FileHash -Path $Path -Algorithm SHA256).Hash -ieq $Sha256
}

if ((Test-Path $OutFile) -and (Test-Hash $OutFile)) {
    Write-Host "Already downloaded and verified: $OutFile"
} else {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Write-Host "Downloading Tolaria $Version ..."
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing

    if (-not (Test-Hash $OutFile)) {
        Remove-Item $OutFile -Force
        throw "SHA256 mismatch for $AssetName - download discarded. Expected $Sha256"
    }
    Write-Host "Verified SHA256 OK: $OutFile"
}

Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Run the installer: $OutFile"
Write-Host "  2. In Tolaria, open this repo's 'docs/' directory as the vault."
Write-Host "  3. If Tolaria creates a config directory inside the vault, add it to .gitignore."
