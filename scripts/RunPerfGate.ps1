[CmdletBinding()]
param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [string]$BaselinePath = "",
    [switch]$SkipBuild,
    [switch]$DryRun,
    [switch]$BlessBaseline,
    [ValidateSet("Profile", "Off")]
    [string]$TelemetryMode = "Profile",
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "PerfGateProfileConfig.ps1")

function Get-RepoRoot {
    $path = Resolve-Path (Join-Path $PSScriptRoot "..")
    if (-not (Test-Path -LiteralPath (Join-Path $path.Path "AshEngine.sln"))) {
        throw "Could not resolve AshEngine repository root from $PSScriptRoot"
    }
    return $path.Path
}

function ConvertTo-WindowsCrtArgument {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrEmpty($Value)) {
        return '""'
    }
    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashCount
            continue
        }

        if ($character -eq '"') {
            for ($index = 0; $index -lt (2 * $backslashCount + 1); ++$index) {
                [void]$builder.Append('\')
            }
            [void]$builder.Append('"')
            $backslashCount = 0
            continue
        }

        for ($index = 0; $index -lt $backslashCount; ++$index) {
            [void]$builder.Append('\')
        }
        $backslashCount = 0
        [void]$builder.Append($character)
    }

    for ($index = 0; $index -lt (2 * $backslashCount); ++$index) {
        [void]$builder.Append('\')
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Join-WindowsCrtArguments {
    param([AllowEmptyCollection()][string[]]$Arguments)

    return (@($Arguments | ForEach-Object { ConvertTo-WindowsCrtArgument $_ }) -join " ")
}

function Quote-Argument {
    param([AllowNull()][string]$Value)

    return ConvertTo-WindowsCrtArgument $Value
}

function Join-Arguments {
    param([AllowEmptyCollection()][string[]]$Arguments)

    return Join-WindowsCrtArguments $Arguments
}

function Set-SanitizedPathEnvironment {
    param([System.Diagnostics.ProcessStartInfo]$ProcessStartInfo)

    $pathValue = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    $ProcessStartInfo.EnvironmentVariables.Clear()
    foreach ($entry in [System.Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ([string]::Equals([string]$entry.Key, "Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $ProcessStartInfo.EnvironmentVariables[[string]$entry.Key] = [string]$entry.Value
    }
    $ProcessStartInfo.EnvironmentVariables["Path"] = $pathValue
}

function New-GateProcessStartInfo {
    param(
        [string]$Executable,
        [AllowEmptyCollection()][string[]]$Arguments,
        [string]$WorkingDirectory
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.Arguments = Join-WindowsCrtArguments $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    Set-SanitizedPathEnvironment $startInfo
    return $startInfo
}

function Complete-GateProcessOutput {
    param(
        [System.Threading.Tasks.Task]$StandardOutputTask,
        [System.Threading.Tasks.Task]$StandardErrorTask,
        [int]$TimeoutMilliseconds
    )

    $tasks = [System.Threading.Tasks.Task[]]@($StandardOutputTask, $StandardErrorTask)
    $completed = $false
    $failure = $null
    try {
        $completed = [System.Threading.Tasks.Task]::WaitAll($tasks, [Math]::Max(0, $TimeoutMilliseconds))
    }
    catch {
        $failure = $_.Exception.Message
    }

    $ranToCompletion = $completed -and
        $StandardOutputTask.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion -and
        $StandardErrorTask.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion
    return [PSCustomObject]@{
        completed = $ranToCompletion
        standard_output = if ($ranToCompletion) { [string]$StandardOutputTask.Result } else { "" }
        standard_error = if ($ranToCompletion) { [string]$StandardErrorTask.Result } else { "" }
        failure = $failure
    }
}

function Invoke-BatchCommand {
    param(
        [string]$RepoRoot,
        [string]$CommandLine,
        [string]$LogPath
    )

    $logDirectory = Split-Path -Parent $LogPath
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = "/d /c `"$CommandLine > `"$LogPath`" 2>&1`""
    $psi.WorkingDirectory = $RepoRoot
    $psi.UseShellExecute = $false
    Set-SanitizedPathEnvironment $psi

    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $CommandLine. Log: $LogPath"
    }
}

function New-RunRecord {
    param(
        [string]$Target,
        [string]$Backend,
        [string]$Executable,
        [string]$TelemetryPath,
        [string]$ProcessLogPath,
        [string]$ProcessErrorLogPath
    )

    [PSCustomObject]@{
        target = $Target
        backend = $Backend
        configuration = ""
        executable = $Executable
        arguments = @()
        command_line = ""
        telemetry = $TelemetryPath
        process_log = $ProcessLogPath
        process_error_log = $ProcessErrorLogPath
        engine_logs = @()
        exit_code = $null
        root_exited = $false
        tree_termination_confirmed = $false
        job_kill_on_close = $false
        job_assigned = $false
        job_active_processes_after_cleanup = $null
        job_cleanup_confirmed = $false
        status = "NOT_RUN"
        failures = @()
        warnings = @()
        frames_sampled = 0
        cpu_frame_time_avg_ms = 0.0
        cpu_frame_time_p95_ms = 0.0
        cpu_frame_time_p99_ms = 0.0
        process_private_bytes_peak_mb = 0.0
        engine_heap_peak_mb = 0.0
        draw_calls_avg = 0.0
        gpu_frame_avg_ms = $null
        gpu_frame_p95_ms = $null
        gpu_coverage = $null
        gpu_submitted = 0
        gpu_resolved = 0
        gpu_valid = 0
        gpu_invalid = 0
        gpu_metric_summaries = [PSCustomObject]@{}
        required_gpu_metrics = @()
        gpu_adapter = "n/a"
        gpu_driver = "n/a"
        actual_width = $null
        actual_height = $null
        extent_stable = $null
        validation = $null
        vsync = $null
        fixed_camera = $null
        frame_cap = "n/a"
        telemetry_mode = "Profile"
        gpu_baseline_comparable = $true
        baseline_status = "NOT_COMPARED"
        cpu_frame_time_avg_delta = "n/a"
        cpu_frame_time_p95_delta = "n/a"
        cpu_frame_time_p99_delta = "n/a"
        process_private_bytes_delta = "n/a"
        engine_heap_delta = "n/a"
        draw_calls_delta = "n/a"
        baseline_deltas = @()
    }
}

function ConvertTo-InvariantNumber {
    param([double]$Value)

    return $Value.ToString("0.################", [System.Globalization.CultureInfo]::InvariantCulture)
}

function ConvertTo-OnOff {
    param([bool]$Value)

    if ($Value) {
        return "on"
    }
    return "off"
}

function Get-PerfGateTargetName {
    param([object]$TargetConfig)

    $name = Get-PerfGateObjectProperty $TargetConfig "name"
    if ($null -eq $name) {
        $name = Get-PerfGateObjectProperty $TargetConfig "target"
    }
    if ($null -eq $name -or [string]::IsNullOrWhiteSpace([string]$name)) {
        throw "Perf gate target has no name."
    }
    return [string]$name
}

function Assert-PerfGateOptions {
    param(
        [bool]$BlessBaseline,
        [bool]$DryRun,
        [ValidateSet("Profile", "Off")]
        [string]$TelemetryMode
    )

    if ($BlessBaseline -and $DryRun) {
        throw "-BlessBaseline cannot be used with -DryRun."
    }
    if ($BlessBaseline -and $TelemetryMode -eq "Off") {
        throw "-TelemetryMode Off cannot be used with -BlessBaseline."
    }
}

function Assert-PerfGateProfileMatrix {
    param([object]$ProfileConfig)

    $targets = @(Get-PerfGateObjectProperty $ProfileConfig "targets")
    if ($targets.Count -eq 0) {
        throw "Perf gate profile must declare at least one target."
    }

    $seenTargets = @{}
    foreach ($targetConfig in $targets) {
        $targetName = Get-PerfGateTargetName $targetConfig
        $targetKey = $targetName.ToLowerInvariant()
        if ($targetKey -notin @("sandbox", "editor")) {
            throw "Unsupported perf gate target '$targetName'."
        }
        if ($seenTargets.ContainsKey($targetKey)) {
            throw "Duplicate perf gate target '$targetName'."
        }
        $seenTargets[$targetKey] = $true

        $backends = @(Get-PerfGateObjectProperty $targetConfig "backends")
        if ($backends.Count -eq 0) {
            throw "Perf gate target '$targetName' must declare at least one backend."
        }

        $seenBackends = @{}
        foreach ($backendValue in $backends) {
            $backend = [string]$backendValue
            if ([string]::IsNullOrWhiteSpace($backend)) {
                throw "Perf gate target '$targetName' contains a blank backend."
            }
            $backendKey = $backend.ToLowerInvariant()
            if ($backendKey -notin @("vulkan", "dx12")) {
                throw "Unsupported perf gate backend '$backend'."
            }
            if ($seenBackends.ContainsKey($backendKey)) {
                throw "Duplicate perf gate backend '$backend' for target '$targetName'."
            }
            $seenBackends[$backendKey] = $true
        }
    }
}

function Assert-PerfGateRunRecords {
    param([AllowEmptyCollection()][object[]]$Records)

    if (@($Records).Count -eq 0) {
        throw "Perf gate profile must generate at least one run."
    }
}

function Get-PerfGateBuildCommands {
    param(
        [object]$ProfileConfig,
        [string]$Configuration
    )

    $seenTargets = @{}
    foreach ($targetConfig in @($ProfileConfig.targets)) {
        $targetName = Get-PerfGateTargetName $targetConfig
        $targetKey = $targetName.ToLowerInvariant()
        if ($seenTargets.ContainsKey($targetKey)) {
            continue
        }
        $seenTargets[$targetKey] = $true

        $scriptName = switch ($targetKey) {
            "sandbox" { "build_sandbox.bat" }
            "editor" { "build_editor.bat" }
            default { throw "Unsupported perf gate target '$targetName'." }
        }
        [PSCustomObject]@{
            target = $targetName
            command_line = "$scriptName $Configuration x64"
            log_name = "build_$targetKey.log"
        }
    }
}

function New-PerfGateRunPlan {
    param(
        [string]$RepoRoot,
        [string]$ReportRoot,
        [string]$Profile,
        [object]$ProfileConfig,
        [string]$Configuration,
        [ValidateSet("Profile", "Off")]
        [string]$TelemetryMode
    )

    Assert-PerfGateProfileMatrix -ProfileConfig $ProfileConfig

    $warmupSeconds = [double]$ProfileConfig.warmup_seconds
    $sampleSeconds = [double]$ProfileConfig.sample_seconds
    $drainValue = Get-PerfGateObjectProperty $ProfileConfig "drain_seconds"
    $drainSeconds = if ($null -eq $drainValue) { 0.0 } else { [double]$drainValue }
    $runForSeconds = $warmupSeconds + $sampleSeconds + $drainSeconds + 10.0

    foreach ($targetConfig in @($ProfileConfig.targets)) {
        $targetName = Get-PerfGateTargetName $targetConfig
        $targetKey = $targetName.ToLowerInvariant()
        $exeName = switch ($targetKey) {
            "sandbox" { "Sandbox.exe" }
            "editor" { "Editor.exe" }
            default { throw "Unsupported perf gate target '$targetName'." }
        }

        foreach ($backendValue in @($targetConfig.backends)) {
            $backend = [string]$backendValue
            $backendLower = $backend.ToLowerInvariant()
            if ($backendLower -notin @("vulkan", "dx12")) {
                throw "Unsupported perf gate backend '$backend'."
            }

            $runDir = Join-Path $RepoRoot "product/bin64/$Configuration-windows-x86_64"
            $exePath = Join-Path $runDir $exeName
            $runName = "{0}-{1}" -f $targetName, $backend
            $telemetryPath = Join-Path $ReportRoot "$runName.json"
            $processLogPath = Join-Path $ReportRoot "$runName.stdout.log"
            $processErrorLogPath = Join-Path $ReportRoot "$runName.stderr.log"
            $record = New-RunRecord $targetName $backend $exePath $telemetryPath $processLogPath $processErrorLogPath
            $record.configuration = $Configuration
            $record.telemetry_mode = $TelemetryMode
            $record.gpu_baseline_comparable = ($TelemetryMode -ne "Off")

            $arguments = @(
                "--rhi=$backendLower",
                "--perf-gate",
                "--perf-gate-profile=$Profile",
                "--perf-gate-output=$telemetryPath",
                "--perf-gate-target=$targetName",
                "--perf-gate-warmup-seconds=$(ConvertTo-InvariantNumber $warmupSeconds)",
                "--perf-gate-sample-seconds=$(ConvertTo-InvariantNumber $sampleSeconds)",
                "--run-for-seconds=$(ConvertTo-InvariantNumber $runForSeconds)"
            )

            $scene = Get-PerfGateObjectProperty $ProfileConfig "scene"
            if ($null -ne $scene -and -not [string]::IsNullOrWhiteSpace([string]$scene)) {
                $arguments += "--scene=$scene"
            }
            $width = Get-PerfGateObjectProperty $ProfileConfig "window_width"
            $height = Get-PerfGateObjectProperty $ProfileConfig "window_height"
            if (($null -eq $width) -xor ($null -eq $height)) {
                throw "Perf gate profile must define window_width and window_height together."
            }
            if ($null -ne $width) {
                $arguments += "--window-width=$([int]$width)"
                $arguments += "--window-height=$([int]$height)"
            }
            if ($null -ne $drainValue) {
                $arguments += "--perf-gate-drain-seconds=$(ConvertTo-InvariantNumber $drainSeconds)"
            }

            $vsync = Get-PerfGateObjectProperty $ProfileConfig "vsync"
            if ($null -ne $vsync) {
                $arguments += "--perf-gate-vsync=$(ConvertTo-OnOff ([bool]$vsync))"
            }
            $validation = Get-PerfGateObjectProperty $ProfileConfig "validation"
            if ($null -ne $validation) {
                $arguments += "--perf-gate-validation=$(ConvertTo-OnOff ([bool]$validation))"
            }
            $gpuTiming = Get-PerfGateObjectProperty $ProfileConfig "gpu_timing"
            if ($TelemetryMode -eq "Off") {
                $arguments += "--perf-gate-gpu-timing=off"
            }
            elseif ($null -ne $gpuTiming) {
                $arguments += "--perf-gate-gpu-timing=on"
            }

            $record.arguments = $arguments
            $record.command_line = "{0} {1}" -f (Quote-Argument $exePath), (Join-Arguments $arguments)
            $record
        }
    }
}

function Add-Failure {
    param(
        [object]$Record,
        [string]$Message
    )

    $Record.failures = @($Record.failures) + $Message
    $Record.status = "FAIL"
}

function Add-Warning {
    param(
        [object]$Record,
        [string]$Message
    )

    $Record.warnings = @($Record.warnings) + $Message
    if ($Record.status -eq "PASS") {
        $Record.status = "WARN"
    }
}

function Get-ProfileProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function ConvertTo-PerfGateFiniteDouble {
    param([AllowNull()][object]$Value)

    $isJsonNumber = $Value -is [System.Int32] -or
        $Value -is [System.Int64] -or
        $Value -is [System.Double] -or
        $Value -is [System.Decimal]
    if (-not $isJsonNumber) {
        return $null
    }

    try {
        $converted = [double]$Value
    }
    catch {
        return $null
    }
    if ([double]::IsNaN($converted) -or [double]::IsInfinity($converted)) {
        return $null
    }
    return $converted
}

function ConvertTo-PerfGateNonNegativeInt64 {
    param([AllowNull()][object]$Value)

    if ($Value -isnot [System.Int32] -and $Value -isnot [System.Int64]) {
        return $null
    }
    $converted = [int64]$Value
    if ($converted -lt 0) {
        return $null
    }
    return $converted
}

function Ensure-ObjectProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -ne $property) {
        return $property.Value
    }

    $value = [PSCustomObject]@{}
    $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $value
    return $value
}

function Set-ObjectProperty {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Value
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value
    }
    else {
        $property.Value = $Value
    }
}

function Get-BaselineEntry {
    param(
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [string]$Target,
        [string]$Backend
    )

    $profileNode = Get-ProfileProperty $Baseline.baselines $Profile
    $configurationNode = Get-ProfileProperty $profileNode $Configuration
    $targetNode = Get-ProfileProperty $configurationNode $Target
    return Get-ProfileProperty $targetNode $Backend
}

function Format-DeltaPercent {
    param([object]$DeltaPercent)

    if ($null -eq $DeltaPercent) {
        return "new"
    }

    $deltaDouble = [double]$DeltaPercent
    if ($deltaDouble -gt 0.0) {
        return "+{0:N1}%" -f $deltaDouble
    }
    return "{0:N1}%" -f $deltaDouble
}

function New-BaselineDelta {
    param(
        [string]$Metric,
        [string]$Label,
        [double]$Current,
        [object]$BaselineValue,
        [double]$ThresholdPercent
    )

    if ($null -eq $BaselineValue) {
        return $null
    }

    $baselineDouble = [double]$BaselineValue
    $deltaPercent = $null
    $status = "PASS"
    if ([Math]::Abs($baselineDouble) -gt [double]::Epsilon) {
        $deltaPercent = (($Current - $baselineDouble) / $baselineDouble) * 100.0
        if ($deltaPercent -gt $ThresholdPercent) {
            $status = "WARN"
        }
    }
    elseif ([Math]::Abs($Current) -le [double]::Epsilon) {
        $deltaPercent = 0.0
    }
    elseif ($Current -gt 0.0) {
        $status = "WARN"
    }

    return [PSCustomObject]@{
        metric = $Metric
        label = $Label
        current = $Current
        baseline = $baselineDouble
        delta_percent = $deltaPercent
        delta_text = Format-DeltaPercent $deltaPercent
        threshold_percent = $ThresholdPercent
        status = $status
    }
}

function Add-BaselineDelta {
    param(
        [object]$Record,
        [object]$Delta
    )

    if ($null -eq $Delta) {
        return
    }

    $Record.baseline_deltas = @($Record.baseline_deltas) + $Delta
    switch ($Delta.metric) {
    "cpu_frame_time_avg_ms" { $Record.cpu_frame_time_avg_delta = $Delta.delta_text; break }
    "cpu_frame_time_p95_ms" { $Record.cpu_frame_time_p95_delta = $Delta.delta_text; break }
    "cpu_frame_time_p99_ms" { $Record.cpu_frame_time_p99_delta = $Delta.delta_text; break }
    "process_private_bytes_peak_mb" { $Record.process_private_bytes_delta = $Delta.delta_text; break }
    "engine_heap_peak_mb" { $Record.engine_heap_delta = $Delta.delta_text; break }
    "draw_calls_avg" { $Record.draw_calls_delta = $Delta.delta_text; break }
    }

    if ($Delta.status -eq "WARN") {
        Add-Warning $Record ("Baseline regression: {0} {1} exceeded {2:N1}% threshold" -f $Delta.label, $Delta.delta_text, [double]$Delta.threshold_percent)
    }
}

function Compare-RecordToBaseline {
    param(
        [object]$Record,
        [object]$Baseline,
        [object]$ProfileConfig,
        [string]$Profile,
        [string]$Configuration
    )

    $entry = Get-BaselineEntry -Baseline $Baseline -Profile $Profile -Configuration $Configuration -Target $Record.target -Backend $Record.backend
    if ($null -eq $entry) {
        $Record.baseline_status = "MISSING"
        return
    }

    $Record.baseline_status = "COMPARED"
    $warnThresholds = Get-ProfileProperty $ProfileConfig "warn_thresholds"
    if ($null -eq $warnThresholds) {
        return
    }
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_avg_ms" "CPU Avg" $Record.cpu_frame_time_avg_ms (Get-ProfileProperty $entry "cpu_frame_time_avg_ms") ([double]$warnThresholds.cpu_frame_time_avg_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_p95_ms" "CPU P95" $Record.cpu_frame_time_p95_ms (Get-ProfileProperty $entry "cpu_frame_time_p95_ms") ([double]$warnThresholds.cpu_frame_time_p95_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_p99_ms" "CPU P99" $Record.cpu_frame_time_p99_ms (Get-ProfileProperty $entry "cpu_frame_time_p99_ms") ([double]$warnThresholds.cpu_frame_time_p99_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "process_private_bytes_peak_mb" "Private MB" $Record.process_private_bytes_peak_mb (Get-ProfileProperty $entry "process_private_bytes_peak_mb") ([double]$warnThresholds.private_bytes_peak_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "engine_heap_peak_mb" "Heap MB" $Record.engine_heap_peak_mb (Get-ProfileProperty $entry "engine_heap_peak_mb") ([double]$warnThresholds.engine_heap_peak_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "draw_calls_avg" "Draw Calls" $Record.draw_calls_avg (Get-ProfileProperty $entry "draw_calls_avg") ([double]$warnThresholds.draw_call_count_percent))
}

function New-BaselineEntry {
    param(
        [object]$Record,
        [string]$ReportRoot
    )

    $entry = [PSCustomObject][ordered]@{
        updated_utc = (Get-Date).ToUniversalTime().ToString("o")
        source_report = $ReportRoot
        frames_sampled = [int64]$Record.frames_sampled
        cpu_frame_time_avg_ms = [Math]::Round([double]$Record.cpu_frame_time_avg_ms, 6)
        cpu_frame_time_p95_ms = [Math]::Round([double]$Record.cpu_frame_time_p95_ms, 6)
        cpu_frame_time_p99_ms = [Math]::Round([double]$Record.cpu_frame_time_p99_ms, 6)
        process_private_bytes_peak_mb = [Math]::Round([double]$Record.process_private_bytes_peak_mb, 6)
        engine_heap_peak_mb = [Math]::Round([double]$Record.engine_heap_peak_mb, 6)
        draw_calls_avg = [Math]::Round([double]$Record.draw_calls_avg, 6)
    }

    if ($Record.gpu_baseline_comparable -and
        @($Record.required_gpu_metrics).Count -gt 0 -and
        $null -ne $Record.gpu_metric_summaries) {
        $gpuMetrics = [PSCustomObject]@{}
        foreach ($metricName in @($Record.required_gpu_metrics)) {
            $metric = Get-ProfileProperty $Record.gpu_metric_summaries ([string]$metricName)
            if ($null -eq $metric) {
                throw "Cannot bless an invalid required GPU metric '$metricName': metric was missing."
            }
            $coverage = ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $metric "coverage")
            $avg = Get-ProfileProperty $metric "avg"
            if ($null -eq $avg) {
                $avg = Get-ProfileProperty $metric "avg_ms"
            }
            $p95 = Get-ProfileProperty $metric "p95"
            if ($null -eq $p95) {
                $p95 = Get-ProfileProperty $metric "p95_ms"
            }
            $validatedAverage = ConvertTo-PerfGateFiniteDouble $avg
            $validatedP95 = ConvertTo-PerfGateFiniteDouble $p95
            if ($null -eq $coverage -or $coverage -lt 0.0 -or $coverage -gt 1.0 -or
                $null -eq $validatedAverage -or $validatedAverage -lt 0.0 -or
                $null -eq $validatedP95 -or $validatedP95 -lt 0.0) {
                throw "Cannot bless an invalid required GPU metric '$metricName': coverage, avg, and p95 must be finite non-negative numbers and coverage must be within [0, 1]."
            }
            $gpuMetrics | Add-Member -MemberType NoteProperty -Name ([string]$metricName) -Value ([PSCustomObject][ordered]@{
                avg = [Math]::Round($validatedAverage, 6)
                p95 = [Math]::Round($validatedP95, 6)
            })
        }
        if (@($gpuMetrics.PSObject.Properties).Count -gt 0) {
            $entry | Add-Member -MemberType NoteProperty -Name "gpu_metrics" -Value $gpuMetrics
        }
    }

    return $entry
}

function Update-BaselinesFromRecords {
    param(
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [object[]]$Records,
        [string]$ReportRoot
    )

    Assert-PerfGateRunRecords -Records $Records
    foreach ($record in $Records) {
        $terminationProperty = $record.PSObject.Properties["tree_termination_confirmed"]
        if ($null -ne $terminationProperty -and -not [bool]$terminationProperty.Value) {
            throw "Cannot bless a baseline after unconfirmed process-tree termination."
        }
        $jobCleanupProperty = $record.PSObject.Properties["job_cleanup_confirmed"]
        if ($null -ne $jobCleanupProperty -and -not [bool]$jobCleanupProperty.Value) {
            throw "Cannot bless a baseline after unconfirmed Job Object cleanup."
        }
    }

    $baselinesNode = Ensure-ObjectProperty $Baseline "baselines"
    $profileNode = Ensure-ObjectProperty $baselinesNode $Profile
    $configurationNode = Ensure-ObjectProperty $profileNode $Configuration

    foreach ($record in $Records) {
        if ($record.status -eq "FAIL" -or $record.status -eq "DRY_RUN") {
            continue
        }
        $targetNode = Ensure-ObjectProperty $configurationNode $record.target
        Set-ObjectProperty $targetNode $record.backend (New-BaselineEntry -Record $record -ReportRoot $ReportRoot)
    }
}

function ConvertTo-MarkdownCell {
    param([object]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return ([string]$Value).Replace("|", "\|").Replace("`r", " ").Replace("`n", " ")
}

function Format-OptionalNumber {
    param(
        [object]$Value,
        [int]$Digits = 4
    )

    if ($null -eq $Value) {
        return "n/a"
    }
    return [Math]::Round([double]$Value, $Digits)
}

function Format-MarkdownCell {
    param(
        [string]$Value,
        [int]$Width,
        [string]$Alignment
    )

    if ($Alignment -eq "Right") {
        return $Value.PadLeft($Width)
    }
    return $Value.PadRight($Width)
}

function New-MarkdownTable {
    param(
        [string[]]$Headers,
        [string[]]$Alignments,
        [System.Collections.IEnumerable]$Rows
    )

    $formattedRows = New-Object 'System.Collections.Generic.List[string[]]'
    $widths = @()
    for ($i = 0; $i -lt $Headers.Count; ++$i) {
        $widths += [Math]::Max(3, $Headers[$i].Length)
    }

    foreach ($row in $Rows) {
        $formattedRow = @()
        for ($i = 0; $i -lt $Headers.Count; ++$i) {
            $cell = ""
            if ($i -lt $row.Count) {
                $cell = ConvertTo-MarkdownCell $row[$i]
            }
            $formattedRow += $cell
            $widths[$i] = [Math]::Max($widths[$i], $cell.Length)
        }
        $formattedRows.Add([string[]]$formattedRow) | Out-Null
    }

    $lines = @()
    $headerCells = @()
    $separatorCells = @()
    for ($i = 0; $i -lt $Headers.Count; ++$i) {
        $alignment = if ($i -lt $Alignments.Count) { $Alignments[$i] } else { "Left" }
        $headerCells += Format-MarkdownCell $Headers[$i] $widths[$i] $alignment
        if ($alignment -eq "Right") {
            $separatorCells += (("-" * ($widths[$i] - 1)) + ":")
        }
        else {
            $separatorCells += ("-" * $widths[$i])
        }
    }
    $lines += "| " + ($headerCells -join " | ") + " |"
    $lines += "| " + ($separatorCells -join " | ") + " |"

    foreach ($row in $formattedRows) {
        $cells = @()
        for ($i = 0; $i -lt $Headers.Count; ++$i) {
            $alignment = if ($i -lt $Alignments.Count) { $Alignments[$i] } else { "Left" }
            $cells += Format-MarkdownCell $row[$i] $widths[$i] $alignment
        }
        $lines += "| " + ($cells -join " | ") + " |"
    }

    return $lines
}

function Get-RunLogFiles {
    param(
        [string]$RepoRoot,
        [datetime]$Since
    )

    $logRoot = Join-Path $RepoRoot "product/logs"
    if (-not (Test-Path -LiteralPath $logRoot)) {
        return @()
    }

    return @(Get-ChildItem -LiteralPath $logRoot -File -Filter "*.logfile" |
        Where-Object { $_.LastWriteTime -ge $Since.AddSeconds(-1) } |
        Sort-Object LastWriteTime)
}

function Test-LogForDiagnostics {
    param(
        [object]$Record,
        [System.IO.FileInfo[]]$LogFiles
    )

    $failurePatterns = @(
        "VUID-",
        "Validation Error",
        "VK_VALIDATION_ERROR",
        "D3D12 ERROR",
        "DXGI ERROR",
        "GPU-Based Validation ERROR",
        "CORRUPTION"
    )
    $warningPatterns = @(
        "D3D12 WARNING",
        "DXGI WARNING",
        "Validation Warning"
    )

    $Record.engine_logs = @($LogFiles | ForEach-Object { $_.FullName })
    foreach ($logFile in $LogFiles) {
        foreach ($pattern in $failurePatterns) {
            $matches = Select-String -LiteralPath $logFile.FullName -Pattern $pattern -SimpleMatch -ErrorAction SilentlyContinue
            if ($matches) {
                Add-Failure $Record "Diagnostic failure pattern '$pattern' found in $($logFile.Name)"
                break
            }
        }
        foreach ($pattern in $warningPatterns) {
            $matches = Select-String -LiteralPath $logFile.FullName -Pattern $pattern -SimpleMatch -ErrorAction SilentlyContinue
            if ($matches) {
                Add-Warning $Record "Diagnostic warning pattern '$pattern' found in $($logFile.Name)"
                break
            }
        }
    }
}

function Initialize-GateJobInterop {
    if ($null -ne ("AshPerfGate.JobObject" -as [type])) {
        return
    }

    # This immediate assignment contract is limited to trusted Editor/Sandbox processes on Windows 10/11.
    # If launch-time spawning or breakaway is introduced, replace Process.Start with native suspended
    # CreateProcess plus PROC_THREAD_ATTRIBUTE_JOB_LIST before resuming the primary thread.
    $source = @'
using System;
using System.ComponentModel;
using System.Runtime.ConstrainedExecution;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace AshPerfGate
{
    internal sealed class SafeJobHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal bool ReleaseSucceeded { get; private set; }
        internal int ReleaseError { get; private set; }

        private SafeJobHandle() : base(true) { }

        internal SafeJobHandle(IntPtr handle) : base(true)
        {
            SetHandle(handle);
            ReleaseSucceeded = true;
        }

        [ReliabilityContract(Consistency.WillNotCorruptState, Cer.Success)]
        protected override bool ReleaseHandle()
        {
            ReleaseSucceeded = NativeMethods.CloseHandle(handle);
            if (!ReleaseSucceeded)
            {
                ReleaseError = Marshal.GetLastWin32Error();
            }
            return ReleaseSucceeded;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct IO_COUNTERS
    {
        internal UInt64 ReadOperationCount;
        internal UInt64 WriteOperationCount;
        internal UInt64 OtherOperationCount;
        internal UInt64 ReadTransferCount;
        internal UInt64 WriteTransferCount;
        internal UInt64 OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        internal Int64 PerProcessUserTimeLimit;
        internal Int64 PerJobUserTimeLimit;
        internal UInt32 LimitFlags;
        internal UIntPtr MinimumWorkingSetSize;
        internal UIntPtr MaximumWorkingSetSize;
        internal UInt32 ActiveProcessLimit;
        internal UIntPtr Affinity;
        internal UInt32 PriorityClass;
        internal UInt32 SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        internal JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        internal IO_COUNTERS IoInfo;
        internal UIntPtr ProcessMemoryLimit;
        internal UIntPtr JobMemoryLimit;
        internal UIntPtr PeakProcessMemoryUsed;
        internal UIntPtr PeakJobMemoryUsed;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
    {
        internal Int64 TotalUserTime;
        internal Int64 TotalKernelTime;
        internal Int64 ThisPeriodTotalUserTime;
        internal Int64 ThisPeriodTotalKernelTime;
        internal UInt32 TotalPageFaultCount;
        internal UInt32 TotalProcesses;
        internal UInt32 ActiveProcesses;
        internal UInt32 TotalTerminatedProcesses;
    }

    internal static class NativeMethods
    {
        internal const UInt32 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        internal const Int32 JobObjectBasicAccountingInformation = 1;
        internal const Int32 JobObjectExtendedLimitInformation = 9;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern IntPtr CreateJobObjectW(IntPtr securityAttributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetInformationJobObject(
            SafeJobHandle job,
            Int32 informationClass,
            ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION information,
            UInt32 informationLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool AssignProcessToJobObject(SafeJobHandle job, IntPtr processHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool TerminateJobObject(SafeJobHandle job, UInt32 exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool QueryInformationJobObject(
            SafeJobHandle job,
            Int32 informationClass,
            out JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information,
            UInt32 informationLength,
            IntPtr returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CloseHandle(IntPtr handle);
    }

    public sealed class JobObject : IDisposable
    {
        private SafeJobHandle handle;
        private bool closed;

        public bool KillOnJobCloseEnabled { get; private set; }

        public JobObject()
        {
            IntPtr rawHandle = NativeMethods.CreateJobObjectW(IntPtr.Zero, null);
            if (rawHandle == IntPtr.Zero || rawHandle == new IntPtr(-1))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObjectW failed.");
            }

            handle = new SafeJobHandle(rawHandle);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            limits.BasicLimitInformation.LimitFlags = NativeMethods.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!NativeMethods.SetInformationJobObject(
                handle,
                NativeMethods.JobObjectExtendedLimitInformation,
                ref limits,
                (UInt32)Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION))))
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "SetInformationJobObject(KILL_ON_JOB_CLOSE) failed.");
            }
            KillOnJobCloseEnabled = true;
        }

        public void Assign(IntPtr processHandle)
        {
            EnsureOpen();
            if (!NativeMethods.AssignProcessToJobObject(handle, processHandle))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "AssignProcessToJobObject failed.");
            }
        }

        public void Terminate(UInt32 exitCode)
        {
            EnsureOpen();
            if (!NativeMethods.TerminateJobObject(handle, exitCode))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "TerminateJobObject failed.");
            }
        }

        public UInt32 QueryActiveProcesses()
        {
            EnsureOpen();
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
            if (!NativeMethods.QueryInformationJobObject(
                handle,
                NativeMethods.JobObjectBasicAccountingInformation,
                out accounting,
                (UInt32)Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)),
                IntPtr.Zero))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryInformationJobObject failed.");
            }
            return accounting.ActiveProcesses;
        }

        public void Close()
        {
            if (closed)
            {
                return;
            }
            closed = true;
            SafeJobHandle closingHandle = handle;
            handle = null;
            closingHandle.Dispose();
            if (!closingHandle.ReleaseSucceeded)
            {
                throw new Win32Exception(closingHandle.ReleaseError, "CloseHandle(job) failed.");
            }
        }

        public void Dispose()
        {
            Close();
        }

        private void EnsureOpen()
        {
            if (closed || handle == null || handle.IsInvalid || handle.IsClosed)
            {
                throw new ObjectDisposedException("JobObject");
            }
        }
    }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function New-GateJobObject {
    Initialize-GateJobInterop
    return New-Object AshPerfGate.JobObject
}

