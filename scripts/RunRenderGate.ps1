# RunRenderGate.ps1（SDD-0001 T4）：RenderGate 编排脚本。
# 每个后端抓一帧 PNG 与 golden 做 SSIM 回归，另做 Vulkan/DX12 跨后端对比。
# -BlessGolden 用本次抓帧刷新 golden 基线。报告落 Intermediate/test-reports/render-gate/<时间戳>/。
[CmdletBinding()]
param(
    [string]$Configuration = "Debug",
    [string[]]$Backends = @("vulkan", "dx12"),
    [int]$SmokeFrames = 20000,
    [double]$GoldenSsimThreshold = 0.995,
    [double]$CrossSsimThreshold = 0.99,
    [switch]$BlessGolden,
    [switch]$SkipCrossBackend
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $path = Resolve-Path (Join-Path $PSScriptRoot "..")
    if (-not (Test-Path -LiteralPath (Join-Path $path.Path "AshEngine.sln"))) {
        throw "Could not resolve AshEngine repository root from $PSScriptRoot"
    }
    return $path.Path
}

function Invoke-CapturedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [string]$LogPath
    )

    $previousLocation = Get-Location
    try {
        Set-Location -LiteralPath $WorkingDirectory
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        Set-Location -LiteralPath $previousLocation
    }

    $output | Out-File -LiteralPath $LogPath -Encoding UTF8
    return [PSCustomObject]@{
        exit_code = $exitCode
        output    = @($output | ForEach-Object { [string]$_ })
    }
}

function ConvertFrom-ImageDiffOutput {
    param([string[]]$Lines)

    $values = @{}
    foreach ($line in $Lines) {
        if ($line -match '^([a-z_]+)=(.*)$') {
            $values[$matches[1]] = $matches[2]
        }
    }
    return $values
}

$repoRoot = Get-RepoRoot
$binDir = Join-Path $repoRoot "product/bin64/$Configuration-windows-x86_64"
$sandboxExe = Join-Path $binDir "Sandbox.exe"
$imageDiffExe = Join-Path $binDir "AshImageDiff.exe"

foreach ($required in @($sandboxExe, $imageDiffExe)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "Missing required binary: $required. Build the $Configuration configuration first."
        exit 2
    }
}

$sceneName = "sandbox"
$goldenDir = Join-Path $repoRoot "tools/render/goldens/$sceneName"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/render-gate/$timestamp"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

Write-Host "RenderGate: configuration=$Configuration backends=$($Backends -join ',') frames=$SmokeFrames"
Write-Host "RenderGate: report directory $reportRoot"

$records = New-Object System.Collections.Generic.List[object]
$dumpPaths = @{}
$anyFailure = $false

foreach ($backend in $Backends) {
    $normalizedBackend = $backend.ToLowerInvariant()
    $dumpPath = Join-Path $reportRoot "$sceneName-$normalizedBackend.png"
    $dumpLog = Join-Path $reportRoot "dump-$normalizedBackend.log"
    Write-Host "RenderGate: capturing $normalizedBackend frame ($SmokeFrames frames)..."

    $dumpArgs = @("--rhi=$normalizedBackend", "--smoke-test=$SmokeFrames", "--dump-frame=$dumpPath")
    $dumpResult = Invoke-CapturedProcess -FilePath $sandboxExe -Arguments $dumpArgs -WorkingDirectory $repoRoot -LogPath $dumpLog

    $record = [ordered]@{
        backend      = $normalizedBackend
        dump_path    = $dumpPath
        dump_exit    = $dumpResult.exit_code
        status       = "FAIL"
        detail       = ""
        ssim         = $null
        max_abs_diff = $null
    }

    if ($dumpResult.exit_code -ne 0 -or -not (Test-Path -LiteralPath $dumpPath)) {
        $record.detail = "frame dump failed (exit=$($dumpResult.exit_code)); see $dumpLog"
        $anyFailure = $true
        $records.Add([PSCustomObject]$record)
        continue
    }
    $dumpPaths[$normalizedBackend] = $dumpPath

    $goldenPath = Join-Path $goldenDir "$normalizedBackend.png"
    if ($BlessGolden) {
        New-Item -ItemType Directory -Force -Path $goldenDir | Out-Null
        Copy-Item -LiteralPath $dumpPath -Destination $goldenPath -Force
        $record.status = "BLESSED"
        $record.detail = "golden updated: $goldenPath"
        $records.Add([PSCustomObject]$record)
        Write-Host "RenderGate: [$normalizedBackend] golden blessed."
        continue
    }

    if (-not (Test-Path -LiteralPath $goldenPath)) {
        $record.detail = "golden missing: $goldenPath. Run RunRenderGate.bat -BlessGolden to create baselines."
        $anyFailure = $true
        $records.Add([PSCustomObject]$record)
        Write-Host "RenderGate: [$normalizedBackend] FAIL - $($record.detail)"
        continue
    }

    $heatmapPath = Join-Path $reportRoot "$sceneName-$normalizedBackend-heatmap.png"
    $diffLog = Join-Path $reportRoot "diff-$normalizedBackend.log"
    $diffArgs = @($goldenPath, $dumpPath, "--ssim-threshold=$GoldenSsimThreshold", "--heatmap=$heatmapPath")
    $diffResult = Invoke-CapturedProcess -FilePath $imageDiffExe -Arguments $diffArgs -WorkingDirectory $repoRoot -LogPath $diffLog
    $diffValues = ConvertFrom-ImageDiffOutput -Lines $diffResult.output

    if ($diffValues.ContainsKey("ssim")) { $record.ssim = [double]$diffValues["ssim"] }
    if ($diffValues.ContainsKey("max_abs_diff")) { $record.max_abs_diff = [int]$diffValues["max_abs_diff"] }

    if ($diffResult.exit_code -eq 0) {
        $record.status = "PASS"
        $record.detail = "ssim=$($record.ssim) >= $GoldenSsimThreshold"
    }
    else {
        $record.detail = "golden regression failed (exit=$($diffResult.exit_code), ssim=$($record.ssim), threshold=$GoldenSsimThreshold); heatmap: $heatmapPath"
        $anyFailure = $true
    }
    $records.Add([PSCustomObject]$record)
    Write-Host "RenderGate: [$normalizedBackend] $($record.status) - $($record.detail)"
}

