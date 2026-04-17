param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [Parameter(Mandatory = $true)]
    [string]$Destination,
    [switch]$Optional
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Source)) {
    if ($Optional) {
        exit 0
    }

    throw "Runtime sync source is missing: $Source"
}

$destinationDirectory = Split-Path -Path $Destination -Parent
if (-not [string]::IsNullOrWhiteSpace($destinationDirectory) -and -not (Test-Path -LiteralPath $destinationDirectory)) {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
}

Copy-Item -LiteralPath $Source -Destination $Destination -Force