function Complete-GateJob {
    param(
        [object]$Job,
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMilliseconds
    )

    $errors = @()
    $terminateSucceeded = $false
    $querySucceeded = $false
    $rootExited = $false
    $closeSucceeded = $false
    $activeProcesses = $null
    $deadline = [datetime]::UtcNow.AddMilliseconds([Math]::Max(1, $TimeoutMilliseconds))

    try {
        try {
            $Job.Terminate([uint32]1)
            $terminateSucceeded = $true
        }
        catch {
            $errors += "TerminateJobObject failed: $($_.Exception.Message)"
        }

        if ($terminateSucceeded) {
            do {
                try {
                    $activeProcesses = [uint32]$Job.QueryActiveProcesses()
                    $querySucceeded = $true
                }
                catch {
                    $errors += "QueryInformationJobObject failed: $($_.Exception.Message)"
                    $querySucceeded = $false
                    break
                }
                if ($activeProcesses -eq 0) {
                    break
                }
                $remaining = [int][Math]::Max(0, ($deadline - [datetime]::UtcNow).TotalMilliseconds)
                if ($remaining -le 0) {
                    break
                }
                Start-Sleep -Milliseconds ([Math]::Min(10, $remaining))
            } while ([datetime]::UtcNow -lt $deadline)
        }

        $remainingForRoot = [int][Math]::Max(0, ($deadline - [datetime]::UtcNow).TotalMilliseconds)
        try {
            $rootExited = $Process.HasExited -or ($remainingForRoot -gt 0 -and $Process.WaitForExit($remainingForRoot))
        }
        catch {
            $errors += "Root process exit confirmation failed: $($_.Exception.Message)"
        }
    }
    finally {
        try {
            $Job.Close()
            $closeSucceeded = $true
        }
        catch {
            $errors += "CloseHandle(job) failed: $($_.Exception.Message)"
        }
    }

    if (-not $terminateSucceeded -or -not $querySucceeded -or $activeProcesses -ne 0 -or -not $rootExited -or -not $closeSucceeded) {
        if ($querySucceeded -and $activeProcesses -ne 0) {
            $errors += "Job Object still reported $activeProcesses active process(es) at the cleanup deadline"
        }
    }
    return [PSCustomObject]@{
        confirmed = $terminateSucceeded -and $querySucceeded -and $activeProcesses -eq 0 -and $rootExited -and $closeSucceeded
        root_exited = $rootExited
        active_processes = $activeProcesses
        close_attempted = $true
        errors = $errors
    }
}

