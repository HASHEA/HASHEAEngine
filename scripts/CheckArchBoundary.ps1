[CmdletBinding()]
param(
    [string]$RulesPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "premake5.lua"))) {
    throw "Could not resolve AshEngine repository root from $PSScriptRoot"
}

if ([string]::IsNullOrWhiteSpace($RulesPath)) {
    $RulesPath = Join-Path $repoRoot "tools\ai-dev\rules\arch-boundary-rules.json"
}
if (-not (Test-Path -LiteralPath $RulesPath)) {
    throw "Arch boundary rules file missing: $RulesPath"
}
$rules = Get-Content -LiteralPath $RulesPath -Raw | ConvertFrom-Json

$forbiddenMap = @{}
foreach ($edge in $rules.forbidden) {
    $forbiddenMap[[string]$edge.from] = @($edge.to | ForEach-Object { [string]$_ })
}

$includeRegex = '^\s*#\s*include\s+"((Base|Graphics|Function)/[^"]+)"'

function Test-AllowEntry {
    param([object]$Entry, [string]$RelativeFile, [string]$IncludePath)
    if ($RelativeFile -ne [string]$Entry.file) { return $false }
    return ($IncludePath -like [string]$Entry.include)
}

$violations = @()
$legacyHits = @()
$exceptionHits = @()
$matchedEntries = New-Object System.Collections.Generic.HashSet[string]

foreach ($layer in $rules.layers) {
    $layerName = [string]$layer.name
    if (-not $forbiddenMap.ContainsKey($layerName)) { continue }
    $forbiddenTargets = $forbiddenMap[$layerName]

    $layerRoot = Join-Path $repoRoot (([string]$layer.path_prefix) -replace "/", "\")
    if (-not (Test-Path -LiteralPath $layerRoot)) { continue }

    $files = Get-ChildItem -LiteralPath $layerRoot -Recurse -File -Include *.h, *.hpp, *.cpp, *.inl
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($repoRoot.Length).TrimStart('\') -replace "\\", "/"
        $lineNumber = 0
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            $match = [regex]::Match($line, $includeRegex)
            if (-not $match.Success) { continue }
            $includePath = $match.Groups[1].Value

            $targetLayer = ""
            foreach ($prefix in $rules.include_prefixes.PSObject.Properties) {
                if ($includePath.StartsWith($prefix.Name)) {
                    $targetLayer = [string]$prefix.Value
                    break
                }
            }
            if (-not $targetLayer -or $targetLayer -eq $layerName) { continue }
            if ($forbiddenTargets -notcontains $targetLayer) { continue }

            $record = "{0}:{1}: {2} -> {3} (`"{4}`")" -f $relative, $lineNumber, $layerName, $targetLayer, $includePath

            $handled = $false
            foreach ($entry in $rules.exceptions) {
                if (Test-AllowEntry -Entry $entry -RelativeFile $relative -IncludePath $includePath) {
                    $exceptionHits += $record
                    [void]$matchedEntries.Add("exception|$($entry.file)|$($entry.include)")
                    $handled = $true
                    break
                }
            }
            if ($handled) { continue }
            foreach ($entry in $rules.legacy_violations) {
                if (Test-AllowEntry -Entry $entry -RelativeFile $relative -IncludePath $includePath) {
                    $legacyHits += $record
                    [void]$matchedEntries.Add("legacy|$($entry.file)|$($entry.include)")
                    $handled = $true
                    break
                }
            }
            if ($handled) { continue }

            $violations += $record
        }
    }
}

# 名单条目失配（文件已修复/删除）视为 FAIL，保证名单只减不增
$staleEntries = @()
foreach ($entry in $rules.exceptions) {
    if (-not $matchedEntries.Contains("exception|$($entry.file)|$($entry.include)")) {
        $staleEntries += "exception: $($entry.file) -> $($entry.include)"
    }
}
foreach ($entry in $rules.legacy_violations) {
    if (-not $matchedEntries.Contains("legacy|$($entry.file)|$($entry.include)")) {
        $staleEntries += "legacy_violation: $($entry.file) -> $($entry.include)"
    }
}

Write-Host "[ArchGate] exceptions matched : $($exceptionHits.Count)"
if ($legacyHits.Count -gt 0) {
    Write-Host "[ArchGate] WARN legacy violations (grandfathered, fix pending, must not grow):"
    $legacyHits | Sort-Object -Unique | ForEach-Object { Write-Host "  WARN  $_" }
}

$failed = $false
if ($violations.Count -gt 0) {
    $failed = $true
    Write-Host "[ArchGate] FAIL new boundary violations:"
    $violations | Sort-Object -Unique | ForEach-Object { Write-Host "  FAIL  $_" }
}
if ($staleEntries.Count -gt 0) {
    $failed = $true
    Write-Host "[ArchGate] FAIL stale allowlist entries (no longer match anything, remove them from arch-boundary-rules.json):"
    $staleEntries | ForEach-Object { Write-Host "  FAIL  $_" }
}

if ($failed) {
    Write-Host "[ArchGate] RESULT: FAIL"
    exit 1
}
Write-Host "[ArchGate] RESULT: PASS ($($legacyHits.Count) legacy warns)"
exit 0
