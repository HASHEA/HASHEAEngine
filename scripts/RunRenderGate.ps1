# RunRenderGate.ps1（SDD-2026-07-07-render-gate T4）：RenderGate 编排脚本。
# 每个后端抓一帧 PNG 与 golden 做 SSIM 回归，另做 Vulkan/DX12 跨后端对比。
# -BlessGolden 用本次抓帧刷新 golden 基线。报告落 Intermediate/test-reports/render-gate/<时间戳>/。
# 同一 Sandbox 进程等待 readiness、抓取通过 epoch 复核的画面并以成功退出码证明 runtime smoke；
# -TimeoutSeconds 只是 wall-clock 硬失败上限，超时不会产出可 bless 的图片。
[CmdletBinding()]
param(
	[string]$Configuration = "Debug",
	[string[]]$Backends = @("vulkan", "dx12"),
	[string[]]$Scenes = @("sandbox", "particles"),
	[double]$TimeoutSeconds = 120.0,
	[double]$ProcessTimeoutGraceSeconds = 15.0,
    [double]$GoldenSsimThreshold = 0.995,
    [double]$CrossSsimThreshold = 0.99,
    [switch]$BlessGolden,
    [switch]$SkipCrossBackend
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "RenderGateGoldenPublisher.ps1")

function Get-RepoRoot {
    $path = Resolve-Path (Join-Path $PSScriptRoot "..")
    if (-not (Test-Path -LiteralPath (Join-Path $path.Path "AshEngine.sln"))) {
        throw "Could not resolve AshEngine repository root from $PSScriptRoot"
    }
    return $path.Path
}