function Set-RemainingRunRecordsAborted {
    param(
        [object[]]$Records,
        [int]$StartIndex,
        [string]$Reason
    )

    for ($recordIndex = [Math]::Max(0, $StartIndex); $recordIndex -lt @($Records).Count; ++$recordIndex) {
        if ($Records[$recordIndex].status -eq "NOT_RUN") {
            Add-Failure $Records[$recordIndex] "Run aborted: $Reason"
        }
    }
}

function Invoke-GateProcess {
    param(
        [object]$Record,
        [string]$RunDirectory,
        [string[]]$Arguments,
        [double]$TimeoutSeconds,
        [scriptblock]$JobFactory = $null,
        [scriptblock]$AfterJobAssignment = $null
    )

    $processLogDirectory = Split-Path -Parent $Record.process_log
    New-Item -ItemType Directory -Force -Path $processLogDirectory | Out-Null
    New-Item -ItemType File -Force -Path $Record.process_log | Out-Null
    New-Item -ItemType File -Force -Path $Record.process_error_log | Out-Null

    $timeoutMilliseconds = [Math]::Max(1, [int]([double]$TimeoutSeconds * 1000.0))
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = New-GateProcessStartInfo `
        -Executable $Record.executable `
        -Arguments $Arguments `
        -WorkingDirectory $RunDirectory
    $job = $null
    $jobCloseAttempted = $false
    $processStarted = $false
    $standardOutputTask = $null
    $standardErrorTask = $null
    try {
        try {
            $job = if ($null -ne $JobFactory) { & $JobFactory } else { New-GateJobObject }
        }
        catch {
            Add-Failure $Record "Failed to create Job Object: $($_.Exception.Message)"
            return $false
        }
        if ($null -eq $job -or -not [bool]$job.KillOnJobCloseEnabled) {
            Add-Failure $Record "Job Object was not created with KILL_ON_JOB_CLOSE"
            return $false
        }
        $Record.job_kill_on_close = $true

        try {
            $processStarted = $process.Start()
        }
        catch {
            Add-Failure $Record "Failed to start process: $($_.Exception.Message)"
            return $false
        }
        if (-not $processStarted) {
            Add-Failure $Record "Failed to start process"
            return $false
        }

        try {
            $job.Assign($process.Handle)
            $Record.job_assigned = $true
        }
        catch {
            Add-Failure $Record "AssignProcessToJobObject failed: $($_.Exception.Message)"
            try {
                if (-not $process.HasExited) {
                    $process.Kill()
                }
                $Record.root_exited = $process.HasExited -or $process.WaitForExit(2000)
            }
            catch {
                Add-Failure $Record "Assigned-root failure cleanup could not confirm process exit: $($_.Exception.Message)"
            }
            return $false
        }

        $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
        $standardErrorTask = $process.StandardError.ReadToEndAsync()
        $afterAssignmentFailed = $false
        if ($null -ne $AfterJobAssignment) {
            try {
                & $AfterJobAssignment
            }
            catch {
                Add-Failure $Record "After-assignment action failed: $($_.Exception.Message)"
                $afterAssignmentFailed = $true
            }
        }

        $rootExitedBeforeDeadline = $false
        if (-not $afterAssignmentFailed) {
            try {
                $rootExitedBeforeDeadline = $process.WaitForExit($timeoutMilliseconds)
            }
            catch {
                Add-Failure $Record "Root process wait failed: $($_.Exception.Message)"
            }
            if (-not $rootExitedBeforeDeadline) {
                Add-Failure $Record "Process timed out after $TimeoutSeconds seconds"
            }
            else {
                try {
                    $process.Refresh()
                    $Record.exit_code = $process.ExitCode
                    if ($Record.exit_code -ne 0) {
                        Add-Failure $Record "Process exited with code $($Record.exit_code)"
                    }
                }
                catch {
                    Add-Failure $Record "Could not read process exit status: $($_.Exception.Message)"
                }
            }
        }

        $jobCleanup = Complete-GateJob -Job $job -Process $process -TimeoutMilliseconds 2000
        $jobCloseAttempted = [bool]$jobCleanup.close_attempted
        $Record.root_exited = [bool]$jobCleanup.root_exited
        $Record.job_active_processes_after_cleanup = $jobCleanup.active_processes
        foreach ($cleanupError in @($jobCleanup.errors)) {
            Add-Failure $Record $cleanupError
        }

        $outputDrain = Complete-GateProcessOutput `
            -StandardOutputTask $standardOutputTask `
            -StandardErrorTask $standardErrorTask `
            -TimeoutMilliseconds 2000
        Set-Content -LiteralPath $Record.process_log -Value $outputDrain.standard_output -Encoding UTF8
        Set-Content -LiteralPath $Record.process_error_log -Value $outputDrain.standard_error -Encoding UTF8
        if (-not $outputDrain.completed) {
            Add-Failure $Record "Redirected stdout/stderr did not drain within the bounded deadline"
        }

        $cleanupConfirmed = [bool]$jobCleanup.confirmed -and $outputDrain.completed -and -not $afterAssignmentFailed
        $Record.job_cleanup_confirmed = $cleanupConfirmed
        $Record.tree_termination_confirmed = $cleanupConfirmed
        if (-not $cleanupConfirmed) {
            if (@($jobCleanup.errors).Count -eq 0 -and $outputDrain.completed -and -not $afterAssignmentFailed) {
                Add-Failure $Record "Job Object cleanup could not be confirmed"
            }
        }
        return $cleanupConfirmed
    }
    finally {
        if ($null -ne $job -and -not $jobCloseAttempted) {
            try {
                $job.Close()
            }
            catch {
                Add-Failure $Record "CloseHandle(job) failed: $($_.Exception.Message)"
            }
        }
        $process.Dispose()
    }
}