$crossRecord = $null
if (-not $SkipCrossBackend -and $dumpPaths.ContainsKey("vulkan") -and $dumpPaths.ContainsKey("dx12")) {
    Write-Host "RenderGate: cross-backend diff (vulkan vs dx12)..."
    $crossHeatmap = Join-Path $reportRoot "$sceneName-cross-heatmap.png"
    $crossLog = Join-Path $reportRoot "diff-cross.log"
    $crossArgs = @($dumpPaths["vulkan"], $dumpPaths["dx12"], "--ssim-threshold=$CrossSsimThreshold", "--heatmap=$crossHeatmap")
    $crossResult = Invoke-CapturedProcess -FilePath $imageDiffExe -Arguments $crossArgs -WorkingDirectory $repoRoot -LogPath $crossLog
    $crossValues = ConvertFrom-ImageDiffOutput -Lines $crossResult.output

    $crossRecord = [ordered]@{
        comparison   = "vulkan-vs-dx12"
        status       = if ($crossResult.exit_code -eq 0) { "PASS" } else { "FAIL" }
        ssim         = if ($crossValues.ContainsKey("ssim")) { [double]$crossValues["ssim"] } else { $null }
        max_abs_diff = if ($crossValues.ContainsKey("max_abs_diff")) { [int]$crossValues["max_abs_diff"] } else { $null }
        threshold    = $CrossSsimThreshold
        heatmap      = $crossHeatmap
    }
    if ($crossResult.exit_code -ne 0) {
        $anyFailure = $true
    }
    $crossRecord = [PSCustomObject]$crossRecord
    Write-Host "RenderGate: [cross] $($crossRecord.status) - ssim=$($crossRecord.ssim) threshold=$CrossSsimThreshold"
}

$summary = [PSCustomObject]@{
    timestamp              = $timestamp
    configuration          = $Configuration
    scene                  = $sceneName
    smoke_frames           = $SmokeFrames
    golden_ssim_threshold  = $GoldenSsimThreshold
    cross_ssim_threshold   = $CrossSsimThreshold
    golden_blessed         = [bool]$BlessGolden
    result                 = if ($anyFailure) { "FAIL" } else { "PASS" }
    backends               = $records
    cross_backend          = $crossRecord
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# RenderGate Report ($timestamp)"
$markdown += ""
$markdown += "- Configuration: $Configuration"
$markdown += "- Scene: $sceneName"
$markdown += "- Smoke frames: $SmokeFrames"
$markdown += "- Golden SSIM threshold: $GoldenSsimThreshold"
$markdown += "- Cross-backend SSIM threshold: $CrossSsimThreshold"
$markdown += "- Result: **$($summary.result)**"
$markdown += ""
$markdown += "| Backend | Status | SSIM | MaxAbsDiff | Detail |"
$markdown += "| --- | --- | --- | --- | --- |"
foreach ($record in $records) {
    $markdown += "| $($record.backend) | $($record.status) | $($record.ssim) | $($record.max_abs_diff) | $($record.detail) |"
}
if ($null -ne $crossRecord) {
    $markdown += "| cross ($($crossRecord.comparison)) | $($crossRecord.status) | $($crossRecord.ssim) | $($crossRecord.max_abs_diff) | threshold=$($crossRecord.threshold) |"
}
$markdown -join "`r`n" | Set-Content -LiteralPath (Join-Path $reportRoot "summary.md") -Encoding UTF8

Write-Host ""
Write-Host "RenderGate: result=$($summary.result) report=$reportRoot"
if ($anyFailure) {
    exit 1
}
exit 0