function Quote-Argument {
    param([string]$Value)

    if ($null -eq $Value -or $Value.Length -eq 0) {
        return '""'
    }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Join-Arguments {
    param([string[]]$Arguments)

    return (($Arguments | ForEach-Object { Quote-Argument $_ }) -join " ")
}

function ConvertTo-WaitMilliseconds {
    param([double]$Seconds)

    $maximumSeconds = [int]::MaxValue / 1000.0
    if ([double]::IsNaN($Seconds) -or [double]::IsInfinity($Seconds) -or
        $Seconds -le 0.0 -or $Seconds -gt $maximumSeconds) {
        throw "Process timeout must be finite, greater than zero, and no more than $maximumSeconds seconds."
    }
    return [Math]::Max(1, [int][Math]::Ceiling($Seconds * 1000.0))
}

function Invoke-TaskkillBounded {
    param(
        [int]$ProcessId,
        [int]$WaitMilliseconds
    )

    $taskkill = New-Object System.Diagnostics.Process
    $taskkillPsi = New-Object System.Diagnostics.ProcessStartInfo
    $taskkillPsi.FileName = "taskkill.exe"
    $taskkillPsi.Arguments = "/PID $ProcessId /T /F"
    $taskkillPsi.UseShellExecute = $false
    $taskkillPsi.CreateNoWindow = $true
    $taskkillPsi.RedirectStandardOutput = $true
    $taskkillPsi.RedirectStandardError = $true
    $taskkill.StartInfo = $taskkillPsi
    $started = $false
    try {
        if (-not $taskkill.Start()) { return $false }
        $started = $true
        $stdoutTask = $taskkill.StandardOutput.ReadToEndAsync()
        $stderrTask = $taskkill.StandardError.ReadToEndAsync()
        if (-not $taskkill.WaitForExit($WaitMilliseconds)) {
            try { $taskkill.Kill() } catch { }
            $taskkill.WaitForExit(1000) | Out-Null
            return $false
        }
        $stdoutTask.Wait(1000) | Out-Null
        $stderrTask.Wait(1000) | Out-Null
        return $taskkill.ExitCode -eq 0
    }
    catch {
        return $false
    }
    finally {
        if ($started) {
            try {
                if (-not $taskkill.HasExited) {
                    $taskkill.Kill()
                    $taskkill.WaitForExit(1000) | Out-Null
                }
            }
            catch { }
        }
        $taskkill.Dispose()
    }
}

function Stop-ProcessTreeBounded {
    param(
        [Diagnostics.Process]$Process,
        [int]$WaitMilliseconds = 5000
    )

    try {
        if ($Process.HasExited) { return $true }
    }
    catch {
        return $true
    }

    $taskkillWaitMilliseconds = [Math]::Min($WaitMilliseconds, 2000)
    $treeKillRequested = Invoke-TaskkillBounded -ProcessId $Process.Id -WaitMilliseconds $taskkillWaitMilliseconds
    if (-not $treeKillRequested) {
        try { $Process.Kill() } catch { }
    }
    if ($Process.WaitForExit($WaitMilliseconds)) { return $true }
    try { $Process.Kill() } catch { }
    return $Process.WaitForExit($WaitMilliseconds)
}

function Invoke-CapturedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [string]$LogPath,
        [double]$TimeoutSeconds
    )

    # Convert before process creation so invalid/overflowing limits cannot leave
    # a child running when the conversion throws.
    $timeoutMilliseconds = ConvertTo-WaitMilliseconds $TimeoutSeconds
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = Join-Arguments $Arguments
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $exitCode = -1
    $timedOut = $false
    $terminationFailed = $false
    $outputDrainFailed = $false
    $stdoutTask = $null
    $stderrTask = $null
    $standardOutput = ""
    $standardError = ""
    $processStarted = $false
    try {
        if (-not $process.Start()) {
            throw "Failed to start process: $FilePath"
        }
        $processStarted = $true
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($timeoutMilliseconds)) {
            $timedOut = $true
            $terminationFailed = -not (Stop-ProcessTreeBounded -Process $process)
        }
        else {
            $process.Refresh()
            $exitCode = $process.ExitCode
        }

        if (-not $terminationFailed) {
            foreach ($outputTask in @($stdoutTask, $stderrTask)) {
                try {
                    if (-not $outputTask.Wait(5000)) {
                        $outputDrainFailed = $true
                    }
                }
                catch {
                    $outputDrainFailed = $true
                }
            }
        }
        if ($null -ne $stdoutTask -and $stdoutTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion) {
            $standardOutput = $stdoutTask.Result
        }
        if ($null -ne $stderrTask -and $stderrTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion) {
            $standardError = $stderrTask.Result
        }
        if ($outputDrainFailed -and -not $timedOut) {
            $exitCode = -1
        }
    }
    finally {
        if ($processStarted) {
            try {
                if (-not $process.HasExited) {
                    Stop-ProcessTreeBounded -Process $process -WaitMilliseconds 1000 | Out-Null
                }
            }
            catch { }
        }
        $process.Dispose()
    }

    $outputParts = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrEmpty($standardOutput)) { $outputParts.Add($standardOutput.TrimEnd([char[]]"`r`n")) }
    if (-not [string]::IsNullOrEmpty($standardError)) { $outputParts.Add($standardError.TrimEnd([char[]]"`r`n")) }
    $combinedOutput = $outputParts -join [Environment]::NewLine
    Set-Content -LiteralPath $LogPath -Value $combinedOutput -NoNewline -Encoding UTF8
    $output = if ([string]::IsNullOrEmpty($combinedOutput)) { @() } else { @($combinedOutput -split '\r?\n') }
    return [PSCustomObject]@{
        exit_code = $exitCode
        output    = @($output | ForEach-Object { [string]$_ })
        timed_out = $timedOut
        termination_failed = $terminationFailed
        output_drain_failed = $outputDrainFailed
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
if ([double]::IsNaN($TimeoutSeconds) -or [double]::IsInfinity($TimeoutSeconds) -or $TimeoutSeconds -le 0.0) {
    throw "TimeoutSeconds must be finite and greater than zero."
}
if ([double]::IsNaN($ProcessTimeoutGraceSeconds) -or [double]::IsInfinity($ProcessTimeoutGraceSeconds) -or $ProcessTimeoutGraceSeconds -lt 0.0) {
    throw "ProcessTimeoutGraceSeconds must be finite and non-negative."
}
$captureProcessTimeoutSeconds = $TimeoutSeconds + $ProcessTimeoutGraceSeconds
ConvertTo-WaitMilliseconds $captureProcessTimeoutSeconds | Out-Null
$binDir = Join-Path $repoRoot "product/bin64/$Configuration-windows-x86_64"
$sandboxExe = Join-Path $binDir "Sandbox.exe"
$imageDiffExe = Join-Path $binDir "AshImageDiff.exe"

foreach ($required in @($sandboxExe, $imageDiffExe)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "Missing required binary: $required. Build the $Configuration configuration first."
        exit 2
    }
}