function Test-Telemetry {
    param(
        [object]$Record,
        [object]$ProfileConfig,
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration
    )

    if (-not (Test-Path -LiteralPath $Record.telemetry)) {
        Add-Failure $Record "Missing telemetry JSON: $($Record.telemetry)"
        return
    }

    try {
        $telemetry = Get-Content -Raw -LiteralPath $Record.telemetry | ConvertFrom-Json
    }
    catch {
        Add-Failure $Record "Failed to parse telemetry JSON: $($_.Exception.Message)"
        return
    }

    Test-TelemetryData `
        -Record $Record `
        -Telemetry $telemetry `
        -ProfileConfig $ProfileConfig `
        -Baseline $Baseline `
        -Profile $Profile `
        -Configuration $Configuration `
        -TelemetryMode $Record.telemetry_mode
}

function Get-GpuMetricSummaryValue {
    param(
        [object]$Metric,
        [string]$Name
    )

    $value = Get-ProfileProperty $Metric $Name
    if ($null -eq $value) {
        $value = Get-ProfileProperty $Metric ("{0}_ms" -f $Name)
    }
    return $value
}

function Get-FirstNonBlankProperty {
    param(
        [object]$Object,
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $value = Get-ProfileProperty $Object $name
        if ($null -eq $value) {
            continue
        }
        $text = ([string]$value).Trim()
        if (-not [string]::IsNullOrWhiteSpace($text)) {
            return $text
        }
    }
    return $null
}

function Test-TelemetryData {
    param(
        [object]$Record,
        [object]$Telemetry,
        [object]$ProfileConfig,
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [ValidateSet("Profile", "Off")]
        [string]$TelemetryMode
    )

    $schemaVersionValue = Get-ProfileProperty $Telemetry "schema_version"
    $schemaVersionIsJsonInteger = $schemaVersionValue -is [System.Int32] -or $schemaVersionValue -is [System.Int64]
    $schemaVersion = if ($schemaVersionIsJsonInteger) { [int64]$schemaVersionValue } else { $null }
    if (-not $schemaVersionIsJsonInteger -or $schemaVersion -notin @(1, 2)) {
        Add-Failure $Record "Unsupported telemetry schema_version: $schemaVersionValue"
    }
    if ([string]$Telemetry.backend_actual -ne $Record.backend) {
        Add-Failure $Record "Backend mismatch: requested $($Record.backend), actual $($Telemetry.backend_actual)"
    }
    if ([int64]$Telemetry.frames_sampled -le 0) {
        Add-Failure $Record "No sampled frames were reported"
    }

    $Record.frames_sampled = [int64]$Telemetry.frames_sampled
    $Record.cpu_frame_time_avg_ms = [double]$Telemetry.cpu_frame_time_ms.avg
    $Record.cpu_frame_time_p95_ms = [double]$Telemetry.cpu_frame_time_ms.p95
    $Record.cpu_frame_time_p99_ms = [double]$Telemetry.cpu_frame_time_ms.p99
    $Record.process_private_bytes_peak_mb = [double]$Telemetry.memory.process_private_bytes_peak_mb
    $Record.engine_heap_peak_mb = [double]$Telemetry.memory.engine_heap_peak_mb
    $Record.draw_calls_avg = [double]$Telemetry.render_stats.draw_calls_avg

    if ([int64]$Telemetry.memory.engine_heap_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "Engine heap live bytes at shutdown: $($Telemetry.memory.engine_heap_shutdown_live_bytes)"
    }
    if ([bool]$Telemetry.memory.gpu_allocator_supported -and [int64]$Telemetry.memory.gpu_allocator_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "GPU allocator live bytes at shutdown: $($Telemetry.memory.gpu_allocator_shutdown_live_bytes)"
    }

    $absoluteCaps = Get-ProfileProperty $ProfileConfig "absolute_caps"
    $targetKey = $Record.target.ToLowerInvariant()
    $capName = "{0}_private_bytes_mb" -f $targetKey
    $privateBytesCap = Get-ProfileProperty $absoluteCaps $capName
    if ($null -ne $privateBytesCap -and $Record.process_private_bytes_peak_mb -gt [double]$privateBytesCap) {
        Add-Failure $Record "Private bytes peak $($Record.process_private_bytes_peak_mb) MB exceeded cap $privateBytesCap MB"
    }

    $gpuTimingRequirement = Get-ProfileProperty $ProfileConfig "gpu_timing"
    $gpuRequired = $TelemetryMode -ne "Off" -and [string]$gpuTimingRequirement -eq "required"
    $requiredGpuMetrics = Get-ProfileProperty $ProfileConfig "required_gpu_metrics"
    if ($null -ne $requiredGpuMetrics) {
        $Record.required_gpu_metrics = @($requiredGpuMetrics)
    }
    $Record.gpu_baseline_comparable = ($TelemetryMode -ne "Off")
    $runtimeV2Required = $null -ne (Get-ProfileProperty $ProfileConfig "window_width") -or
        $null -ne (Get-ProfileProperty $ProfileConfig "window_height") -or
        $null -ne (Get-ProfileProperty $ProfileConfig "fixed_camera")
    if (($gpuRequired -or $runtimeV2Required) -and $schemaVersion -ne 2) {
        Add-Failure $Record "Fixed-runtime perf profile requires telemetry schema_version 2"
    }

    if ($schemaVersion -eq 2) {
        $runtime = Get-ProfileProperty $Telemetry "runtime"
        if ($null -eq $runtime) {
            Add-Failure $Record "Telemetry schema v2 is missing runtime metadata"
        }
        else {
            $runtimeExtent = Get-ProfileProperty $runtime "extent"
            if ($null -ne $runtimeExtent) {
                $Record.actual_width = [int](Get-ProfileProperty $runtimeExtent "width")
                $Record.actual_height = [int](Get-ProfileProperty $runtimeExtent "height")
                $stableValue = Get-ProfileProperty $runtimeExtent "stable"
                $Record.extent_stable = $null -ne $stableValue -and [bool]$stableValue
            }
            $validationValue = Get-ProfileProperty $runtime "validation"
            if ($null -ne $validationValue) { $Record.validation = [bool]$validationValue }
            $vsyncValue = Get-ProfileProperty $runtime "vsync"
            if ($null -ne $vsyncValue) { $Record.vsync = [bool]$vsyncValue }
            $fixedCameraValue = Get-ProfileProperty $runtime "fixed_camera"
            if ($null -ne $fixedCameraValue) { $Record.fixed_camera = [bool]$fixedCameraValue }
            $frameCapValue = Get-ProfileProperty $runtime "frame_cap"
            if ($null -ne $frameCapValue) { $Record.frame_cap = [string]$frameCapValue }

            $expectedWidth = Get-ProfileProperty $ProfileConfig "window_width"
            $expectedHeight = Get-ProfileProperty $ProfileConfig "window_height"
            if ($null -ne $expectedWidth -and $null -ne $expectedHeight) {
                if ($null -eq $runtimeExtent -or $Record.actual_width -ne [int]$expectedWidth -or $Record.actual_height -ne [int]$expectedHeight) {
                    Add-Failure $Record ("Runtime extent {0}x{1} did not match profile extent {2}x{3}" -f $Record.actual_width, $Record.actual_height, $expectedWidth, $expectedHeight)
                }
                elseif (-not [bool]$Record.extent_stable) {
                    Add-Failure $Record "Runtime extent was not stable during sampling"
                }
            }

            $expectedVsync = Get-ProfileProperty $ProfileConfig "vsync"
            if ($null -ne $expectedVsync -and ($null -eq $Record.vsync -or [bool]$Record.vsync -ne [bool]$expectedVsync)) {
                Add-Failure $Record "Runtime vsync did not match the profile override"
            }
            $expectedValidation = Get-ProfileProperty $ProfileConfig "validation"
            if ($null -ne $expectedValidation -and ($null -eq $Record.validation -or [bool]$Record.validation -ne [bool]$expectedValidation)) {
                Add-Failure $Record "Runtime validation did not match the profile override"
            }
            $expectedFixedCamera = Get-ProfileProperty $ProfileConfig "fixed_camera"
            if ($null -ne $expectedFixedCamera -and ($null -eq $Record.fixed_camera -or [bool]$Record.fixed_camera -ne [bool]$expectedFixedCamera)) {
                Add-Failure $Record "Runtime fixed-camera state did not match the profile"
            }
            $expectedFrameCap = Get-ProfileProperty $ProfileConfig "frame_cap"
            if ($null -ne $expectedFrameCap -and $Record.frame_cap -ne [string]$expectedFrameCap) {
                Add-Failure $Record "Runtime frame cap did not match the profile"
            }
            $runtimeConfiguration = Get-ProfileProperty $runtime "configuration"
            if (($gpuRequired -or $runtimeV2Required) -and $null -eq $runtimeConfiguration) {
                Add-Failure $Record "Runtime configuration metadata was missing"
            }
            elseif ($null -ne $runtimeConfiguration -and [string]$runtimeConfiguration -ne $Configuration) {
                Add-Failure $Record "Runtime configuration '$runtimeConfiguration' did not match '$Configuration'"
            }
        }

        $gpu = Get-ProfileProperty $Telemetry "gpu"
        $backendInfo = $null
        $adapter = $null
        $driver = $null
        $validatedGpuCoverage = $null
        $validatedGpuCounts = @{}
        if ($null -ne $gpu) {
            $coverage = Get-ProfileProperty $gpu "coverage"
            $validatedGpuCoverage = ConvertTo-PerfGateFiniteDouble $coverage
            if ($null -eq $coverage) {
                if ($gpuRequired) {
                    Add-Failure $Record "GPU coverage must be a finite number within [0, 1]"
                }
            }
            elseif ($null -eq $validatedGpuCoverage -or $validatedGpuCoverage -lt 0.0 -or $validatedGpuCoverage -gt 1.0) {
                Add-Failure $Record "GPU coverage '$coverage' must be a finite number within [0, 1]"
                $validatedGpuCoverage = $null
            }
            else {
                $Record.gpu_coverage = $validatedGpuCoverage
            }

            foreach ($countContract in @(
                [PSCustomObject]@{ name = "submitted"; record_property = "gpu_submitted" },
                [PSCustomObject]@{ name = "resolved"; record_property = "gpu_resolved" },
                [PSCustomObject]@{ name = "valid"; record_property = "gpu_valid" },
                [PSCustomObject]@{ name = "invalid"; record_property = "gpu_invalid" }
            )) {
                $rawCount = Get-ProfileProperty $gpu $countContract.name
                $validatedCount = ConvertTo-PerfGateNonNegativeInt64 $rawCount
                if ($null -eq $rawCount) {
                    if ($gpuRequired) {
                        Add-Failure $Record "GPU $($countContract.name) must be a non-negative integer"
                    }
                    continue
                }
                if ($null -eq $validatedCount) {
                    Add-Failure $Record "GPU $($countContract.name) '$rawCount' must be a non-negative integer"
                    continue
                }
                $validatedGpuCounts[$countContract.name] = $validatedCount
                $Record.($countContract.record_property) = $validatedCount
            }

            if ($validatedGpuCounts.Count -eq 4) {
                $submittedCount = [int64]$validatedGpuCounts["submitted"]
                $resolvedCount = [int64]$validatedGpuCounts["resolved"]
                $validCount = [int64]$validatedGpuCounts["valid"]
                $invalidCount = [int64]$validatedGpuCounts["invalid"]
                $countsAreConsistent = $resolvedCount -le $submittedCount -and
                    $validCount -le $resolvedCount -and
                    $invalidCount -le $resolvedCount -and
                    $validCount -eq ($resolvedCount - $invalidCount)
                if (-not $countsAreConsistent) {
                    Add-Failure $Record "GPU sample counts were inconsistent: require resolved <= submitted and resolved = valid + invalid"
                }
                if ($null -ne $validatedGpuCoverage) {
                    $expectedCoverage = if ($submittedCount -eq 0) { 0.0 } else { [double]$validCount / [double]$submittedCount }
                    if ([Math]::Abs($validatedGpuCoverage - $expectedCoverage) -gt 0.000001) {
                        Add-Failure $Record ("GPU coverage {0} did not match valid / submitted ({1})" -f $validatedGpuCoverage, $expectedCoverage)
                    }
                }
            }

            $metrics = Get-ProfileProperty $gpu "metrics"
            if ($null -ne $metrics) { $Record.gpu_metric_summaries = $metrics }

            $backendInfo = Get-ProfileProperty $gpu "backend_info"
            if ($null -ne $backendInfo) {
                $adapter = Get-FirstNonBlankProperty $backendInfo @("adapter", "adapter_name")
                if ($null -ne $adapter) { $Record.gpu_adapter = [string]$adapter }
                $driver = Get-FirstNonBlankProperty $backendInfo @("driver", "driver_name", "driver_version")
                if ($null -ne $driver) { $Record.gpu_driver = [string]$driver }
            }
        }

        if ($gpuRequired -or $runtimeV2Required) {
            if ($null -eq $gpu) {
                Add-Failure $Record "GPU hardware metadata was missing because telemetry.gpu was absent"
            }
            elseif ($null -eq $backendInfo) {
                Add-Failure $Record "GPU hardware metadata backend_info was missing"
            }
            else {
                if ([string]::IsNullOrWhiteSpace([string]$adapter)) {
                    Add-Failure $Record "GPU adapter hardware metadata was missing or blank"
                }
                if ([string]::IsNullOrWhiteSpace([string]$driver)) {
                    Add-Failure $Record "GPU driver hardware metadata was missing or blank"
                }
            }
        }

        if ($gpuRequired) {
            if ($null -eq $gpu) {
                Add-Failure $Record "GPU timing profile is missing GPU telemetry"
            }
            else {
                $minimumCoverage = [double]$ProfileConfig.min_gpu_coverage
                if ($null -eq $Record.gpu_coverage -or [double]$Record.gpu_coverage -lt $minimumCoverage) {
                    Add-Failure $Record ("GPU coverage {0} was below required {1}" -f $Record.gpu_coverage, $minimumCoverage)
                }

                foreach ($requiredMetric in @($ProfileConfig.required_gpu_metrics)) {
                    $metric = Get-ProfileProperty $Record.gpu_metric_summaries ([string]$requiredMetric)
                    if ($null -eq $metric) {
                        Add-Failure $Record "Required GPU metric '$requiredMetric' was missing"
                        continue
                    }
                    $metricCoverageRaw = Get-ProfileProperty $metric "coverage"
                    $metricCoverage = ConvertTo-PerfGateFiniteDouble $metricCoverageRaw
                    if ($null -eq $metricCoverage -or $metricCoverage -lt 0.0 -or $metricCoverage -gt 1.0) {
                        Add-Failure $Record ("Required GPU metric '{0}' coverage '{1}' must be a finite number within [0, 1]" -f $requiredMetric, $metricCoverageRaw)
                    }
                    elseif ($metricCoverage -lt $minimumCoverage) {
                        Add-Failure $Record ("Required GPU metric '{0}' coverage {1} was below required {2}" -f $requiredMetric, $metricCoverage, $minimumCoverage)
                    }
                    $metricAverageRaw = Get-GpuMetricSummaryValue $metric "avg"
                    $metricP95Raw = Get-GpuMetricSummaryValue $metric "p95"
                    $metricAverage = ConvertTo-PerfGateFiniteDouble $metricAverageRaw
                    $metricP95 = ConvertTo-PerfGateFiniteDouble $metricP95Raw
                    if ($null -eq $metricAverage -or $metricAverage -lt 0.0) {
                        Add-Failure $Record "Required GPU metric '$requiredMetric' avg must be a finite non-negative number"
                    }
                    if ($null -eq $metricP95 -or $metricP95 -lt 0.0) {
                        Add-Failure $Record "Required GPU metric '$requiredMetric' p95 must be a finite non-negative number"
                    }
                }

                $frameMetric = Get-ProfileProperty $Record.gpu_metric_summaries "GPU.Frame"
                if ($null -ne $frameMetric) {
                    $frameAverage = ConvertTo-PerfGateFiniteDouble (Get-GpuMetricSummaryValue $frameMetric "avg")
                    $frameP95 = ConvertTo-PerfGateFiniteDouble (Get-GpuMetricSummaryValue $frameMetric "p95")
                    if ($null -ne $frameAverage -and $frameAverage -ge 0.0 -and
                        $null -ne $frameP95 -and $frameP95 -ge 0.0) {
                        $Record.gpu_frame_avg_ms = [double]$frameAverage
                        $Record.gpu_frame_p95_ms = [double]$frameP95
                    }
                }
            }
        }
    }

    if ($Record.status -eq "NOT_RUN") {
        $Record.status = "PASS"
    }
    if ($Record.status -ne "FAIL") {
        Compare-RecordToBaseline -Record $Record -Baseline $Baseline -ProfileConfig $ProfileConfig -Profile $Profile -Configuration $Configuration
    }
}

function Invoke-RunPerfGateSelfTest {
    function Assert-SelfTest {
        param(
            [bool]$Condition,
            [string]$Message
        )

        if (-not $Condition) {
            throw $Message
        }
    }

    function Assert-SelfTestThrows {
        param(
            [scriptblock]$Action,
            [string]$Pattern,
            [string]$Message
        )

        $caught = $null
        try {
            & $Action
        }
        catch {
            $caught = $_
        }

        if ($null -eq $caught) {
            throw $Message
        }
        if ($caught.Exception.Message -notmatch $Pattern) {
            throw "$Message Actual error: $($caught.Exception.Message)"
        }
    }

    function Test-SelfTestThrows {
        param(
            [scriptblock]$Action,
            [string]$Pattern
        )

        try {
            & $Action
        }
        catch {
            return $_.Exception.Message -match $Pattern
        }
        return $false
    }

    function Test-SelfTestProcessAlive {
        param([int]$ProcessId)

        $process = $null
        try {
            $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
            return -not $process.HasExited
        }
        catch [System.ArgumentException] {
            return $false
        }
        finally {
            if ($null -ne $process) {
                $process.Dispose()
            }
        }
    }

    function Stop-SelfTestProcess {
        param([int]$ProcessId)

        $process = $null
        try {
            $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit(1000) | Out-Null
            }
        }
        catch [System.ArgumentException] {
        }
        finally {
            if ($null -ne $process) {
                $process.Dispose()
            }
        }
    }

    function New-SelfTestJobDouble {
        param([string]$FailOperation)

        $job = [PSCustomObject]@{
            fail_operation = $FailOperation
            KillOnJobCloseEnabled = $true
            assigned = $false
            active_processes = 0
            closed = $false
        }
        $job | Add-Member -MemberType ScriptMethod -Name "Assign" -Value {
            param([IntPtr]$ProcessHandle)
            if ($this.fail_operation -eq "assign") { throw "injected assign failure" }
            $this.assigned = $true
            $this.active_processes = 1
        }
        $job | Add-Member -MemberType ScriptMethod -Name "Terminate" -Value {
            param([uint32]$ExitCode)
            if ($this.fail_operation -eq "terminate") { throw "injected terminate failure" }
            $this.active_processes = 0
        }
        $job | Add-Member -MemberType ScriptMethod -Name "QueryActiveProcesses" -Value {
            if ($this.fail_operation -eq "query") { throw "injected query failure" }
            return [uint32]$this.active_processes
        }
        $job | Add-Member -MemberType ScriptMethod -Name "Close" -Value {
            if ($this.fail_operation -eq "close") { throw "injected close failure" }
            $this.closed = $true
        }
        return $job
    }

    function New-SelfTestTelemetryV2 {
        param(
            [object]$ProfileConfig,
            [double]$GpuCoverage = 0.96,
            [int]$Width = 2560,
            [int]$Height = 1440,
            [bool]$ExtentStable = $true
        )

        $metrics = [PSCustomObject]@{}
        foreach ($metricName in @($ProfileConfig.required_gpu_metrics)) {
            $metrics | Add-Member -MemberType NoteProperty -Name ([string]$metricName) -Value ([PSCustomObject]@{
                coverage = 0.96
                avg = 1.25
                p50 = 1.20
                p95 = 1.50
                p99 = 1.60
                min = 1.00
                max = 1.75
            })
        }

        return [PSCustomObject]@{
            schema_version = 2
            backend_actual = "Vulkan"
            frames_sampled = 120
            cpu_frame_time_ms = [PSCustomObject]@{ avg = 2.0; p95 = 2.5; p99 = 3.0 }
            memory = [PSCustomObject]@{
                process_private_bytes_peak_mb = 100.0
                engine_heap_peak_mb = 10.0
                engine_heap_shutdown_live_bytes = 0
                gpu_allocator_supported = $true
                gpu_allocator_shutdown_live_bytes = 0
            }
            render_stats = [PSCustomObject]@{ draw_calls_avg = 50.0 }
            runtime = [PSCustomObject]@{
                configuration = "Release"
                extent = [PSCustomObject]@{ width = $Width; height = $Height; stable = $ExtentStable }
                vsync = $false
                frame_cap = "off"
                validation = $false
                fixed_camera = $true
            }
            gpu = [PSCustomObject]@{
                scope = "main_command_buffer"
                submitted = 125
                resolved = 122
                valid = 120
                invalid = 2
                coverage = $GpuCoverage
                invalid_reasons = [PSCustomObject]@{}
                backend_info = [PSCustomObject]@{
                    adapter_name = "SelfTest GPU"
                    driver_version = "SelfTest Driver"
                }
                metrics = $metrics
            }
        }
    }

    $repoRoot = Get-RepoRoot
    $loaderPath = Join-Path $repoRoot "scripts/PerfGateProfileConfig.ps1"
    if (-not (Test-Path -LiteralPath $loaderPath)) {
        throw "Missing shared perf profile loader: $loaderPath"
    }
    . $loaderPath

    $qualityContractFailures = @()

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-gate-selftest-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    try {
        $profilePath = Join-Path $tempRoot "profiles.json"
        $testBaselinePath = Join-Path $tempRoot "baselines.json"
        @'
{
  "schema_version": 1,
  "profiles": {
    "Collision": {
      "configuration": "Release",
      "configuration_locked": true,
      "targets": [{ "name": "Sandbox", "backends": ["Vulkan", "DX12"] }]
    }
  }
}
'@ | Set-Content -LiteralPath $profilePath -Encoding UTF8
        @'
{
  "schema_version": 1,
  "profiles": {
    "Collision": {
      "configuration": "Debug",
      "targets": [{ "target": "Editor", "backends": ["Vulkan"] }]
    },
    "LegacyOnly": {
      "configuration": "Debug",
      "targets": [{ "target": "Editor", "backends": ["DX12"] }]
    }
  },
  "baselines": {}
}
'@ | Set-Content -LiteralPath $testBaselinePath -Encoding UTF8

        $catalog = Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $testBaselinePath
        $collision = Get-PerfGateProfileConfig -Catalog $catalog -Profile "Collision"
        Assert-SelfTest ($collision.configuration -eq "Release") "New profile file must win over baseline.profiles."
        Assert-SelfTest ((Get-PerfGateProfileSource -Catalog $catalog -Profile "Collision") -eq "profiles") "New profile source was not recorded."

        $legacyOnly = Get-PerfGateProfileConfig -Catalog $catalog -Profile "LegacyOnly"
        Assert-SelfTest ($legacyOnly.configuration -eq "Debug") "Missing new profile must fall back to baseline.profiles."
        Assert-SelfTest ((Get-PerfGateProfileSource -Catalog $catalog -Profile "LegacyOnly") -eq "baseline.profiles") "Legacy fallback source was not recorded."

        Assert-SelfTestThrows { Get-PerfGateProfileConfig -Catalog $catalog -Profile "Unknown" | Out-Null } "Unknown perf gate profile" "Unknown profiles must fail."
        Assert-SelfTestThrows { Resolve-PerfGateConfiguration -ProfileConfig $collision -RequestedConfiguration "Debug" | Out-Null } "locked.*Release" "Locked Release profile must reject Debug."
        Assert-SelfTest ((Resolve-PerfGateConfiguration -ProfileConfig $collision -RequestedConfiguration "") -eq "Release") "Locked profile default configuration was not resolved."

        $unsupportedBaselinePath = Join-Path $tempRoot "unsupported-baseline.json"
        '{ "schema_version": 2, "profiles": {}, "baselines": {} }' | Set-Content -LiteralPath $unsupportedBaselinePath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $unsupportedBaselinePath | Out-Null } "Unsupported perf gate baseline schema_version")) {
            $qualityContractFailures += "baseline schema_version 2 was accepted"
        }

        $fractionalProfileSchemaPath = Join-Path $tempRoot "fractional-profile-schema.json"
        '{ "schema_version": 1.1, "profiles": {} }' | Set-Content -LiteralPath $fractionalProfileSchemaPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $fractionalProfileSchemaPath -BaselinePath $testBaselinePath | Out-Null } "Unsupported perf gate profile schema_version")) {
            $qualityContractFailures += "fractional profile schema_version 1.1 was accepted"
        }

        $fractionalBaselineSchemaPath = Join-Path $tempRoot "fractional-baseline-schema.json"
        '{ "schema_version": 1.1, "profiles": {}, "baselines": {} }' | Set-Content -LiteralPath $fractionalBaselineSchemaPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $fractionalBaselineSchemaPath | Out-Null } "Unsupported perf gate baseline schema_version")) {
            $qualityContractFailures += "fractional baseline schema_version 1.1 was accepted"
        }

        $stringBaselineSchemaPath = Join-Path $tempRoot "string-baseline-schema.json"
        '{ "schema_version": "1", "profiles": {}, "baselines": {} }' | Set-Content -LiteralPath $stringBaselineSchemaPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $stringBaselineSchemaPath | Out-Null } "Unsupported perf gate baseline schema_version")) {
            $qualityContractFailures += "string baseline schema_version 1 was accepted"
        }

        $missingBaselineSchemaPath = Join-Path $tempRoot "missing-baseline-schema.json"
        '{ "profiles": {}, "baselines": {} }' | Set-Content -LiteralPath $missingBaselineSchemaPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $missingBaselineSchemaPath | Out-Null } "Unsupported perf gate baseline schema_version")) {
            $qualityContractFailures += "baseline without schema_version was accepted"
        }

        $invalidBaselinesRootPath = Join-Path $tempRoot "invalid-baselines-root.json"
        '{ "schema_version": 1, "baselines": [] }' | Set-Content -LiteralPath $invalidBaselinesRootPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $invalidBaselinesRootPath | Out-Null } "baselines.*object")) {
            $qualityContractFailures += "baseline baselines array was accepted"
        }

        $invalidProfileDefinitionsPath = Join-Path $tempRoot "invalid-profile-definitions.json"
        '{ "schema_version": 1, "profiles": [] }' | Set-Content -LiteralPath $invalidProfileDefinitionsPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $invalidProfileDefinitionsPath -BaselinePath $testBaselinePath | Out-Null } "profiles.*object")) {
            $qualityContractFailures += "profile definitions array was accepted"
        }

        $invalidLegacyProfilesPath = Join-Path $tempRoot "invalid-legacy-profiles.json"
        '{ "schema_version": 1, "profiles": [], "baselines": {} }' | Set-Content -LiteralPath $invalidLegacyProfilesPath -Encoding UTF8
        if (-not (Test-SelfTestThrows { Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $invalidLegacyProfilesPath | Out-Null } "profiles.*object")) {
            $qualityContractFailures += "baseline legacy profiles array was accepted"
        }

        $baselineWithoutLegacyProfilesPath = Join-Path $tempRoot "baseline-without-legacy-profiles.json"
        '{ "schema_version": 1, "baselines": {} }' | Set-Content -LiteralPath $baselineWithoutLegacyProfilesPath -Encoding UTF8
        try {
            Import-PerfGateProfileCatalog -ProfilesPath $profilePath -BaselinePath $baselineWithoutLegacyProfilesPath | Out-Null
        }
        catch {
            $qualityContractFailures += "baseline without optional legacy profiles was rejected"
        }
    }
    finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $repoCatalog = Import-PerfGateProfileCatalog `
        -ProfilesPath (Join-Path $repoRoot "tools/perf/perf_gate_profiles.json") `
        -BaselinePath (Join-Path $repoRoot "tools/perf/perf_gate_baselines.json")
    $vegetationProfile = Get-PerfGateProfileConfig -Catalog $repoCatalog -Profile "VegetationFullPipeline"
    Assert-SelfTest ($vegetationProfile.configuration -eq "Release") "Vegetation profile must use Release."
    Assert-SelfTest ([bool]$vegetationProfile.configuration_locked) "Vegetation profile configuration must be locked."
    Assert-SelfTest ([int]$vegetationProfile.window_width -eq 2560 -and [int]$vegetationProfile.window_height -eq 1440) "Vegetation profile extent must be 2560x1440."
    Assert-SelfTest ([double]$vegetationProfile.warmup_seconds -eq 10.0 -and [double]$vegetationProfile.sample_seconds -eq 30.0 -and [double]$vegetationProfile.drain_seconds -eq 5.0 -and [double]$vegetationProfile.timeout_seconds -eq 90.0) "Vegetation profile timing contract mismatch."
    Assert-SelfTest ($vegetationProfile.gpu_timing -eq "required" -and [double]$vegetationProfile.min_gpu_coverage -eq 0.95) "Vegetation GPU timing contract mismatch."
    Assert-SelfTest (@($vegetationProfile.required_gpu_metrics).Count -eq 11) "Vegetation profile must require exactly 11 GPU metrics."
    Assert-SelfTest (-not [bool]$vegetationProfile.vsync -and -not [bool]$vegetationProfile.validation -and $vegetationProfile.frame_cap -eq "off" -and [bool]$vegetationProfile.fixed_camera) "Vegetation runtime overrides mismatch."

    $runPlan = New-PerfGateRunPlan `
        -RepoRoot $repoRoot `
        -ReportRoot (Join-Path $repoRoot "Intermediate/test-reports/perf-gate/self-test-plan") `
        -Profile "VegetationFullPipeline" `
        -ProfileConfig $vegetationProfile `
        -Configuration "Release" `
        -TelemetryMode "Profile"
    Assert-SelfTest (@($runPlan).Count -eq 2) "Vegetation profile must create exactly two runs."
    Assert-SelfTest ($runPlan[0].target -eq "Sandbox" -and $runPlan[0].backend -eq "Vulkan") "First Vegetation run must be Sandbox Vulkan."
    Assert-SelfTest ($runPlan[1].target -eq "Sandbox" -and $runPlan[1].backend -eq "DX12") "Second Vegetation run must be Sandbox DX12."
    foreach ($plannedRun in $runPlan) {
        Assert-SelfTest ($plannedRun.configuration -eq "Release") "Every Vegetation run must use Release."
        foreach ($expectedArgument in @(
            "--rhi=$($plannedRun.backend.ToLowerInvariant())",
            "--scene=product/assets/scenes/VegetationBaseline.scene.json",
            "--window-width=2560",
            "--window-height=1440",
            "--perf-gate-vsync=off",
            "--perf-gate-validation=off",
            "--perf-gate-gpu-timing=on",
            "--perf-gate-drain-seconds=5"
        )) {
            Assert-SelfTest (@($plannedRun.arguments) -contains $expectedArgument) "Run plan did not propagate '$expectedArgument'."
        }
    }

    $buildCommands = @(Get-PerfGateBuildCommands -ProfileConfig $vegetationProfile -Configuration "Release")
    Assert-SelfTest ($buildCommands.Count -eq 1) "Vegetation profile must build only one declared target."
    Assert-SelfTest ($buildCommands[0].target -eq "Sandbox" -and $buildCommands[0].command_line -eq "build_sandbox.bat Release x64") "Vegetation build plan must contain only Sandbox Release."

    $emptyTargetsProfile = ConvertFrom-Json '{ "warmup_seconds": 1, "sample_seconds": 1, "targets": [] }'
    if (-not (Test-SelfTestThrows { New-PerfGateRunPlan -RepoRoot $repoRoot -ReportRoot "self-test" -Profile "EmptyTargets" -ProfileConfig $emptyTargetsProfile -Configuration "Debug" -TelemetryMode "Profile" | Out-Null } "at least one target")) {
        $qualityContractFailures += "empty target matrix was accepted"
    }

    $emptyBackendsProfile = ConvertFrom-Json '{ "warmup_seconds": 1, "sample_seconds": 1, "targets": [{ "name": "Sandbox", "backends": [] }] }'
    if (-not (Test-SelfTestThrows { New-PerfGateRunPlan -RepoRoot $repoRoot -ReportRoot "self-test" -Profile "EmptyBackends" -ProfileConfig $emptyBackendsProfile -Configuration "Debug" -TelemetryMode "Profile" | Out-Null } "at least one backend")) {
        $qualityContractFailures += "empty backend matrix was accepted"
    }

    $duplicateTargetsProfile = ConvertFrom-Json '{ "warmup_seconds": 1, "sample_seconds": 1, "targets": [{ "name": "Sandbox", "backends": ["Vulkan"] }, { "name": "sandbox", "backends": ["DX12"] }] }'
    if (-not (Test-SelfTestThrows { New-PerfGateRunPlan -RepoRoot $repoRoot -ReportRoot "self-test" -Profile "DuplicateTargets" -ProfileConfig $duplicateTargetsProfile -Configuration "Debug" -TelemetryMode "Profile" | Out-Null } "Duplicate perf gate target")) {
        $qualityContractFailures += "duplicate target matrix was accepted"
    }

    $duplicateBackendsProfile = ConvertFrom-Json '{ "warmup_seconds": 1, "sample_seconds": 1, "targets": [{ "name": "Sandbox", "backends": ["Vulkan", "vulkan"] }] }'
    if (-not (Test-SelfTestThrows { New-PerfGateRunPlan -RepoRoot $repoRoot -ReportRoot "self-test" -Profile "DuplicateBackends" -ProfileConfig $duplicateBackendsProfile -Configuration "Debug" -TelemetryMode "Profile" | Out-Null } "Duplicate perf gate backend")) {
        $qualityContractFailures += "duplicate backend matrix was accepted"
    }

    if ($null -eq (Get-Command Assert-PerfGateRunRecords -ErrorAction SilentlyContinue)) {
        $qualityContractFailures += "zero-run assertion is missing"
    }
    elseif (-not (Test-SelfTestThrows { Assert-PerfGateRunRecords -Records @() } "at least one run")) {
        $qualityContractFailures += "zero generated records were accepted"
    }

    $standardProfile = Get-PerfGateProfileConfig -Catalog $repoCatalog -Profile "Standard"
    $schemaV1 = ConvertFrom-Json @'
{
  "schema_version": 1,
  "backend_actual": "Vulkan",
  "frames_sampled": 120,
  "cpu_frame_time_ms": { "avg": 2.0, "p95": 2.5, "p99": 3.0 },
  "memory": {
    "process_private_bytes_peak_mb": 100.0,
    "engine_heap_peak_mb": 10.0,
    "engine_heap_shutdown_live_bytes": 0,
    "gpu_allocator_supported": true,
    "gpu_allocator_shutdown_live_bytes": 0
  },
  "render_stats": { "draw_calls_avg": 50.0 }
}
'@
    $emptyBaselineForTelemetry = ConvertFrom-Json '{ "baselines": {} }'
    $schemaV1Record = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $schemaV1Record -Telemetry $schemaV1 -ProfileConfig $standardProfile -Baseline $emptyBaselineForTelemetry -Profile "Standard" -Configuration "Debug" -TelemetryMode "Profile"
    Assert-SelfTest ($schemaV1Record.status -eq "PASS") "Schema v1 must remain accepted for Standard."
    Assert-SelfTest ($schemaV1Record.baseline_status -eq "MISSING") "Missing Standard baseline must be reported as MISSING."

    foreach ($invalidTelemetrySchemaVersion in @(1.1, "1")) {
        $invalidSchemaTelemetry = $schemaV1 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $invalidSchemaTelemetry.schema_version = $invalidTelemetrySchemaVersion
        $invalidSchemaRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-TelemetryData -Record $invalidSchemaRecord -Telemetry $invalidSchemaTelemetry -ProfileConfig $standardProfile -Baseline $emptyBaselineForTelemetry -Profile "Standard" -Configuration "Debug" -TelemetryMode "Profile"
        if ($invalidSchemaRecord.status -ne "FAIL" -or (@($invalidSchemaRecord.failures) -join " ") -notmatch "Unsupported telemetry schema_version") {
            $qualityContractFailures += "non-integer telemetry schema_version $invalidTelemetrySchemaVersion was accepted"
        }
    }

    $telemetryOffSchemaV1Record = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $telemetryOffSchemaV1Record -Telemetry $schemaV1 -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Off"
    Assert-SelfTest ($telemetryOffSchemaV1Record.status -eq "FAIL" -and (@($telemetryOffSchemaV1Record.failures) -join " ") -match "schema_version 2") "Telemetry-off Vegetation A/B still requires schema v2 runtime metadata."

    $validV2 = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $candidateRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $candidateRecord.root_exited = $true
    $candidateRecord.tree_termination_confirmed = $true
    $candidateRecord.job_cleanup_confirmed = $true
    Test-TelemetryData -Record $candidateRecord -Telemetry $validV2 -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($candidateRecord.status -eq "PASS" -and $candidateRecord.baseline_status -eq "MISSING") "Missing Vegetation baseline must remain a passing candidate marked MISSING."
    Assert-SelfTest ([double]$candidateRecord.gpu_frame_avg_ms -eq 1.25 -and [double]$candidateRecord.gpu_frame_p95_ms -eq 1.5) "GPU.Frame summary was not captured."
    Assert-SelfTest ($candidateRecord.gpu_adapter -eq "SelfTest GPU" -and $candidateRecord.gpu_driver -eq "SelfTest Driver") "GPU adapter/driver metadata was not captured."

    foreach ($aliasCase in @(
        [PSCustomObject]@{ adapter_property = "adapter"; driver_property = "driver"; suffix = "direct" },
        [PSCustomObject]@{ adapter_property = "adapter_name"; driver_property = "driver_name"; suffix = "named" },
        [PSCustomObject]@{ adapter_property = "adapter_name"; driver_property = "driver_version"; suffix = "versioned" }
    )) {
        $aliasTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
        $aliasTelemetry.gpu.backend_info = [PSCustomObject]@{}
        $expectedAdapter = "Alias GPU $($aliasCase.suffix)"
        $expectedDriver = "Alias Driver $($aliasCase.suffix)"
        $aliasTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name $aliasCase.adapter_property -Value $expectedAdapter
        $aliasTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name $aliasCase.driver_property -Value $expectedDriver
        $aliasRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-TelemetryData -Record $aliasRecord -Telemetry $aliasTelemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
        Assert-SelfTest ($aliasRecord.status -eq "PASS" -and $aliasRecord.gpu_adapter -eq $expectedAdapter -and $aliasRecord.gpu_driver -eq $expectedDriver) "GPU hardware metadata alias contract failed for $($aliasCase.suffix)."
    }

    $fallbackAliasTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $fallbackAliasTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "adapter" -Value " `t "
    $fallbackAliasTelemetry.gpu.backend_info.adapter_name = "  Fallback GPU  "
    $fallbackAliasTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "driver" -Value "   "
    $fallbackAliasTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "driver_name" -Value "  Fallback Driver  "
    $fallbackAliasRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $fallbackAliasRecord -Telemetry $fallbackAliasTelemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($fallbackAliasRecord.status -eq "PASS" -and $fallbackAliasRecord.gpu_adapter -eq "Fallback GPU" -and $fallbackAliasRecord.gpu_driver -eq "Fallback Driver") "GPU hardware metadata must trim values and skip blank aliases before fallback."

    $missingGpuTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingGpuTelemetry.PSObject.Properties.Remove("gpu")
    $missingBackendInfoTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingBackendInfoTelemetry.gpu.PSObject.Properties.Remove("backend_info")
    $missingAdapterTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingAdapterTelemetry.gpu.backend_info.PSObject.Properties.Remove("adapter_name")
    $blankAdapterTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $blankAdapterTelemetry.gpu.backend_info.adapter_name = " `t "
    $blankAdapterTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "adapter" -Value "   "
    $missingDriverTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingDriverTelemetry.gpu.backend_info.PSObject.Properties.Remove("driver_version")
    $blankDriverTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $blankDriverTelemetry.gpu.backend_info.driver_version = " `t "
    $blankDriverTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "driver" -Value "   "
    $blankDriverTelemetry.gpu.backend_info | Add-Member -MemberType NoteProperty -Name "driver_name" -Value "`t"

    $hardwareMetadataCases = @(
        [PSCustomObject]@{ label = "missing gpu"; telemetry = $missingGpuTelemetry; telemetry_mode = "Off"; failure_pattern = "GPU hardware metadata" },
        [PSCustomObject]@{ label = "missing backend_info"; telemetry = $missingBackendInfoTelemetry; telemetry_mode = "Off"; failure_pattern = "backend_info" },
        [PSCustomObject]@{ label = "missing adapter"; telemetry = $missingAdapterTelemetry; telemetry_mode = "Profile"; failure_pattern = "adapter" },
        [PSCustomObject]@{ label = "blank adapter aliases"; telemetry = $blankAdapterTelemetry; telemetry_mode = "Profile"; failure_pattern = "adapter" },
        [PSCustomObject]@{ label = "missing driver"; telemetry = $missingDriverTelemetry; telemetry_mode = "Profile"; failure_pattern = "driver" },
        [PSCustomObject]@{ label = "blank driver aliases"; telemetry = $blankDriverTelemetry; telemetry_mode = "Profile"; failure_pattern = "driver" }
    )
    $unexpectedHardwareMetadataPasses = @()
    foreach ($metadataCase in $hardwareMetadataCases) {
        $metadataRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-TelemetryData -Record $metadataRecord -Telemetry $metadataCase.telemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode $metadataCase.telemetry_mode
        $failureText = @($metadataRecord.failures) -join " "
        if ($metadataRecord.status -ne "FAIL" -or $failureText -notmatch $metadataCase.failure_pattern) {
            $unexpectedHardwareMetadataPasses += $metadataCase.label
        }
    }
    Assert-SelfTest ($unexpectedHardwareMetadataPasses.Count -eq 0) ("Fixed profile accepted invalid GPU hardware metadata: {0}" -f ($unexpectedHardwareMetadataPasses -join ", "))

    $lowCoverage = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile -GpuCoverage 0.94
    $lowCoverage.gpu.submitted = 100
    $lowCoverage.gpu.resolved = 100
    $lowCoverage.gpu.valid = 94
    $lowCoverage.gpu.invalid = 6
    $lowCoverageRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $lowCoverageRecord -Telemetry $lowCoverage -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($lowCoverageRecord.status -eq "FAIL" -and (@($lowCoverageRecord.failures) -join " ") -match "GPU coverage") "Schema v2 total GPU coverage below threshold must fail."

    $lowMetricCoverage = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $lowMetricCoverage.gpu.metrics.'GPU.Bloom'.coverage = 0.94
    $lowMetricRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $lowMetricRecord -Telemetry $lowMetricCoverage -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($lowMetricRecord.status -eq "FAIL" -and (@($lowMetricRecord.failures) -join " ") -match "GPU.Bloom") "Schema v2 required metric coverage below threshold must fail."

    $missingMetricSummary = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingMetricSummary.gpu.metrics.'GPU.Bloom'.PSObject.Properties.Remove("p95")
    $missingMetricSummaryRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $missingMetricSummaryRecord -Telemetry $missingMetricSummary -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($missingMetricSummaryRecord.status -eq "FAIL" -and (@($missingMetricSummaryRecord.failures) -join " ") -match "GPU.Bloom.*p95") "Every required GPU metric must report avg and p95."

    $invalidGpuValueCases = @(
        [PSCustomObject]@{
            label = "nonnumeric aggregate coverage"
            failure_pattern = "GPU coverage.*finite.*0.*1"
            mutate = { param($Telemetry) $Telemetry.gpu.coverage = "bad" }
        },
        [PSCustomObject]@{
            label = "out-of-range aggregate coverage"
            failure_pattern = "GPU coverage.*finite.*0.*1"
            mutate = { param($Telemetry) $Telemetry.gpu.coverage = 1.5 }
        },
        [PSCustomObject]@{
            label = "negative aggregate count"
            failure_pattern = "resolved.*non-negative integer"
            mutate = { param($Telemetry) $Telemetry.gpu.resolved = -1 }
        },
        [PSCustomObject]@{
            label = "string aggregate count"
            failure_pattern = "submitted.*non-negative integer"
            mutate = { param($Telemetry) $Telemetry.gpu.submitted = "125" }
        },
        [PSCustomObject]@{
            label = "inconsistent aggregate counts"
            failure_pattern = "GPU sample counts were inconsistent"
            mutate = {
                param($Telemetry)
                $Telemetry.gpu.valid = 119
                $Telemetry.gpu.coverage = 0.952
            }
        },
        [PSCustomObject]@{
            label = "aggregate coverage/count mismatch"
            failure_pattern = "GPU coverage.*valid / submitted"
            mutate = { param($Telemetry) $Telemetry.gpu.coverage = 0.99 }
        },
        [PSCustomObject]@{
            label = "invalid required metric values"
            failure_pattern = "GPU.Bloom.*(coverage|avg|p95)"
            mutate = {
                param($Telemetry)
                $Telemetry.gpu.metrics.'GPU.Bloom'.coverage = 1.5
                $Telemetry.gpu.metrics.'GPU.Bloom'.avg = ""
                $Telemetry.gpu.metrics.'GPU.Bloom'.p95 = -1
            }
        },
        [PSCustomObject]@{
            label = "nonfinite required metric values"
            failure_pattern = "GPU.Bloom.*(coverage|avg|p95)"
            mutate = {
                param($Telemetry)
                $Telemetry.gpu.metrics.'GPU.Bloom'.coverage = [double]::PositiveInfinity
                $Telemetry.gpu.metrics.'GPU.Bloom'.avg = [double]::NaN
                $Telemetry.gpu.metrics.'GPU.Bloom'.p95 = [double]::PositiveInfinity
            }
        }
    )
    foreach ($invalidGpuValueCase in $invalidGpuValueCases) {
        $invalidGpuTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
        $mutateInvalidGpuTelemetry = [scriptblock]$invalidGpuValueCase.mutate
        & $mutateInvalidGpuTelemetry $invalidGpuTelemetry
        $invalidGpuRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
        try {
            Test-TelemetryData -Record $invalidGpuRecord -Telemetry $invalidGpuTelemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
        }
        catch {
            $qualityContractFailures += "$($invalidGpuValueCase.label) escaped validation and threw: $($_.Exception.Message)"
            continue
        }
        if ($invalidGpuRecord.status -ne "FAIL" -or (@($invalidGpuRecord.failures) -join " ") -notmatch $invalidGpuValueCase.failure_pattern) {
            $qualityContractFailures += "$($invalidGpuValueCase.label) was accepted"
        }
    }

    $unsafeGpuBlessTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $unsafeGpuBlessTelemetry.gpu.metrics.'GPU.Bloom'.avg = ""
    $unsafeGpuBlessTelemetry.gpu.metrics.'GPU.Bloom'.p95 = -1
    $unsafeGpuBlessRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "unsafe-gpu.json" "unsafe-gpu.out" "unsafe-gpu.err"
    $unsafeGpuBlessRecord.status = "PASS"
    $unsafeGpuBlessRecord.tree_termination_confirmed = $true
    $unsafeGpuBlessRecord.job_cleanup_confirmed = $true
    $unsafeGpuBlessRecord.gpu_baseline_comparable = $true
    $unsafeGpuBlessRecord.required_gpu_metrics = @($vegetationProfile.required_gpu_metrics)
    $unsafeGpuBlessRecord.gpu_metric_summaries = $unsafeGpuBlessTelemetry.gpu.metrics
    $unsafeGpuBlessBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $unsafeGpuBlessBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Records @($unsafeGpuBlessRecord) -ReportRoot "self-test"
    } "invalid required GPU metric")) {
        $qualityContractFailures += "invalid required GPU timing values could bless a partial baseline"
    }

    $wrongExtent = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile -Width 1920 -Height 1080
    $wrongExtentRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $wrongExtentRecord -Telemetry $wrongExtent -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($wrongExtentRecord.status -eq "FAIL" -and (@($wrongExtentRecord.failures) -join " ") -match "extent") "Schema v2 wrong extent must fail."

    $unstableExtent = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile -ExtentStable $false
    $unstableExtentRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $unstableExtentRecord -Telemetry $unstableExtent -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($unstableExtentRecord.status -eq "FAIL" -and (@($unstableExtentRecord.failures) -join " ") -match "stable") "Schema v2 unstable extent must fail."

    $missingConfiguration = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $missingConfiguration.runtime.PSObject.Properties.Remove("configuration")
    $missingConfigurationRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $missingConfigurationRecord -Telemetry $missingConfiguration -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($missingConfigurationRecord.status -eq "FAIL" -and (@($missingConfigurationRecord.failures) -join " ") -match "configuration") "Fixed profile must reject missing runtime configuration metadata."

    Assert-SelfTestThrows { Assert-PerfGateOptions -BlessBaseline $true -DryRun $false -TelemetryMode "Off" } "TelemetryMode Off.*BlessBaseline" "Telemetry-off A/B must reject bless."

    $nonRequiredGpuRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $nonRequiredGpuRecord.status = "PASS"
    $nonRequiredGpuRecord.root_exited = $true
    $nonRequiredGpuRecord.tree_termination_confirmed = $true
    $nonRequiredGpuRecord.job_cleanup_confirmed = $true
    $nonRequiredGpuRecord.gpu_metric_summaries | Add-Member -MemberType NoteProperty -Name "GPU.Frame" -Value ([PSCustomObject]@{ avg = 1.0; p95 = 1.2 })
    $baselineWithoutRequiredMetrics = ConvertFrom-Json '{ "baselines": {} }'
    Update-BaselinesFromRecords -Baseline $baselineWithoutRequiredMetrics -Profile "Standard" -Configuration "Debug" -Records @($nonRequiredGpuRecord) -ReportRoot "Intermediate/test-reports/perf-gate/self-test"
    $baselineWithoutRequiredEntry = Get-BaselineEntry -Baseline $baselineWithoutRequiredMetrics -Profile "Standard" -Configuration "Debug" -Target "Sandbox" -Backend "Vulkan"
    Assert-SelfTest ($null -eq (Get-ProfileProperty $baselineWithoutRequiredEntry "gpu_metrics")) "Bless must not write GPU metrics unless the profile declares them required."

    $baselineForBless = ConvertFrom-Json '{ "schema_version": 1, "profiles": { "KeepMe": { "configuration": "Debug" } }, "baselines": {} }'
    $profilesBeforeBless = $baselineForBless.profiles | ConvertTo-Json -Depth 8 -Compress
    Update-BaselinesFromRecords -Baseline $baselineForBless -Profile "VegetationFullPipeline" -Configuration "Release" -Records @($candidateRecord) -ReportRoot "Intermediate/test-reports/perf-gate/self-test"
    $profilesAfterBless = $baselineForBless.profiles | ConvertTo-Json -Depth 8 -Compress
    Assert-SelfTest ($profilesAfterBless -ceq $profilesBeforeBless) "Bless must never modify profile definitions."
    $candidateBaseline = Get-BaselineEntry -Baseline $baselineForBless -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
    Assert-SelfTest ($null -ne $candidateBaseline) "Bless did not write baselines.* candidate data."
    Assert-SelfTest (@($candidateBaseline.gpu_metrics.PSObject.Properties).Count -eq 11) "Bless must write avg/p95 for all required GPU metrics."

    $runnerSource = Get-Content -Raw -LiteralPath $PSCommandPath
    $cmdLaunchAssignments = [regex]::Matches($runnerSource, '(?m)\.FileName\s*=\s*"cmd\.exe"')
    if ($cmdLaunchAssignments.Count -ne 1) {
        $qualityContractFailures += "cmd.exe is still used outside .bat builds"
    }
    $invokeGateSource = [regex]::Match($runnerSource, '(?s)function Invoke-GateProcess\s*\{.*?(?=\r?\nfunction Test-Telemetry\s*\{)').Value
    if ($invokeGateSource -notmatch 'New-GateProcessStartInfo' -or $invokeGateSource -match 'cmd\.exe') {
        $qualityContractFailures += "gate process does not use direct ProcessStartInfo"
    }
    if ([regex]::Matches($invokeGateSource, 'ReadToEndAsync\s*\(').Count -lt 2 -or $invokeGateSource -notmatch 'Complete-GateProcessOutput') {
        $qualityContractFailures += "gate process does not asynchronously drain both redirected streams"
    }

    $crtEncoder = Get-Command ConvertTo-WindowsCrtArgument -ErrorAction SilentlyContinue
    if ($null -eq $crtEncoder) {
        $qualityContractFailures += "Windows CRT argument encoder is missing"
    }
    else {
        foreach ($crtCase in @(
            [PSCustomObject]@{ value = ""; expected = '""'; label = "empty" },
            [PSCustomObject]@{ value = "plain"; expected = 'plain'; label = "plain" },
            [PSCustomObject]@{ value = "two words"; expected = '"two words"'; label = "spaces" },
            [PSCustomObject]@{ value = 'C:\path with spaces\'; expected = '"C:\path with spaces\\"'; label = "trailing slash" },
            [PSCustomObject]@{ value = 'say"hi'; expected = '"say\"hi"'; label = "embedded quote" },
            [PSCustomObject]@{ value = '&|<>^%!()'; expected = '&|<>^%!()'; label = "shell metacharacters" }
        )) {
            $encoded = ConvertTo-WindowsCrtArgument $crtCase.value
            if ($encoded -cne $crtCase.expected) {
                $qualityContractFailures += "Windows CRT encoding failed for $($crtCase.label): '$encoded'"
            }
        }

        $threeSlashesBeforeQuote = 'a\\\"b'
        $expectedThreeSlashEncoding = '"a' + ((@('\') * 7) -join '') + '"b"'
        $encodedThreeSlashes = ConvertTo-WindowsCrtArgument $threeSlashesBeforeQuote
        if ($encodedThreeSlashes -cne $expectedThreeSlashEncoding) {
            $qualityContractFailures += "Windows CRT encoding failed for three backslashes before quote: '$encodedThreeSlashes'"
        }
    }

    $startInfoFactory = Get-Command New-GateProcessStartInfo -ErrorAction SilentlyContinue
    if ($null -eq $startInfoFactory) {
        $qualityContractFailures += "direct gate ProcessStartInfo factory is missing"
    }
    else {
        $directStartInfo = New-GateProcessStartInfo `
            -Executable 'C:\Program Files\Ash Tool.exe' `
            -Arguments @('plain', 'two words', 'C:\path with spaces\') `
            -WorkingDirectory 'C:\work'
        if ($directStartInfo.FileName -cne 'C:\Program Files\Ash Tool.exe' -or
            $directStartInfo.Arguments -cne 'plain "two words" "C:\path with spaces\\"' -or
            $directStartInfo.WorkingDirectory -cne 'C:\work' -or
            $directStartInfo.UseShellExecute -or
            -not $directStartInfo.RedirectStandardOutput -or
            -not $directStartInfo.RedirectStandardError -or
            -not $directStartInfo.CreateNoWindow) {
            $qualityContractFailures += "direct gate ProcessStartInfo contract mismatch"
        }
    }

    $outputDrainer = Get-Command Complete-GateProcessOutput -ErrorAction SilentlyContinue
    if ($null -eq $outputDrainer) {
        $qualityContractFailures += "bounded asynchronous stdout/stderr drain helper is missing"
    }
    else {
        $stdoutSource = New-Object 'System.Threading.Tasks.TaskCompletionSource[string]'
        $stderrSource = New-Object 'System.Threading.Tasks.TaskCompletionSource[string]'
        $stdoutSource.SetResult("stdout")
        $stderrSource.SetResult("stderr")
        $completedDrain = Complete-GateProcessOutput `
            -StandardOutputTask $stdoutSource.Task `
            -StandardErrorTask $stderrSource.Task `
            -TimeoutMilliseconds 100
        if (-not $completedDrain.completed -or $completedDrain.standard_output -cne "stdout" -or $completedDrain.standard_error -cne "stderr") {
            $qualityContractFailures += "completed stdout/stderr were not drained together"
        }

        $hungOutputSource = New-Object 'System.Threading.Tasks.TaskCompletionSource[string]'
        $completedErrorSource = New-Object 'System.Threading.Tasks.TaskCompletionSource[string]'
        $completedErrorSource.SetResult("stderr")
        $drainStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $timedOutDrain = Complete-GateProcessOutput `
            -StandardOutputTask $hungOutputSource.Task `
            -StandardErrorTask $completedErrorSource.Task `
            -TimeoutMilliseconds 50
        $drainStopwatch.Stop()
        if ($timedOutDrain.completed -or $drainStopwatch.ElapsedMilliseconds -gt 1000) {
            $qualityContractFailures += "stdout/stderr drain did not honor one bounded shared deadline"
        }
    }

    if ($null -ne $startInfoFactory -and $null -ne $outputDrainer) {
        $argvProbeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-argv-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $argvProbeRoot | Out-Null
        try {
            $argvProbeScript = Join-Path $argvProbeRoot "argv-probe.ps1"
            @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ProbeArguments)
[Console]::Out.Write(($ProbeArguments | ConvertTo-Json -Compress))
'@ | Set-Content -LiteralPath $argvProbeScript -Encoding UTF8

            $expectedProbeArguments = @(
                "plain",
                "two words",
                'C:\path with spaces\',
                'a\\\"b',
                '&|<>^%!()',
                ""
            )
            $probeStartInfo = New-GateProcessStartInfo `
                -Executable "powershell.exe" `
                -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $argvProbeScript) + $expectedProbeArguments) `
                -WorkingDirectory $argvProbeRoot
            $probeProcess = New-Object System.Diagnostics.Process
            $probeProcess.StartInfo = $probeStartInfo
            try {
                if (-not $probeProcess.Start()) {
                    $qualityContractFailures += "end-to-end argv probe did not start"
                }
                else {
                    $probeStdoutTask = $probeProcess.StandardOutput.ReadToEndAsync()
                    $probeStderrTask = $probeProcess.StandardError.ReadToEndAsync()
                    if (-not $probeProcess.WaitForExit(5000)) {
                        Stop-Process -Id $probeProcess.Id -Force -ErrorAction SilentlyContinue
                        $probeProcess.WaitForExit(1000) | Out-Null
                        $qualityContractFailures += "end-to-end argv probe timed out"
                    }
                    else {
                        $probeDrain = Complete-GateProcessOutput `
                            -StandardOutputTask $probeStdoutTask `
                            -StandardErrorTask $probeStderrTask `
                            -TimeoutMilliseconds 1000
                        if (-not $probeDrain.completed -or -not [string]::IsNullOrWhiteSpace($probeDrain.standard_error)) {
                            $qualityContractFailures += "end-to-end argv probe output did not drain"
                        }
                        else {
                            $parsedProbeArguments = ConvertFrom-Json $probeDrain.standard_output
                            $actualProbeArguments = @()
                            foreach ($parsedProbeArgument in $parsedProbeArguments) {
                                $actualProbeArguments += [string]$parsedProbeArgument
                            }
                            if ($actualProbeArguments.Count -ne $expectedProbeArguments.Count) {
                                $qualityContractFailures += "end-to-end argv probe argument count mismatch: expected $($expectedProbeArguments.Count), actual $($actualProbeArguments.Count), stdout '$($probeDrain.standard_output)'"
                            }
                            else {
                                for ($argumentIndex = 0; $argumentIndex -lt $expectedProbeArguments.Count; ++$argumentIndex) {
                                    if ([string]$actualProbeArguments[$argumentIndex] -cne [string]$expectedProbeArguments[$argumentIndex]) {
                                        $qualityContractFailures += "end-to-end argv probe mismatch at index $argumentIndex"
                                        break
                                    }
                                }
                            }
                        }
                    }
                }
            }
            finally {
                if (-not $probeProcess.HasExited) {
                    $probeProcess.Kill()
                    $probeProcess.WaitForExit(1000) | Out-Null
                }
                $probeProcess.Dispose()
            }
        }
        finally {
            Remove-Item -LiteralPath $argvProbeRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $productionSource = $runnerSource.Substring(0, $runnerSource.IndexOf('function Invoke-RunPerfGateSelfTest'))
    if ($productionSource -match 'Get-CimInstance|taskkill(?:\.exe)?|Stop-Process\s+-Id') {
        $qualityContractFailures += "production still uses CIM, taskkill, or PID-based Stop-Process"
    }
    if ($productionSource -notmatch 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE' -or
        $productionSource -notmatch 'AssignProcessToJobObject' -or
        $productionSource -notmatch 'TerminateJobObject' -or
        $productionSource -notmatch 'QueryInformationJobObject') {
        $qualityContractFailures += "Windows Job Object kill-on-close interop is missing"
    }

    $jobFactoryCommand = Get-Command New-GateJobObject -ErrorAction SilentlyContinue
    $jobContractReady = $null -ne $jobFactoryCommand -and
        $invokeGateSource -match '\$JobFactory' -and
        $invokeGateSource -match '\$AfterJobAssignment'
    if (-not $jobContractReady) {
        $qualityContractFailures += "gate launcher Job Object assignment contract is missing"
    }
    else {
        $jobTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-job-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $jobTestRoot | Out-Null
        $childProcessIds = @()
        try {
            $jobParentScript = Join-Path $jobTestRoot "job-parent.ps1"
            @'
param([string]$AssignedSignal, [string]$ChildPidPath, [string]$Mode)
$deadline = [datetime]::UtcNow.AddSeconds(5)
while (-not (Test-Path -LiteralPath $AssignedSignal)) {
    if ([datetime]::UtcNow -ge $deadline) { exit 41 }
    Start-Sleep -Milliseconds 10
}
$childInfo = New-Object System.Diagnostics.ProcessStartInfo
$childInfo.FileName = "powershell.exe"
$childInfo.Arguments = '-NoProfile -Command "Start-Sleep -Seconds 30"'
$childInfo.UseShellExecute = $false
$childInfo.CreateNoWindow = $true
$child = [System.Diagnostics.Process]::Start($childInfo)
[System.IO.File]::WriteAllText($ChildPidPath, [string]$child.Id)
$child.Dispose()
if ($Mode -eq "timeout") { Start-Sleep -Seconds 30 }
exit 0
'@ | Set-Content -LiteralPath $jobParentScript -Encoding UTF8

            foreach ($mode in @("normal", "timeout")) {
                $signalPath = Join-Path $jobTestRoot "$mode-assigned.signal"
                $childPidPath = Join-Path $jobTestRoot "$mode-child.pid"
                $record = New-RunRecord "Sandbox" "Vulkan" "powershell.exe" (Join-Path $jobTestRoot "$mode.json") (Join-Path $jobTestRoot "$mode.out") (Join-Path $jobTestRoot "$mode.err")
                $runMayContinue = Invoke-GateProcess `
                    -Record $record `
                    -RunDirectory $jobTestRoot `
                    -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $jobParentScript, $signalPath, $childPidPath, $mode) `
                    -TimeoutSeconds $(if ($mode -eq "timeout") { 0.5 } else { 5.0 }) `
                    -AfterJobAssignment { [System.IO.File]::WriteAllText($signalPath, "assigned") }

                if (Test-Path -LiteralPath $childPidPath) {
                    $childProcessId = [int](Get-Content -Raw -LiteralPath $childPidPath)
                    $childProcessIds += $childProcessId
                }
                else {
                    $childProcessId = 0
                }
                if (-not $runMayContinue -or
                    -not $record.job_kill_on_close -or
                    -not $record.job_assigned -or
                    -not $record.job_cleanup_confirmed -or
                    [int]$record.job_active_processes_after_cleanup -ne 0 -or
                    $childProcessId -le 0 -or
                    (Test-SelfTestProcessAlive $childProcessId)) {
                    $qualityContractFailures += "Job Object $mode cleanup did not reach ActiveProcesses=0 after assigned child"
                }
            }

            $largeOutputScript = Join-Path $jobTestRoot "large-output.ps1"
            '[Console]::Out.Write(("O" * 131072)); [Console]::Error.Write(("E" * 131072))' | Set-Content -LiteralPath $largeOutputScript -Encoding UTF8
            $largeOutputRecord = New-RunRecord "Sandbox" "Vulkan" "powershell.exe" (Join-Path $jobTestRoot "large.json") (Join-Path $jobTestRoot "large.out") (Join-Path $jobTestRoot "large.err")
            $largeOutputSafe = Invoke-GateProcess `
                -Record $largeOutputRecord `
                -RunDirectory $jobTestRoot `
                -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $largeOutputScript) `
                -TimeoutSeconds 5
            $stdoutLength = if (Test-Path -LiteralPath $largeOutputRecord.process_log) { (Get-Content -Raw -LiteralPath $largeOutputRecord.process_log).Length } else { 0 }
            $stderrLength = if (Test-Path -LiteralPath $largeOutputRecord.process_error_log) { (Get-Content -Raw -LiteralPath $largeOutputRecord.process_error_log).Length } else { 0 }
            if (-not $largeOutputSafe -or -not $largeOutputRecord.job_cleanup_confirmed -or $stdoutLength -lt 131072 -or $stderrLength -lt 131072) {
                $qualityContractFailures += "Job Object launcher did not drain large stdout and stderr without deadlock"
            }

            foreach ($failedOperation in @("create", "assign", "query", "terminate", "close")) {
                $failureJob = if ($failedOperation -eq "create") { $null } else { New-SelfTestJobDouble $failedOperation }
                $failureArguments = if ($failedOperation -eq "create" -or $failedOperation -eq "assign") {
                    @("-NoProfile", "-Command", "Start-Sleep -Seconds 30")
                }
                else {
                    @("-NoProfile", "-Command", "exit 0")
                }
                $failureRecord = New-RunRecord "Sandbox" "Vulkan" "powershell.exe" (Join-Path $jobTestRoot "$failedOperation.json") (Join-Path $jobTestRoot "$failedOperation.out") (Join-Path $jobTestRoot "$failedOperation.err")
                $failureSafe = Invoke-GateProcess `
                    -Record $failureRecord `
                    -RunDirectory $jobTestRoot `
                    -Arguments $failureArguments `
                    -TimeoutSeconds 2 `
                    -JobFactory {
                        if ($failedOperation -eq "create") { throw "injected create failure" }
                        return $failureJob
                    }
                $failureRecords = @($failureRecord, (New-RunRecord "Sandbox" "DX12" "Sandbox.exe" "next.json" "next.out" "next.err"))
                Set-RemainingRunRecordsAborted -Records $failureRecords -StartIndex 1 -Reason "$failedOperation job failure"
                $failureBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
                $blessRejected = Test-SelfTestThrows {
                    Update-BaselinesFromRecords -Baseline $failureBaseline -Profile "Standard" -Configuration "Debug" -Records $failureRecords -ReportRoot "self-test"
                } "unconfirmed process-tree termination"
                if ($failureSafe -or
                    $failureRecord.status -ne "FAIL" -or
                    $failureRecord.tree_termination_confirmed -or
                    $failureRecords[1].status -ne "FAIL" -or
                    -not $blessRejected) {
                    $qualityContractFailures += "Job Object $failedOperation failure did not fail closed, abort remaining runs, and reject bless"
                }
            }
        }
        finally {
            foreach ($childProcessId in $childProcessIds) {
                if (Test-SelfTestProcessAlive $childProcessId) {
                    Stop-SelfTestProcess $childProcessId
                }
            }
            Remove-Item -LiteralPath $jobTestRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $matrixAborter = Get-Command Set-RemainingRunRecordsAborted -ErrorAction SilentlyContinue
    if ($null -eq $matrixAborter) {
        $qualityContractFailures += "remaining matrix abort helper is missing"
    }
    else {
        $abortRecords = @(
            (New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "one.json" "one.out" "one.err"),
            (New-RunRecord "Sandbox" "DX12" "Sandbox.exe" "two.json" "two.out" "two.err")
        )
        Set-RemainingRunRecordsAborted -Records $abortRecords -StartIndex 1 -Reason "unconfirmed process tree"
        if ($abortRecords[0].status -ne "NOT_RUN" -or $abortRecords[1].status -ne "FAIL" -or (@($abortRecords[1].failures) -join " ") -notmatch "unconfirmed process tree") {
            $qualityContractFailures += "remaining matrix records were not aborted after unsafe termination"
        }
    }

    if ($runnerSource -notmatch 'Assert-PerfGateRunRecords\s+-Records\s+\$records' -or
        $runnerSource -notmatch 'Set-RemainingRunRecordsAborted') {
        $qualityContractFailures += "runner does not enforce non-empty records and matrix abort"
    }

    $runnerMainStart = $runnerSource.LastIndexOf('$repoRoot = Get-RepoRoot')
    if ($runnerMainStart -lt 0) {
        $qualityContractFailures += "runner main entry point was not found"
    }
    else {
        $runnerMainSource = $runnerSource.Substring($runnerMainStart)
        $runPlanIndex = $runnerMainSource.IndexOf('$records = @(New-PerfGateRunPlan')
        $runRecordAssertIndex = $runnerMainSource.IndexOf('Assert-PerfGateRunRecords -Records $records')
        $reportSideEffectIndex = $runnerMainSource.IndexOf('New-Item -ItemType Directory -Force -Path $reportRoot')
        $buildSideEffectIndex = $runnerMainSource.IndexOf('Invoke-BatchCommand')
        if ($runPlanIndex -lt 0 -or $runRecordAssertIndex -lt $runPlanIndex -or
            ($reportSideEffectIndex -ge 0 -and $runRecordAssertIndex -gt $reportSideEffectIndex) -or
            ($buildSideEffectIndex -ge 0 -and $runRecordAssertIndex -gt $buildSideEffectIndex)) {
            $qualityContractFailures += "run plan validation does not precede report and build side effects"
        }
    }

    $emptyBlessBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $emptyBlessBaseline -Profile "Standard" -Configuration "Debug" -Records @() -ReportRoot "self-test"
    } "at least one run")) {
        $qualityContractFailures += "zero-run baseline bless was accepted"
    }

    $unsafeBlessRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "unsafe.json" "unsafe.out" "unsafe.err"
    $unsafeBlessRecord.status = "PASS"
    if ($null -eq $unsafeBlessRecord.PSObject.Properties["tree_termination_confirmed"]) {
        $unsafeBlessRecord | Add-Member -MemberType NoteProperty -Name "tree_termination_confirmed" -Value $false
        $qualityContractFailures += "run record does not track tree termination confirmation"
    }
    else {
        $unsafeBlessRecord.tree_termination_confirmed = $false
    }
    $unsafeBlessBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $unsafeBlessBaseline -Profile "Standard" -Configuration "Debug" -Records @($unsafeBlessRecord) -ReportRoot "self-test"
    } "unconfirmed process-tree termination")) {
        $qualityContractFailures += "unconfirmed process-tree termination could still bless a baseline"
    }

    $unsafeJobBlessRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "unsafe-job.json" "unsafe-job.out" "unsafe-job.err"
    $unsafeJobBlessRecord.status = "PASS"
    $unsafeJobBlessRecord.tree_termination_confirmed = $true
    $unsafeJobBlessBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $unsafeJobBlessBaseline -Profile "Standard" -Configuration "Debug" -Records @($unsafeJobBlessRecord) -ReportRoot "self-test"
    } "unconfirmed Job Object cleanup")) {
        $qualityContractFailures += "unconfirmed Job Object cleanup could still bless a baseline"
    }

    if ($qualityContractFailures.Count -gt 0) {
        throw ("Task9 quality contract RED: {0}" -f ($qualityContractFailures -join "; "))
    }

    $profileConfig = ConvertFrom-Json @'
{
  "warn_thresholds": {
    "cpu_frame_time_avg_percent": 10,
    "cpu_frame_time_p95_percent": 15,
    "cpu_frame_time_p99_percent": 25,
    "private_bytes_peak_percent": 15,
    "engine_heap_peak_percent": 15,
    "draw_call_count_percent": 10
  }
}
'@
    $baseline = ConvertFrom-Json @'
{
  "baselines": {
    "Standard": {
      "Debug": {
        "Sandbox": {
          "Vulkan": {
            "cpu_frame_time_avg_ms": 1.0,
            "cpu_frame_time_p95_ms": 2.0,
            "cpu_frame_time_p99_ms": 3.0,
            "process_private_bytes_peak_mb": 100.0,
            "engine_heap_peak_mb": 10.0,
            "draw_calls_avg": 50.0
          }
        }
      }
    }
  }
}
'@
    $record = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $record.status = "PASS"
    $record.root_exited = $true
    $record.tree_termination_confirmed = $true
    $record.job_cleanup_confirmed = $true
    $record.cpu_frame_time_avg_ms = 1.12
    $record.cpu_frame_time_p95_ms = 2.10
    $record.cpu_frame_time_p99_ms = 3.10
    $record.process_private_bytes_peak_mb = 110.0
    $record.engine_heap_peak_mb = 10.5
    $record.draw_calls_avg = 60.0

    Compare-RecordToBaseline -Record $record -Baseline $baseline -ProfileConfig $profileConfig -Profile "Standard" -Configuration "Debug"
    if ($record.status -ne "WARN") {
        throw "Expected baseline comparison to set WARN, got '$($record.status)'."
    }
    if ($record.cpu_frame_time_avg_delta -ne "+12.0%") {
        throw "Expected CPU average delta '+12.0%', got '$($record.cpu_frame_time_avg_delta)'."
    }
    if ($record.draw_calls_delta -ne "+20.0%") {
        throw "Expected draw call delta '+20.0%', got '$($record.draw_calls_delta)'."
    }
    if (@($record.warnings).Count -ne 2) {
        throw "Expected two baseline warnings, got $(@($record.warnings).Count)."
    }

    $emptyBaseline = ConvertFrom-Json '{ "baselines": {} }'
    Update-BaselinesFromRecords -Baseline $emptyBaseline -Profile "Standard" -Configuration "Debug" -Records @($record) -ReportRoot "Intermediate/test-reports/perf-gate/self-test"
    $blessed = Get-BaselineEntry -Baseline $emptyBaseline -Profile "Standard" -Configuration "Debug" -Target "Sandbox" -Backend "Vulkan"
    if ($null -eq $blessed) {
        throw "Blessed baseline entry was not created."
    }
    if ([double]$blessed.cpu_frame_time_avg_ms -ne 1.12) {
        throw "Blessed CPU average was not preserved."
    }

    Write-Host "RunPerfGate self-test PASS"
}

if ($SelfTest) {
    Invoke-RunPerfGateSelfTest
    exit 0
}

$repoRoot = Get-RepoRoot
$baselinePath = if ([string]::IsNullOrWhiteSpace($BaselinePath)) { Join-Path $repoRoot "tools/perf/perf_gate_baselines.json" } else { $BaselinePath }
$profilesPath = Join-Path $repoRoot "tools/perf/perf_gate_profiles.json"
$profileCatalog = Import-PerfGateProfileCatalog -ProfilesPath $profilesPath -BaselinePath $baselinePath
$baseline = $profileCatalog.baseline
$profileConfig = Get-PerfGateProfileConfig -Catalog $profileCatalog -Profile $Profile
$Configuration = Resolve-PerfGateConfiguration -ProfileConfig $profileConfig -RequestedConfiguration $Configuration
Assert-PerfGateOptions -BlessBaseline ([bool]$BlessBaseline) -DryRun ([bool]$DryRun) -TelemetryMode $TelemetryMode

$timestamp = "{0}-{1}-{2}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"), $PID, [Guid]::NewGuid().ToString("N").Substring(0, 8)
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/perf-gate/$timestamp"
$buildLogRoot = Join-Path $reportRoot "build"

$records = @(New-PerfGateRunPlan `
    -RepoRoot $repoRoot `
    -ReportRoot $reportRoot `
    -Profile $Profile `
    -ProfileConfig $profileConfig `
    -Configuration $Configuration `
    -TelemetryMode $TelemetryMode)