function Expand-ListArgument {
    param([string[]]$Values)

    return @(
        $Values |
            ForEach-Object { $_ -split ',' } |
            ForEach-Object { $_.Trim() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
}

$sceneDefinitions = @{
    sandbox = [PSCustomObject]@{
        scene_path = $null
    }
    particles = [PSCustomObject]@{
        scene_path = "product/assets/scenes/Particles.scene.json"
    }
}

$backendNames = @(Expand-ListArgument -Values $Backends | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique)
$sceneNames = @(Expand-ListArgument -Values $Scenes | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique)
if ($backendNames.Count -eq 0) {
    throw "RenderGate requires at least one backend."
}
if ($sceneNames.Count -eq 0) {
    throw "RenderGate requires at least one scene."
}
foreach ($sceneName in $sceneNames) {
    if (-not $sceneDefinitions.ContainsKey($sceneName)) {
        throw "Unknown RenderGate scene '$sceneName'. Supported scenes: $($sceneDefinitions.Keys -join ', ')."
    }
}

$runNonce = [Guid]::NewGuid().ToString("N").Substring(0, 8)
$timestamp = "$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')-$PID-$runNonce"
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/render-gate/$timestamp"
$goldenPublishLock = Join-Path $repoRoot "Intermediate/locks/render-gate-golden.lock"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

Write-Host "RenderGate: configuration=$Configuration scenes=$($sceneNames -join ',') backends=$($backendNames -join ',') readiness_timeout=${TimeoutSeconds}s"
Write-Host "RenderGate: report directory $reportRoot"

$records = New-Object System.Collections.Generic.List[object]
$crossRecords = New-Object System.Collections.Generic.List[object]
$blessCandidates = New-Object System.Collections.Generic.List[object]
$anyFailure = $false
$goldenBlessCompleted = $false
$goldenReadLock = $null
if (-not $BlessGolden) {
    try {
        $goldenReadLock = Open-RenderGateGoldenReadLock -LockPath $goldenPublishLock
        $unresolvedGoldenTransactions = @(
            Get-ChildItem -LiteralPath (Join-Path $repoRoot "tools/render/goldens") -Recurse -File -ErrorAction Stop |
                Where-Object { $_.Name -match '\.rendergate\.' }
        )
        if ($unresolvedGoldenTransactions.Count -gt 0) {
            throw "unresolved golden transaction artifact(s): $($unresolvedGoldenTransactions.FullName -join ', ')"
        }
    }
    catch {
        if ($null -ne $goldenReadLock) {
            $goldenReadLock.Dispose()
            $goldenReadLock = $null
        }
        Write-Host "RenderGate: cannot acquire a stable golden snapshot: $($_.Exception.Message)"
        exit 3
    }
}

foreach ($sceneName in $sceneNames) {
    $sceneDefinition = $sceneDefinitions[$sceneName]
    $goldenDir = Join-Path $repoRoot "tools/render/goldens/$sceneName"
    $dumpPaths = @{}

    foreach ($normalizedBackend in $backendNames) {
        $dumpPath = Join-Path $reportRoot "$sceneName-$normalizedBackend.png"
        $dumpLog = Join-Path $reportRoot "dump-$sceneName-$normalizedBackend.log"
        Write-Host "RenderGate: [$sceneName] running readiness smoke + $normalizedBackend capture (${TimeoutSeconds}s timeout)..."

        $dumpArgs = @("--rhi=$normalizedBackend", "--smoke-test-seconds=$TimeoutSeconds", "--dump-frame=$dumpPath")
        if (-not [string]::IsNullOrWhiteSpace($sceneDefinition.scene_path)) {
            $dumpArgs += "--scene=$($sceneDefinition.scene_path)"
        }
        $dumpResult = Invoke-CapturedProcess `
            -FilePath $sandboxExe `
            -Arguments $dumpArgs `
            -WorkingDirectory $repoRoot `
            -LogPath $dumpLog `
            -TimeoutSeconds $captureProcessTimeoutSeconds

        $record = [ordered]@{
            scene        = $sceneName
            backend      = $normalizedBackend
            dump_path    = $dumpPath
            dump_exit    = $dumpResult.exit_code
            runtime_smoke = $dumpResult.exit_code -eq 0
            script_timed_out = $dumpResult.timed_out
            termination_failed = $dumpResult.termination_failed
            output_drain_failed = $dumpResult.output_drain_failed
            status       = "FAIL"
            detail       = ""
            ssim         = $null
            max_abs_diff = $null
        }

        if ($dumpResult.exit_code -ne 0 -or -not (Test-Path -LiteralPath $dumpPath)) {
            $record.detail = if ($dumpResult.timed_out) {
                if ($dumpResult.termination_failed) {
                    "process exceeded script timeout ${captureProcessTimeoutSeconds}s and resisted bounded termination; see $dumpLog"
                }
                else {
                    "process exceeded script timeout ${captureProcessTimeoutSeconds}s; see $dumpLog"
                }
            }
            else {
                "readiness smoke/capture failed (exit=$($dumpResult.exit_code)); see $dumpLog"
            }
            $anyFailure = $true
            $records.Add([PSCustomObject]$record)
            continue
        }
        $dumpPaths[$normalizedBackend] = $dumpPath

        $goldenPath = Join-Path $goldenDir "$normalizedBackend.png"
        if ($BlessGolden) {
            $record.status = "READY_TO_BLESS"
            $record.detail = "capture passed readiness; baseline update deferred until the full selected matrix passes"
            $recordObject = [PSCustomObject]$record
            $records.Add($recordObject)
            $blessCandidates.Add([PSCustomObject]@{
                source = $dumpPath
                destination = $goldenPath
                record = $recordObject
            })
            continue
        }

        if (-not (Test-Path -LiteralPath $goldenPath)) {
            $record.detail = "golden missing: $goldenPath. Run RunRenderGate.bat -Scenes $sceneName -BlessGolden after visual approval."
            $anyFailure = $true
            $records.Add([PSCustomObject]$record)
            Write-Host "RenderGate: [$sceneName/$normalizedBackend] FAIL - $($record.detail)"
            continue
        }

        $heatmapPath = Join-Path $reportRoot "$sceneName-$normalizedBackend-heatmap.png"
        $diffLog = Join-Path $reportRoot "diff-$sceneName-$normalizedBackend.log"
        $diffArgs = @($goldenPath, $dumpPath, "--ssim-threshold=$GoldenSsimThreshold", "--heatmap=$heatmapPath")
        $diffResult = Invoke-CapturedProcess -FilePath $imageDiffExe -Arguments $diffArgs -WorkingDirectory $repoRoot -LogPath $diffLog -TimeoutSeconds 60.0
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
        Write-Host "RenderGate: [$sceneName/$normalizedBackend] $($record.status) - $($record.detail)"
    }

    if (-not $SkipCrossBackend -and $dumpPaths.ContainsKey("vulkan") -and $dumpPaths.ContainsKey("dx12")) {
        Write-Host "RenderGate: [$sceneName] cross-backend diff (vulkan vs dx12)..."
        $crossHeatmap = Join-Path $reportRoot "$sceneName-cross-heatmap.png"
        $crossLog = Join-Path $reportRoot "diff-$sceneName-cross.log"
        $crossArgs = @($dumpPaths["vulkan"], $dumpPaths["dx12"], "--ssim-threshold=$CrossSsimThreshold", "--heatmap=$crossHeatmap")
        $crossResult = Invoke-CapturedProcess -FilePath $imageDiffExe -Arguments $crossArgs -WorkingDirectory $repoRoot -LogPath $crossLog -TimeoutSeconds 60.0
        $crossValues = ConvertFrom-ImageDiffOutput -Lines $crossResult.output

        $crossRecord = [PSCustomObject][ordered]@{
            scene        = $sceneName
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
        $crossRecords.Add($crossRecord)
        Write-Host "RenderGate: [$sceneName/cross] $($crossRecord.status) - ssim=$($crossRecord.ssim) threshold=$CrossSsimThreshold"
    }
}

if ($BlessGolden) {
    if ($anyFailure) {
        foreach ($candidate in $blessCandidates) {
            $candidate.record.status = "NOT_BLESSED"
            $candidate.record.detail = "baseline unchanged because another selected capture or cross-backend check failed"
        }
        Write-Host "RenderGate: selected matrix failed; no golden baselines were changed."
    }
    else {
        try {
            $publishResult = Publish-RenderGateGoldenMatrix `
                -Candidates $blessCandidates `
                -TransactionId $timestamp `
                -LockPath $goldenPublishLock
            $goldenBlessCompleted = [bool]$publishResult.committed
            if ($publishResult.cleanup_complete) {
                Write-Host "RenderGate: all selected golden baselines were blessed after the full matrix passed."
            }
            else {
                $anyFailure = $true
                Write-Host "RenderGate: goldens were committed, but transaction cleanup failed: $($publishResult.cleanup_errors -join '; ')"
            }
        }
        catch {
            $anyFailure = $true
            Write-Host "RenderGate: golden matrix publication failed: $($_.Exception.Message)"
        }
    }
}

$summary = [PSCustomObject]@{
    timestamp              = $timestamp
    configuration          = $Configuration
    scenes                 = $sceneNames
    timeout_seconds        = $TimeoutSeconds
    golden_ssim_threshold  = $GoldenSsimThreshold
    cross_ssim_threshold   = $CrossSsimThreshold
    golden_bless_requested = [bool]$BlessGolden
    golden_blessed         = $goldenBlessCompleted
    result                 = if ($anyFailure) { "FAIL" } else { "PASS" }
    backends               = $records
    cross_backend          = $crossRecords
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# RenderGate Report ($timestamp)"
$markdown += ""
$markdown += "- Configuration: $Configuration"
$markdown += "- Scenes: $($sceneNames -join ', ')"
$markdown += "- Readiness timeout: $TimeoutSeconds seconds"
$markdown += "- Golden SSIM threshold: $GoldenSsimThreshold"
$markdown += "- Cross-backend SSIM threshold: $CrossSsimThreshold"
$markdown += "- Result: **$($summary.result)**"
$markdown += ""
$markdown += "| Scene | Backend | Status | SSIM | MaxAbsDiff | Detail |"
$markdown += "| --- | --- | --- | --- | --- | --- |"
foreach ($record in $records) {
    $markdown += "| $($record.scene) | $($record.backend) | $($record.status) | $($record.ssim) | $($record.max_abs_diff) | $($record.detail) |"
}
foreach ($crossRecord in $crossRecords) {
    $markdown += "| $($crossRecord.scene) | cross ($($crossRecord.comparison)) | $($crossRecord.status) | $($crossRecord.ssim) | $($crossRecord.max_abs_diff) | threshold=$($crossRecord.threshold) |"
}
$markdown -join "`r`n" | Set-Content -LiteralPath (Join-Path $reportRoot "summary.md") -Encoding UTF8

Write-Host ""
Write-Host "RenderGate: result=$($summary.result) report=$reportRoot"
if ($null -ne $goldenReadLock) {
    $goldenReadLock.Dispose()
    $goldenReadLock = $null
}
if ($anyFailure) {
    exit 1
}
exit 0