Assert-PerfGateRunRecords -Records $records

$buildCommands = @(Get-PerfGateBuildCommands -ProfileConfig $profileConfig -Configuration $Configuration)
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null
if ($DryRun -and -not $SkipBuild) {
    foreach ($buildCommand in $buildCommands) {
        Write-Host "BUILD_PLAN: $($buildCommand.command_line)"
    }
}
if (-not $SkipBuild -and -not $DryRun) {
    foreach ($buildCommand in $buildCommands) {
        Invoke-BatchCommand `
            -RepoRoot $repoRoot `
            -CommandLine $buildCommand.command_line `
            -LogPath (Join-Path $buildLogRoot $buildCommand.log_name)
    }
}

for ($recordIndex = 0; $recordIndex -lt $records.Count; ++$recordIndex) {
    $record = $records[$recordIndex]
    if ($DryRun) {
        $record.status = "DRY_RUN"
        Write-Host "DRY_RUN: $($record.command_line)"
        continue
    }

    if (-not (Test-Path -LiteralPath $record.executable)) {
        Add-Failure $record "Missing executable: $($record.executable)"
        continue
    }

    $runStart = Get-Date
    $matrixMayContinue = Invoke-GateProcess `
        -Record $record `
        -RunDirectory (Split-Path -Parent $record.executable) `
        -Arguments $record.arguments `
        -TimeoutSeconds ([double]$profileConfig.timeout_seconds)

    $runLogs = Get-RunLogFiles -RepoRoot $repoRoot -Since $runStart
    Test-LogForDiagnostics -Record $record -LogFiles $runLogs
    if (-not $matrixMayContinue) {
        Set-RemainingRunRecordsAborted `
            -Records $records `
            -StartIndex ($recordIndex + 1) `
            -Reason "the prior Job Object lifecycle could not be confirmed safe"
        break
    }
    if ($record.status -ne "FAIL") {
        Test-Telemetry -Record $record -ProfileConfig $profileConfig -Baseline $baseline -Profile $Profile -Configuration $Configuration
    }
}

$failedRecords = @($records | Where-Object { $_.status -eq "FAIL" })
$warnRecords = @($records | Where-Object { $_.status -eq "WARN" })
$dryRunRecords = @($records | Where-Object { $_.status -eq "DRY_RUN" })
$overall = "PASS"
if ($failedRecords.Count -gt 0) {
    $overall = "FAIL"
}
elseif ($warnRecords.Count -gt 0) {
    $overall = "WARN"
}
elseif ($dryRunRecords.Count -gt 0) {
    $overall = "DRY_RUN"
}

$summary = [PSCustomObject]@{
    schema_version = 2
    profile = $Profile
    profile_path = $profilesPath
    profile_source = (Get-PerfGateProfileSource -Catalog $profileCatalog -Profile $Profile)
    configuration = $Configuration
    telemetry_mode = $TelemetryMode
    gpu_baseline_comparable = ($TelemetryMode -ne "Off")
    status = $overall
    baseline_path = $baselinePath
    baseline_blessed = $false
    report_root = $reportRoot
    runs = $records
}

if ($BlessBaseline -and $overall -ne "FAIL" -and $overall -ne "DRY_RUN") {
    Update-BaselinesFromRecords -Baseline $baseline -Profile $Profile -Configuration $Configuration -Records $records -ReportRoot $reportRoot
    $baseline | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
    $summary.baseline_blessed = $true
}
$summary | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# AshEngine Perf Gate Summary"
$markdown += ""
$markdown += "Status: $overall"
$markdown += ""
$markdown += "Profile source: $($summary.profile_source)"
$markdown += "Telemetry mode: $TelemetryMode"
$markdown += "GPU baseline comparable: $($summary.gpu_baseline_comparable)"
$markdown += ""
$markdown += "Baseline: $baselinePath"
if ($BlessBaseline) {
    $markdown += "Baseline blessed: $($summary.baseline_blessed)"
}
$markdown += ""
$summaryRows = New-Object 'System.Collections.Generic.List[object[]]'
foreach ($record in $records) {
    $failureText = (@($record.failures) -join "; ")
    $warningText = (@($record.warnings) -join "; ")
    $extentText = if ($null -eq $record.actual_width -or $null -eq $record.actual_height) {
        "n/a"
    }
    else {
        "{0}x{1} stable={2}" -f $record.actual_width, $record.actual_height, $record.extent_stable
    }
    $summaryRows.Add([object[]]@(
        $record.target,
        $record.backend,
        $record.status,
        $record.baseline_status,
        $record.frames_sampled,
        [Math]::Round([double]$record.cpu_frame_time_avg_ms, 4),
        $record.cpu_frame_time_avg_delta,
        [Math]::Round([double]$record.cpu_frame_time_p95_ms, 4),
        $record.cpu_frame_time_p95_delta,
        $record.cpu_frame_time_p99_delta,
        [Math]::Round([double]$record.process_private_bytes_peak_mb, 2),
        $record.process_private_bytes_delta,
        [Math]::Round([double]$record.engine_heap_peak_mb, 2),
        $record.engine_heap_delta,
        $record.draw_calls_delta,
        (Format-OptionalNumber $record.gpu_frame_avg_ms 4),
        (Format-OptionalNumber $record.gpu_frame_p95_ms 4),
        (Format-OptionalNumber $record.gpu_coverage 4),
        $record.gpu_adapter,
        $record.gpu_driver,
        $extentText,
        $record.validation,
        $record.vsync,
        $failureText,
        $warningText
    )) | Out-Null
}
$markdown += New-MarkdownTable `
    -Headers @("Target", "Backend", "Status", "Baseline", "Frames", "CPU Avg ms", "CPU Avg delta", "CPU P95 ms", "CPU P95 delta", "CPU P99 delta", "Private MB", "Private delta", "Heap MB", "Heap delta", "Draw delta", "GPU Avg ms", "GPU P95 ms", "GPU coverage", "Adapter", "Driver", "Actual extent", "Validation", "VSync", "Failures", "Warnings") `
    -Alignments @("Left", "Left", "Left", "Left", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Left", "Left", "Left", "Left", "Left", "Left", "Left") `
    -Rows $summaryRows
$markdown | Set-Content -LiteralPath (Join-Path $reportRoot "summary.md") -Encoding UTF8

Write-Host "Perf gate report: $reportRoot"
Write-Host "Status: $overall"
if ($summary.baseline_blessed) {
    Write-Host "Baseline updated: $baselinePath"
}
if ($overall -eq "FAIL") {
    exit 1
}
exit 0
