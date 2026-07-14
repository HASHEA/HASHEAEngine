[CmdletBinding()]
param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [string]$BaselinePath = "",
    [switch]$SkipBuild,
    [switch]$DryRun,
    [switch]$BlessBaseline,
    [string]$BlessBaselineFromReport = "",
    [string]$ExpectedReportSha256 = "",
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
        os_build = "n/a"
        source_sha = "n/a"
        workload_fingerprint = ""
        workload = $null
        baseline_identity_required = $false
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
        [string]$TelemetryMode,
        [string]$BlessBaselineFromReport = "",
        [string]$ExpectedReportSha256 = ""
    )

    if ($BlessBaseline -and $DryRun) {
        throw "-BlessBaseline cannot be used with -DryRun."
    }
    if ($BlessBaseline -and $TelemetryMode -eq "Off") {
        throw "-TelemetryMode Off cannot be used with -BlessBaseline."
    }
    $hasImportPath = -not [string]::IsNullOrWhiteSpace($BlessBaselineFromReport)
    $hasImportHash = -not [string]::IsNullOrWhiteSpace($ExpectedReportSha256)
    if ($hasImportPath -and -not $hasImportHash) {
        throw "-ExpectedReportSha256 is required with -BlessBaselineFromReport."
    }
    if ($hasImportHash -and -not $hasImportPath) {
        throw "-BlessBaselineFromReport is required with -ExpectedReportSha256."
    }
    if ($hasImportPath -and $BlessBaseline) {
        throw "-BlessBaselineFromReport cannot be used with -BlessBaseline."
    }
    if ($hasImportPath -and $DryRun) {
        throw "-BlessBaselineFromReport cannot be used with -DryRun."
    }
    if ($hasImportPath -and $TelemetryMode -eq "Off") {
        throw "-BlessBaselineFromReport requires -TelemetryMode Profile."
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
        [AllowNull()][object]$ThresholdPercent,
        [double]$AbsoluteFloor = 0.0
    )

    if ($null -eq $BaselineValue) {
        return $null
    }

    $baselineDouble = [double]$BaselineValue
    $deltaPercent = $null
    $status = "PASS"
    $relativeThreshold = if ($null -eq $ThresholdPercent) { 0.0 } else { ([Math]::Abs($baselineDouble) * [double]$ThresholdPercent) / 100.0 }
    $allowedIncrease = [Math]::Max($relativeThreshold, $AbsoluteFloor)
    $actualIncrease = $Current - $baselineDouble
    if ([Math]::Abs($baselineDouble) -gt [double]::Epsilon) {
        $deltaPercent = (($Current - $baselineDouble) / $baselineDouble) * 100.0
        if ($actualIncrease -gt $allowedIncrease) {
            $status = "WARN"
        }
    }
    elseif ([Math]::Abs($Current) -le [double]::Epsilon) {
        $deltaPercent = 0.0
    }
    elseif ($Current -gt 0.0) {
        if ($actualIncrease -gt $allowedIncrease) {
            $status = "WARN"
        }
    }

    return [PSCustomObject]@{
        metric = $Metric
        label = $Label
        current = $Current
        baseline = $baselineDouble
        delta_percent = $deltaPercent
        delta_text = Format-DeltaPercent $deltaPercent
        threshold_percent = $ThresholdPercent
        absolute_floor = $AbsoluteFloor
        allowed_increase = $allowedIncrease
        actual_increase = $actualIncrease
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
        Add-Warning $Record ("Baseline regression: {0} {1} (+{2:N4}) exceeded allowed increase {3:N4}" -f $Delta.label, $Delta.delta_text, [double]$Delta.actual_increase, [double]$Delta.allowed_increase)
    }
}

function Add-ConfiguredBaselineDelta {
    param(
        [object]$Record,
        [string]$Metric,
        [string]$Label,
        [object]$Current,
        [object]$BaselineValue,
        [object]$Threshold
    )

    $relativePercent = Get-ProfileProperty $Threshold "relative_percent"
    $absoluteFloor = Get-ProfileProperty $Threshold "absolute_floor"
    if ($null -eq $absoluteFloor) { $absoluteFloor = 0.0 }
    Add-BaselineDelta $Record (New-BaselineDelta `
        -Metric $Metric `
        -Label $Label `
        -Current ([double]$Current) `
        -BaselineValue $BaselineValue `
        -ThresholdPercent $relativePercent `
        -AbsoluteFloor ([double]$absoluteFloor))
}

function Get-RequiredComparisonDouble {
    param(
        [object]$Record,
        [AllowNull()][object]$Value,
        [string]$MetricLabel,
        [ValidateSet("baseline", "current")]
        [string]$Side
    )

    $validated = ConvertTo-PerfGateFiniteDouble $Value
    if ($null -eq $validated -or $validated -lt 0.0) {
        Add-Failure $Record "Required $MetricLabel $Side value must be a finite non-negative number"
        return $null
    }
    return [double]$validated
}

function Get-PerfGateSha256Text {
    param([string]$Text)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return (($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-PerfGateFileSha256 {
    param([string]$Path)

    $stream = $null
    $sha256 = $null
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read)
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        return (($sha256.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally {
        if ($null -ne $sha256) {
            $sha256.Dispose()
        }
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

function Write-PerfGateJsonFile {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$InputObject,

        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [ValidateRange(1, 100)]
        [int]$Depth = 16
    )

    $json = ConvertTo-Json -InputObject $InputObject -Depth $Depth
    $json = $json.Replace("`r`n", "`n").Replace("`r", "`n")
    $json = $json.TrimEnd([char[]]@("`n")) + "`n"
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($LiteralPath, $json, $utf8WithoutBom)
}

function Write-PerfGateJsonFileAtomically {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$InputObject,

        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [ValidateRange(1, 100)]
        [int]$Depth = 16
    )

    $destinationPath = [System.IO.Path]::GetFullPath($LiteralPath)
    $destinationDirectory = [System.IO.Path]::GetDirectoryName($destinationPath)
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        throw "PerfGate JSON destination directory does not exist: $destinationDirectory"
    }
    $temporaryPath = Join-Path $destinationDirectory (".{0}.{1}.tmp" -f [System.IO.Path]::GetFileName($destinationPath), [Guid]::NewGuid().ToString("N"))
    $backupPath = Join-Path $destinationDirectory (".{0}.{1}.bak" -f [System.IO.Path]::GetFileName($destinationPath), [Guid]::NewGuid().ToString("N"))
    try {
        Write-PerfGateJsonFile -InputObject $InputObject -LiteralPath $temporaryPath -Depth $Depth
        if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
            [System.IO.File]::Replace($temporaryPath, $destinationPath, $backupPath, $true)
        }
        else {
            [System.IO.File]::Move($temporaryPath, $destinationPath)
        }
    }
    finally {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    }
}

function New-PerfGateWorkloadIdentity {
    param(
        [object]$ProfileConfig,
        [string]$RepoRoot
    )

    $sceneValue = Get-ProfileProperty $ProfileConfig "scene"
    if ($null -eq $sceneValue -or [string]::IsNullOrWhiteSpace([string]$sceneValue)) {
        throw "Comparable perf profile must define scene."
    }

    $rootFullPath = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    $sceneInput = [string]$sceneValue
    $sceneFullPath = if ([System.IO.Path]::IsPathRooted($sceneInput)) {
        [System.IO.Path]::GetFullPath($sceneInput)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $rootFullPath $sceneInput))
    }
    $rootPrefix = $rootFullPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $sceneFullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Comparable perf profile scene must remain inside the repository root."
    }
    if (-not (Test-Path -LiteralPath $sceneFullPath -PathType Leaf)) {
        throw "Comparable perf profile scene does not exist: $sceneFullPath"
    }
    $normalizedScene = $sceneFullPath.Substring($rootPrefix.Length).Replace('\', '/').ToLowerInvariant()
    $sceneSha256 = Get-PerfGateFileSha256 -Path $sceneFullPath

    $widthValue = Get-ProfileProperty $ProfileConfig "window_width"
    $heightValue = Get-ProfileProperty $ProfileConfig "window_height"
    if ($widthValue -isnot [System.Int32] -and $widthValue -isnot [System.Int64]) {
        throw "Comparable perf profile window_width must be an integer."
    }
    if ($heightValue -isnot [System.Int32] -and $heightValue -isnot [System.Int64]) {
        throw "Comparable perf profile window_height must be an integer."
    }
    $width = [int64]$widthValue
    $height = [int64]$heightValue
    if ($width -le 0 -or $height -le 0) {
        throw "Comparable perf profile extent must be positive."
    }

    $fixedCamera = Get-ProfileProperty $ProfileConfig "fixed_camera"
    $vsync = Get-ProfileProperty $ProfileConfig "vsync"
    $validation = Get-ProfileProperty $ProfileConfig "validation"
    if ($fixedCamera -isnot [bool] -or $vsync -isnot [bool] -or $validation -isnot [bool]) {
        throw "Comparable perf profile fixed_camera, vsync, and validation must be booleans."
    }
    $frameCapValue = Get-ProfileProperty $ProfileConfig "frame_cap"
    if ($null -eq $frameCapValue -or [string]::IsNullOrWhiteSpace([string]$frameCapValue)) {
        throw "Comparable perf profile frame_cap must be non-blank."
    }

    $metricValues = @(Get-ProfileProperty $ProfileConfig "required_gpu_metrics")
    if ($metricValues.Count -eq 0) {
        throw "Comparable perf profile must define required_gpu_metrics."
    }
    $metricSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::Ordinal)
    foreach ($metricValue in $metricValues) {
        $metricName = [string]$metricValue
        if ([string]::IsNullOrWhiteSpace($metricName) -or -not $metricSet.Add($metricName)) {
            throw "Comparable perf profile required_gpu_metrics must be non-blank and unique."
        }
    }
    $sortedMetrics = [string[]]@($metricSet)
    [Array]::Sort($sortedMetrics, [System.StringComparer]::Ordinal)

    $workload = [PSCustomObject][ordered]@{
        scene = $normalizedScene
        scene_sha256 = $sceneSha256
        extent = [PSCustomObject][ordered]@{ width = $width; height = $height }
        fixed_camera = [bool]$fixedCamera
        vsync = [bool]$vsync
        validation = [bool]$validation
        frame_cap = ([string]$frameCapValue).Trim().ToLowerInvariant()
        required_gpu_metrics = $sortedMetrics
    }
    $canonical = $workload | ConvertTo-Json -Depth 8 -Compress
    return [PSCustomObject]@{
        fingerprint = Get-PerfGateSha256Text $canonical
        workload = $workload
    }
}

function Get-PerfGateSourceSha {
    param([string]$RepoRoot)

    $shaOutput = @(& git -C $RepoRoot rev-parse HEAD 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to resolve PerfGate source SHA: $($shaOutput -join ' ')"
    }
    $sha = ([string]($shaOutput | Select-Object -First 1)).Trim().ToLowerInvariant()
    if ($sha -notmatch '^[0-9a-f]{40,64}$') {
        throw "PerfGate source SHA was not a full Git object id: '$sha'"
    }
    return $sha
}

function Get-PerfGateOsBuild {
    try {
        $windowsVersion = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
        $build = [string]$windowsVersion.CurrentBuildNumber
        $revision = [string]$windowsVersion.UBR
        if (-not [string]::IsNullOrWhiteSpace($build)) {
            if (-not [string]::IsNullOrWhiteSpace($revision)) {
                return "$build.$revision"
            }
            return $build
        }
    }
    catch {
    }

    $version = [System.Environment]::OSVersion.Version
    return "$($version.Major).$($version.Minor).$($version.Build).$($version.Revision)"
}

function Test-PerfGateFixedGpuComparisonProfile {
    param([object]$ProfileConfig)

    return [string](Get-ProfileProperty $ProfileConfig "gpu_timing") -eq "required"
}

function Set-PerfGateRunIdentity {
    param(
        [AllowEmptyCollection()][object[]]$Records,
        [object]$ProfileConfig,
        [string]$RepoRoot,
        [string]$SourceSha = "",
        [string]$OsBuild = ""
    )

    if ([string]::IsNullOrWhiteSpace($SourceSha)) {
        $SourceSha = Get-PerfGateSourceSha -RepoRoot $RepoRoot
    }
    $SourceSha = $SourceSha.Trim().ToLowerInvariant()
    if ($SourceSha -notmatch '^[0-9a-f]{40,64}$') {
        throw "PerfGate source SHA was not a full Git object id: '$SourceSha'"
    }
    if ([string]::IsNullOrWhiteSpace($OsBuild)) {
        $OsBuild = Get-PerfGateOsBuild
    }
    $OsBuild = $OsBuild.Trim()
    if ([string]::IsNullOrWhiteSpace($OsBuild)) {
        throw "PerfGate OS build attribution was blank."
    }

    Assert-PerfGateComparisonProfileContract -ProfileConfig $ProfileConfig
    $identityRequired = Test-PerfGateFixedGpuComparisonProfile -ProfileConfig $ProfileConfig
    $workloadIdentity = if ($identityRequired) {
        New-PerfGateWorkloadIdentity -ProfileConfig $ProfileConfig -RepoRoot $RepoRoot
    }
    else {
        $null
    }
    foreach ($record in @($Records)) {
        $record.source_sha = $SourceSha
        $record.os_build = $OsBuild
        $record.baseline_identity_required = $identityRequired
        if ($identityRequired) {
            $record.workload_fingerprint = [string]$workloadIdentity.fingerprint
            $record.workload = $workloadIdentity.workload
        }
    }
}

function New-PerfGateSpreadMetric {
    param(
        [double[]]$Values,
        [double]$RelativePercent,
        [double]$AbsoluteFloor
    )

    $minimum = [double](($Values | Measure-Object -Minimum).Minimum)
    $maximum = [double](($Values | Measure-Object -Maximum).Maximum)
    $spread = $maximum - $minimum
    $allowed = [Math]::Max(([Math]::Abs($minimum) * $RelativePercent) / 100.0, $AbsoluteFloor)
    return [PSCustomObject][ordered]@{
        minimum_ms = $minimum
        maximum_ms = $maximum
        spread_ms = $spread
        relative_percent = $RelativePercent
        absolute_floor_ms = $AbsoluteFloor
        allowed_spread_ms = $allowed
        status = if ($spread -gt $allowed) { "FAIL" } else { "PASS" }
    }
}

function Get-PerfGateThreeRunSpread {
    param([object[]]$Records)

    $groups = @($Records | Group-Object -Property target, backend)
    foreach ($group in $groups) {
        $runs = @($group.Group)
        $target = [string]$runs[0].target
        $backend = [string]$runs[0].backend
        if ($runs.Count -ne 3) {
            throw "Spread group '$target/$backend' must contain exactly three runs; got $($runs.Count)."
        }

        $metricValues = [ordered]@{}
        foreach ($metricName in @(
            "cpu_frame_time_avg_ms",
            "cpu_frame_time_p95_ms",
            "gpu_frame_avg_ms",
            "gpu_frame_p95_ms"
        )) {
            $values = @()
            foreach ($run in $runs) {
                $validated = ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $run $metricName)
                if ($null -eq $validated -or $validated -lt 0.0) {
                    throw "Spread group '$target/$backend' metric '$metricName' must contain finite non-negative values."
                }
                $values += [double]$validated
            }
            $relativePercent = 3.0
            $absoluteFloor = 0.15
            if ($metricName -eq "cpu_frame_time_p95_ms") {
                $relativePercent = 8.0
                $absoluteFloor = 0.50
            }
            elseif ($metricName -eq "gpu_frame_p95_ms") {
                $relativePercent = 5.0
                $absoluteFloor = 0.30
            }
            $metricValues[$metricName] = New-PerfGateSpreadMetric `
                -Values $values `
                -RelativePercent $relativePercent `
                -AbsoluteFloor $absoluteFloor
        }

        $status = if (@($metricValues.Values | Where-Object { $_.status -eq "FAIL" }).Count -gt 0) { "FAIL" } else { "PASS" }
        [PSCustomObject][ordered]@{
            target = $target
            backend = $backend
            run_count = $runs.Count
            status = $status
            cpu_frame_time_avg_ms = $metricValues["cpu_frame_time_avg_ms"]
            cpu_frame_time_p95_ms = $metricValues["cpu_frame_time_p95_ms"]
            gpu_frame_avg_ms = $metricValues["gpu_frame_avg_ms"]
            gpu_frame_p95_ms = $metricValues["gpu_frame_p95_ms"]
        }
    }
}

function Get-PerfGateComparisonProfileContractErrors {
    param([object]$ProfileConfig)

    $errors = @()
    $fixedGpuComparison = Test-PerfGateFixedGpuComparisonProfile -ProfileConfig $ProfileConfig
    $comparisonThresholds = Get-ProfileProperty $ProfileConfig "comparison_thresholds"
    if ($null -eq $comparisonThresholds) {
        if ($fixedGpuComparison) {
            $errors += "gpu_timing=required profile must define non-null comparison_thresholds"
        }
        return @($errors)
    }
    if (-not $fixedGpuComparison) {
        $errors += "comparison_thresholds may only be defined when gpu_timing=required"
    }

    foreach ($thresholdName in @(
        "cpu_frame_time_avg_ms",
        "cpu_frame_time_p95_ms",
        "cpu_frame_time_p99_ms",
        "process_private_bytes_peak_mb",
        "engine_heap_peak_mb",
        "draw_calls_avg",
        "gpu_frame_avg_ms",
        "gpu_frame_p95_ms"
    )) {
        $threshold = Get-ProfileProperty $comparisonThresholds $thresholdName
        $relative = if ($null -eq $threshold) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $threshold "relative_percent") }
        $absolute = if ($null -eq $threshold) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $threshold "absolute_floor") }
        if ($null -eq $threshold -or $null -eq $relative -or $relative -lt 0.0 -or $null -eq $absolute -or $absolute -lt 0.0) {
            $errors += "$thresholdName must define finite non-negative relative_percent and absolute_floor"
        }
    }

    $tiers = @(Get-ProfileProperty $comparisonThresholds "gpu_pass_tiers")
    if ($tiers.Count -ne 3) {
        $errors += "gpu_pass_tiers must define exactly three tiers"
    }
    else {
        $expectedMinimums = @(0.5, 0.1, 0.0)
        $validatedTiers = @()
        foreach ($tier in $tiers) {
            $minimum = ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $tier "minimum_baseline_avg_ms")
            if ($null -eq $minimum -or $minimum -lt 0.0) {
                $errors += "gpu_pass_tiers minimum_baseline_avg_ms values must be finite non-negative numbers"
                continue
            }
            $validatedTiers += [PSCustomObject]@{ minimum = [double]$minimum; tier = $tier }
        }
        $sortedTiers = @($validatedTiers | Sort-Object minimum -Descending)
        for ($tierIndex = 0; $tierIndex -lt $sortedTiers.Count; ++$tierIndex) {
            $tier = $sortedTiers[$tierIndex].tier
            $minimum = [double]$sortedTiers[$tierIndex].minimum
            if ([Math]::Abs($minimum - $expectedMinimums[$tierIndex]) -gt 0.0000001) {
                $errors += "gpu_pass_tiers minimum_baseline_avg_ms values must be 0.5, 0.1, and 0.0"
            }
            foreach ($statName in @("avg", "p95")) {
                $threshold = Get-ProfileProperty $tier $statName
                $absolute = if ($null -eq $threshold) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $threshold "absolute_floor") }
                $relativeRaw = if ($null -eq $threshold) { $null } else { Get-ProfileProperty $threshold "relative_percent" }
                $relative = ConvertTo-PerfGateFiniteDouble $relativeRaw
                $absoluteOnly = $tierIndex -eq 2
                $relativeIsValid = if ($absoluteOnly) { $null -eq $relativeRaw } else { $null -ne $relative -and $relative -ge 0.0 }
                if ($null -eq $threshold -or $null -eq $absolute -or $absolute -lt 0.0 -or -not $relativeIsValid) {
                    $errors += "gpu_pass_tiers[$tierIndex].$statName has an invalid threshold definition"
                }
            }
        }
    }

    return @($errors)
}

function Assert-PerfGateComparisonProfileContract {
    param([object]$ProfileConfig)

    $errors = @(Get-PerfGateComparisonProfileContractErrors -ProfileConfig $ProfileConfig)
    if ($errors.Count -gt 0) {
        throw ("Invalid fixed GPU comparison profile: {0}" -f ($errors -join "; "))
    }
}

function Test-PerfGateBaselineComparable {
    param(
        [object]$Record,
        [object]$BaselineEntry,
        [string]$Configuration
    )

    $attribution = Get-ProfileProperty $BaselineEntry "attribution"
    $mismatches = @()
    if ($null -eq $attribution) {
        $mismatches += "baseline attribution was missing"
    }
    else {
        foreach ($contract in @(
            [PSCustomObject]@{ label = "target"; current = [string]$Record.target; baseline = [string](Get-ProfileProperty $attribution "target"); ignore_case = $true },
            [PSCustomObject]@{ label = "backend"; current = [string]$Record.backend; baseline = [string](Get-ProfileProperty $attribution "backend"); ignore_case = $true },
            [PSCustomObject]@{ label = "configuration"; current = [string]$Configuration; baseline = [string](Get-ProfileProperty $attribution "configuration"); ignore_case = $true },
            [PSCustomObject]@{ label = "adapter"; current = [string]$Record.gpu_adapter; baseline = [string](Get-ProfileProperty $attribution "adapter"); ignore_case = $false },
            [PSCustomObject]@{ label = "driver"; current = [string]$Record.gpu_driver; baseline = [string](Get-ProfileProperty $attribution "driver"); ignore_case = $false },
            [PSCustomObject]@{ label = "OS build"; current = [string]$Record.os_build; baseline = [string](Get-ProfileProperty $attribution "os_build"); ignore_case = $false }
        )) {
            if ([string]::IsNullOrWhiteSpace($contract.current) -or [string]::IsNullOrWhiteSpace($contract.baseline)) {
                $mismatches += "$($contract.label) attribution was missing"
                continue
            }
            $comparison = if ($contract.ignore_case) { [System.StringComparison]::OrdinalIgnoreCase } else { [System.StringComparison]::Ordinal }
            if (-not [string]::Equals($contract.current, $contract.baseline, $comparison)) {
                $mismatches += "$($contract.label) mismatch ('$($contract.baseline)' baseline vs '$($contract.current)' current)"
            }
        }

        $baselineSourceSha = [string](Get-ProfileProperty $attribution "source_sha")
        if ([string]::IsNullOrWhiteSpace($baselineSourceSha) -or [string]::IsNullOrWhiteSpace([string]$Record.source_sha)) {
            $mismatches += "source SHA attribution was missing"
        }
    }

    $baselineFingerprint = [string](Get-ProfileProperty $BaselineEntry "workload_fingerprint")
    if ([string]::IsNullOrWhiteSpace($baselineFingerprint) -or [string]::IsNullOrWhiteSpace([string]$Record.workload_fingerprint)) {
        $mismatches += "workload fingerprint was missing"
    }
    elseif (-not [string]::Equals($baselineFingerprint, [string]$Record.workload_fingerprint, [System.StringComparison]::Ordinal)) {
        $mismatches += "workload fingerprint mismatch ('$baselineFingerprint' baseline vs '$($Record.workload_fingerprint)' current)"
    }
    if ($null -eq (Get-ProfileProperty $BaselineEntry "workload") -or $null -eq $Record.workload) {
        $mismatches += "readable workload attribution was missing"
    }

    if ($mismatches.Count -gt 0) {
        $Record.baseline_status = "NOT_COMPARABLE"
        Add-Failure $Record ("Baseline NOT_COMPARABLE: {0}" -f ($mismatches -join "; "))
        return $false
    }
    return $true
}

function Compare-RecordToBaseline {
    param(
        [object]$Record,
        [object]$Baseline,
        [object]$ProfileConfig,
        [string]$Profile,
        [string]$Configuration
    )

    $comparisonContractErrors = @(Get-PerfGateComparisonProfileContractErrors -ProfileConfig $ProfileConfig)
    if ($comparisonContractErrors.Count -gt 0) {
        Add-Failure $Record ("Invalid comparison profile contract: {0}" -f ($comparisonContractErrors -join "; "))
        return
    }
    $comparisonThresholds = Get-ProfileProperty $ProfileConfig "comparison_thresholds"

    $entry = Get-BaselineEntry -Baseline $Baseline -Profile $Profile -Configuration $Configuration -Target $Record.target -Backend $Record.backend
    if ($null -eq $entry) {
        $Record.baseline_status = "MISSING"
        return
    }

    if ($null -ne $comparisonThresholds -and -not [bool]$Record.gpu_baseline_comparable) {
        $Record.baseline_status = "NOT_COMPARED"
        return
    }

    $Record.baseline_status = "COMPARED"
    if ($null -ne $comparisonThresholds) {
        if (-not (Test-PerfGateBaselineComparable -Record $Record -BaselineEntry $entry -Configuration $Configuration)) {
            return
        }
        foreach ($metricContract in @(
            [PSCustomObject]@{ name = "cpu_frame_time_avg_ms"; label = "CPU Avg" },
            [PSCustomObject]@{ name = "cpu_frame_time_p95_ms"; label = "CPU P95" },
            [PSCustomObject]@{ name = "cpu_frame_time_p99_ms"; label = "CPU P99" },
            [PSCustomObject]@{ name = "process_private_bytes_peak_mb"; label = "Private MB" },
            [PSCustomObject]@{ name = "engine_heap_peak_mb"; label = "Heap MB" },
            [PSCustomObject]@{ name = "draw_calls_avg"; label = "Draw Calls" }
        )) {
            $currentValue = Get-RequiredComparisonDouble $Record (Get-ProfileProperty $Record $metricContract.name) $metricContract.label "current"
            $baselineValue = Get-RequiredComparisonDouble $Record (Get-ProfileProperty $entry $metricContract.name) $metricContract.label "baseline"
            if ($null -eq $currentValue -or $null -eq $baselineValue) { continue }
            Add-ConfiguredBaselineDelta `
                -Record $Record `
                -Metric $metricContract.name `
                -Label $metricContract.label `
                -Current $currentValue `
                -BaselineValue $baselineValue `
                -Threshold (Get-ProfileProperty $comparisonThresholds $metricContract.name)
        }

        $baselineGpuMetrics = Get-ProfileProperty $entry "gpu_metrics"
        $baselineFrame = Get-ProfileProperty $baselineGpuMetrics "GPU.Frame"
        $currentFrame = Get-ProfileProperty $Record.gpu_metric_summaries "GPU.Frame"
        if ($null -eq $baselineFrame) {
            Add-Failure $Record "Required GPU.Frame baseline metric was missing"
        }
        if ($null -eq $currentFrame) {
            Add-Failure $Record "Required GPU.Frame current metric was missing"
        }
        if ($null -ne $baselineFrame -and $null -ne $currentFrame) {
            foreach ($statContract in @(
                [PSCustomObject]@{ name = "avg"; threshold = "gpu_frame_avg_ms" },
                [PSCustomObject]@{ name = "p95"; threshold = "gpu_frame_p95_ms" }
            )) {
                $currentValue = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $currentFrame $statContract.name) "GPU.Frame current $($statContract.name)" "current"
                $baselineValue = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $baselineFrame $statContract.name) "GPU.Frame baseline $($statContract.name)" "baseline"
                if ($null -eq $currentValue -or $null -eq $baselineValue) { continue }
                Add-ConfiguredBaselineDelta $Record "gpu.GPU.Frame.$($statContract.name)" "GPU.Frame $($statContract.name)" $currentValue $baselineValue (Get-ProfileProperty $comparisonThresholds $statContract.threshold)
            }
        }
        $passTiers = @(Get-ProfileProperty $comparisonThresholds "gpu_pass_tiers" | Sort-Object { [double](Get-ProfileProperty $_ "minimum_baseline_avg_ms") } -Descending)
        foreach ($metricName in @($Record.required_gpu_metrics)) {
            if ([string]$metricName -eq "GPU.Frame") { continue }
            $baselineMetric = Get-ProfileProperty $baselineGpuMetrics ([string]$metricName)
            $currentMetric = Get-ProfileProperty $Record.gpu_metric_summaries ([string]$metricName)
            if ($null -eq $baselineMetric) {
                Add-Failure $Record "Required GPU metric '$metricName' baseline metric was missing"
                continue
            }
            if ($null -eq $currentMetric) {
                Add-Failure $Record "Required GPU metric '$metricName' current metric was missing"
                continue
            }

            $baselineAverage = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $baselineMetric "avg") "$metricName avg" "baseline"
            $currentAverage = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $currentMetric "avg") "$metricName avg" "current"
            $baselineP95 = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $baselineMetric "p95") "$metricName p95" "baseline"
            $currentP95 = Get-RequiredComparisonDouble $Record (Get-GpuMetricSummaryValue $currentMetric "p95") "$metricName p95" "current"
            if ($null -eq $baselineAverage -or $null -eq $currentAverage -or $null -eq $baselineP95 -or $null -eq $currentP95) { continue }
            $tier = @($passTiers | Where-Object { $baselineAverage -ge [double](Get-ProfileProperty $_ "minimum_baseline_avg_ms") } | Select-Object -First 1)
            if ($tier.Count -eq 0) { continue }
            $averageThreshold = Get-ProfileProperty $tier[0] "avg"
            $averageDelta = New-BaselineDelta `
                -Metric "gpu.$metricName.avg" `
                -Label "$metricName avg" `
                -Current $currentAverage `
                -BaselineValue $baselineAverage `
                -ThresholdPercent (Get-ProfileProperty $averageThreshold "relative_percent") `
                -AbsoluteFloor ([double](Get-ProfileProperty $averageThreshold "absolute_floor"))
            Add-BaselineDelta $Record $averageDelta

            $p95Threshold = Get-ProfileProperty $tier[0] "p95"
            $p95Delta = New-BaselineDelta `
                -Metric "gpu.$metricName.p95" `
                -Label "$metricName p95" `
                -Current $currentP95 `
                -BaselineValue $baselineP95 `
                -ThresholdPercent (Get-ProfileProperty $p95Threshold "relative_percent") `
                -AbsoluteFloor ([double](Get-ProfileProperty $p95Threshold "absolute_floor"))
            if ([double](Get-ProfileProperty $tier[0] "minimum_baseline_avg_ms") -eq 0.0) {
                $p95ThresholdExceeded = $p95Delta.status -eq "WARN"
                $averageCorroborated = $averageDelta.status -eq "WARN"
                Set-ObjectProperty $p95Delta "threshold_exceeded" $p95ThresholdExceeded
                Set-ObjectProperty $p95Delta "corroborated" ($p95ThresholdExceeded -and $averageCorroborated)
                if ($p95ThresholdExceeded -and -not $averageCorroborated) {
                    $p95Delta.status = "PASS"
                }
            }
            Add-BaselineDelta $Record $p95Delta
        }
        return
    }

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
        [string]$ReportRoot,
        [string]$SourceReportSha256 = ""
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

    if (-not [string]::IsNullOrWhiteSpace($SourceReportSha256)) {
        $entry | Add-Member -MemberType NoteProperty -Name "source_report_sha256" -Value $SourceReportSha256.ToLowerInvariant()
    }

    if ($Record.gpu_baseline_comparable -and
        @($Record.required_gpu_metrics).Count -gt 0 -and
        $null -ne $Record.gpu_metric_summaries) {
        $identityRequiredProperty = $Record.PSObject.Properties["baseline_identity_required"]
        $identityRequired = $null -ne $identityRequiredProperty -and [bool]$identityRequiredProperty.Value
        if ($identityRequired) {
            foreach ($identityContract in @(
                [PSCustomObject]@{ label = "adapter"; value = [string]$Record.gpu_adapter },
                [PSCustomObject]@{ label = "driver"; value = [string]$Record.gpu_driver },
                [PSCustomObject]@{ label = "OS build"; value = [string]$Record.os_build },
                [PSCustomObject]@{ label = "source SHA"; value = [string]$Record.source_sha },
                [PSCustomObject]@{ label = "workload fingerprint"; value = [string]$Record.workload_fingerprint }
            )) {
                if ([string]::IsNullOrWhiteSpace($identityContract.value) -or $identityContract.value -eq "n/a") {
                    throw "Cannot bless comparable GPU baseline: $($identityContract.label) attribution was missing."
                }
            }
            if ($null -eq $Record.workload) {
                throw "Cannot bless comparable GPU baseline: readable workload attribution was missing."
            }
            $entry | Add-Member -MemberType NoteProperty -Name "attribution" -Value ([PSCustomObject][ordered]@{
                target = [string]$Record.target
                backend = [string]$Record.backend
                configuration = [string]$Record.configuration
                adapter = [string]$Record.gpu_adapter
                driver = [string]$Record.gpu_driver
                os_build = [string]$Record.os_build
                source_sha = [string]$Record.source_sha
            })
            $entry | Add-Member -MemberType NoteProperty -Name "workload_fingerprint" -Value ([string]$Record.workload_fingerprint)
            $entry | Add-Member -MemberType NoteProperty -Name "workload" -Value $Record.workload
        }

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
        [string]$ReportRoot,
        [string]$SourceReportSha256 = ""
    )

    Assert-PerfGateRunRecords -Records $Records
    foreach ($record in $Records) {
        $terminationProperty = $record.PSObject.Properties["tree_termination_confirmed"]
        if ($null -ne $terminationProperty -and -not [bool]$terminationProperty.Value) {
            throw "Cannot bless a baseline after unconfirmed process-tree termination."
        }
        $jobCleanupProperty = $record.PSObject.Properties["job_cleanup_confirmed"]
        if ($null -eq $jobCleanupProperty) {
            throw "Cannot bless a baseline with missing Job Object cleanup proof."
        }
        if (-not [bool]$jobCleanupProperty.Value) {
            throw "Cannot bless a baseline after unconfirmed Job Object cleanup."
        }
        if ([string]$record.status -notin @("PASS", "WARN")) {
            throw "Cannot bless a baseline unless every run reached terminal PASS or WARN status."
        }
    }

    $baselinesNode = Ensure-ObjectProperty $Baseline "baselines"
    $profileNode = Ensure-ObjectProperty $baselinesNode $Profile
    $configurationNode = Ensure-ObjectProperty $profileNode $Configuration

    foreach ($record in $Records) {
        $targetNode = Ensure-ObjectProperty $configurationNode $record.target
        Set-ObjectProperty $targetNode $record.backend (New-BaselineEntry -Record $record -ReportRoot $ReportRoot -SourceReportSha256 $SourceReportSha256)
    }
}

function Import-PerfGateBaselineFromReport {
    param(
        [object]$Baseline,
        [string]$BaselinePath,
        [string]$Profile,
        [string]$Configuration,
        [object]$ProfileConfig,
        [string]$RepoRoot,
        [string]$ReportPath,
        [string]$ExpectedReportSha256,
        [string]$CurrentSourceSha
    )

    if ($ExpectedReportSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Expected report SHA-256 must contain exactly 64 hexadecimal characters."
    }
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
        throw "Approved perf report does not exist: $ReportPath"
    }
    if ($CurrentSourceSha -notmatch '^[0-9a-fA-F]{40,64}$') {
        throw "Current source SHA must be a full Git object id."
    }

    $repoFullPath = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    $reportFullPath = [System.IO.Path]::GetFullPath($ReportPath)
    $approvedRoot = [System.IO.Path]::GetFullPath((Join-Path $repoFullPath "Intermediate/test-reports/perf-gate")).TrimEnd('\', '/')
    $approvedPrefix = $approvedRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $reportFullPath.StartsWith($approvedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Approved perf report must be inside '$approvedRoot'."
    }

    $reportBytes = [System.IO.File]::ReadAllBytes($reportFullPath)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $actualReportSha256 = (($sha256.ComputeHash($reportBytes) | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally {
        $sha256.Dispose()
    }
    if (-not [string]::Equals($actualReportSha256, $ExpectedReportSha256, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Approved perf report SHA-256 mismatch: expected '$ExpectedReportSha256', actual '$actualReportSha256'."
    }

    try {
        $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
        $summary = $strictUtf8.GetString($reportBytes) | ConvertFrom-Json
    }
    catch {
        throw "Approved perf report is not valid JSON: $($_.Exception.Message)"
    }

    if ([int](Get-ProfileProperty $summary "schema_version") -ne 2) {
        throw "Approved perf report must use schema_version 2."
    }
    if (-not [string]::Equals([string](Get-ProfileProperty $summary "profile"), $Profile, [System.StringComparison]::Ordinal)) {
        throw "Approved perf report profile does not match '$Profile'."
    }
    if (-not [string]::Equals([string](Get-ProfileProperty $summary "configuration"), $Configuration, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Approved perf report configuration does not match '$Configuration'."
    }
    if ([string](Get-ProfileProperty $summary "telemetry_mode") -ne "Profile" -or
        -not [bool](Get-ProfileProperty $summary "gpu_baseline_comparable") -or
        [string](Get-ProfileProperty $summary "status") -ne "PASS" -or
        [bool](Get-ProfileProperty $summary "baseline_blessed")) {
        throw "Approved perf report must be an unblessed PASS with comparable Profile telemetry."
    }

    $expectedBaselinePath = [System.IO.Path]::GetFullPath($BaselinePath)
    $reportedBaselinePath = [System.IO.Path]::GetFullPath([string](Get-ProfileProperty $summary "baseline_path"))
    if (-not [string]::Equals($reportedBaselinePath, $expectedBaselinePath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Approved perf report baseline path does not match the requested baseline."
    }

    Assert-PerfGateProfileMatrix -ProfileConfig $ProfileConfig
    Assert-PerfGateComparisonProfileContract -ProfileConfig $ProfileConfig
    $expectedIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $ProfileConfig -RepoRoot $RepoRoot
    $summaryReportRoot = [System.IO.Path]::GetFullPath([string](Get-ProfileProperty $summary "report_root")).TrimEnd('\', '/')
    $reportParent = [System.IO.Path]::GetDirectoryName($reportFullPath).TrimEnd('\', '/')
    if (-not [string]::Equals($summaryReportRoot, $reportParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Approved perf report report_root does not match its containing directory."
    }
    $records = @(Get-ProfileProperty $summary "runs")
    $expectedPairs = @{}
    foreach ($targetConfig in @($ProfileConfig.targets)) {
        $target = Get-PerfGateTargetName $targetConfig
        foreach ($backend in @($targetConfig.backends)) {
            $expectedPairs[("{0}|{1}" -f $target, $backend).ToLowerInvariant()] = $true
        }
    }
    if ($records.Count -ne $expectedPairs.Count) {
        throw "Approved perf report run matrix does not match the current profile."
    }

    $expectedMetricNames = [string[]]@(Get-ProfileProperty $ProfileConfig "required_gpu_metrics")
    [Array]::Sort($expectedMetricNames, [System.StringComparer]::Ordinal)
    $minimumCoverage = [double](Get-ProfileProperty $ProfileConfig "min_gpu_coverage")
    $expectedWidth = [int](Get-ProfileProperty $ProfileConfig "window_width")
    $expectedHeight = [int](Get-ProfileProperty $ProfileConfig "window_height")
    $expectedValidation = [bool](Get-ProfileProperty $ProfileConfig "validation")
    $expectedVsync = [bool](Get-ProfileProperty $ProfileConfig "vsync")
    $expectedFixedCamera = [bool](Get-ProfileProperty $ProfileConfig "fixed_camera")
    $expectedFrameCap = [string](Get-ProfileProperty $ProfileConfig "frame_cap")
    $seenPairs = @{}
    foreach ($record in $records) {
        $pair = ("{0}|{1}" -f [string]$record.target, [string]$record.backend).ToLowerInvariant()
        if (-not $expectedPairs.ContainsKey($pair) -or $seenPairs.ContainsKey($pair)) {
            throw "Approved perf report run matrix contains an unexpected or duplicate '$pair' entry."
        }
        $seenPairs[$pair] = $true
        if ([string](Get-ProfileProperty $record "status") -ne "PASS") {
            throw "Approved perf report runs must all have PASS status."
        }
        if (@(Get-ProfileProperty $record "warnings").Count -ne 0) {
            throw "Approved perf report run warnings must be empty."
        }
        if (@(Get-ProfileProperty $record "failures").Count -ne 0) {
            throw "Approved perf report run failures must be empty."
        }
        if (-not [bool](Get-ProfileProperty $record "root_exited") -or
            -not [bool](Get-ProfileProperty $record "tree_termination_confirmed") -or
            -not [bool](Get-ProfileProperty $record "job_kill_on_close") -or
            -not [bool](Get-ProfileProperty $record "job_assigned") -or
            -not [bool](Get-ProfileProperty $record "job_cleanup_confirmed") -or
            [int](Get-ProfileProperty $record "job_active_processes_after_cleanup") -ne 0 -or
            [int](Get-ProfileProperty $record "exit_code") -ne 0) {
            throw "Approved perf report run cleanup proof is incomplete."
        }
        if (-not [string]::Equals([string](Get-ProfileProperty $record "configuration"), $Configuration, [System.StringComparison]::OrdinalIgnoreCase) -or
            [string](Get-ProfileProperty $record "telemetry_mode") -ne "Profile" -or
            -not [bool](Get-ProfileProperty $record "gpu_baseline_comparable") -or
            -not [bool](Get-ProfileProperty $record "baseline_identity_required")) {
            if (-not [bool](Get-ProfileProperty $record "baseline_identity_required")) {
                throw "Approved perf report comparison identity is required."
            }
            throw "Approved perf report run configuration or telemetry contract does not match the import request."
        }
        if ([int](Get-ProfileProperty $record "actual_width") -ne $expectedWidth -or
            [int](Get-ProfileProperty $record "actual_height") -ne $expectedHeight -or
            -not [bool](Get-ProfileProperty $record "extent_stable") -or
            [bool](Get-ProfileProperty $record "validation") -ne $expectedValidation -or
            [bool](Get-ProfileProperty $record "vsync") -ne $expectedVsync -or
            [bool](Get-ProfileProperty $record "fixed_camera") -ne $expectedFixedCamera -or
            -not [string]::Equals([string](Get-ProfileProperty $record "frame_cap"), $expectedFrameCap, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Approved perf report run extent or runtime flags do not match the current profile."
        }
        if (-not [string]::Equals([string]$record.source_sha, $CurrentSourceSha, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Approved perf report source SHA does not match the current source SHA."
        }
        if (-not [string]::Equals([string]$record.workload_fingerprint, [string]$expectedIdentity.fingerprint, [System.StringComparison]::Ordinal)) {
            throw "Approved perf report workload fingerprint does not match the current profile."
        }
        $recordWorkload = Get-ProfileProperty $record "workload"
        if ($null -eq $recordWorkload) {
            throw "Approved perf report readable workload attribution is missing."
        }
        $expectedWorkloadJson = $expectedIdentity.workload | ConvertTo-Json -Depth 8 -Compress
        $recordWorkloadJson = $recordWorkload | ConvertTo-Json -Depth 8 -Compress
        if (-not [string]::Equals($recordWorkloadJson, $expectedWorkloadJson, [System.StringComparison]::Ordinal)) {
            throw "Approved perf report readable workload does not match the current profile."
        }

        if ([int64](Get-ProfileProperty $record "frames_sampled") -le 0) {
            throw "Approved perf report must contain sampled frames."
        }
        foreach ($metricName in @(
            "cpu_frame_time_avg_ms",
            "cpu_frame_time_p95_ms",
            "cpu_frame_time_p99_ms",
            "process_private_bytes_peak_mb",
            "engine_heap_peak_mb",
            "draw_calls_avg"
        )) {
            $metricValue = ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $record $metricName)
            if ($null -eq $metricValue -or $metricValue -lt 0.0) {
                throw "Approved perf report CPU/memory/draw metrics must be finite non-negative numbers."
            }
        }

        $recordMetricNames = [string[]]@(Get-ProfileProperty $record "required_gpu_metrics")
        [Array]::Sort($recordMetricNames, [System.StringComparer]::Ordinal)
        if (($recordMetricNames -join "`n") -cne ($expectedMetricNames -join "`n")) {
            throw "Approved perf report required GPU metrics do not match the current profile."
        }
        $gpuCoverage = ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $record "gpu_coverage")
        $gpuSubmitted = [int64](Get-ProfileProperty $record "gpu_submitted")
        $gpuResolved = [int64](Get-ProfileProperty $record "gpu_resolved")
        $gpuValid = [int64](Get-ProfileProperty $record "gpu_valid")
        $gpuInvalid = [int64](Get-ProfileProperty $record "gpu_invalid")
        if ($null -eq $gpuCoverage -or $gpuCoverage -lt $minimumCoverage -or
            $gpuSubmitted -le 0 -or $gpuResolved -ne $gpuSubmitted -or
            $gpuValid -ne $gpuSubmitted -or $gpuInvalid -ne 0) {
            throw "Approved perf report GPU coverage and acknowledgement counts are incomplete."
        }
        $metricSummaries = Get-ProfileProperty $record "gpu_metric_summaries"
        foreach ($metricName in $expectedMetricNames) {
            $metric = Get-ProfileProperty $metricSummaries $metricName
            $coverage = if ($null -eq $metric) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-ProfileProperty $metric "coverage") }
            $average = if ($null -eq $metric) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-GpuMetricSummaryValue $metric "avg") }
            $p95 = if ($null -eq $metric) { $null } else { ConvertTo-PerfGateFiniteDouble (Get-GpuMetricSummaryValue $metric "p95") }
            if ($null -eq $metric -or $null -eq $coverage -or $coverage -lt $minimumCoverage -or
                $null -eq $average -or $average -lt 0.0 -or $null -eq $p95 -or $p95 -lt 0.0) {
                throw "Approved perf report required GPU metric '$metricName' is missing or invalid."
            }
        }
    }

    $baselineClone = $Baseline | ConvertTo-Json -Depth 32 | ConvertFrom-Json
    $reportRelativePath = $reportFullPath.Substring($repoFullPath.Length + 1).Replace('\', '/')
    Update-BaselinesFromRecords `
        -Baseline $baselineClone `
        -Profile $Profile `
        -Configuration $Configuration `
        -Records $records `
        -ReportRoot $reportRelativePath `
        -SourceReportSha256 $actualReportSha256

    return [PSCustomObject]@{
        baseline = $baselineClone
        report_path = $reportRelativePath
        report_sha256 = $actualReportSha256
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

function Get-EngineLogFiles {
    param(
        [string]$RepoRoot
    )

    $logRoot = Join-Path $RepoRoot "product/logs"
    if (-not (Test-Path -LiteralPath $logRoot)) {
        return @()
    }

    return @(Get-ChildItem -LiteralPath $logRoot -File -Filter "*.logfile" |
        Sort-Object LastWriteTime, FullName)
}

function Get-RunLogFiles {
    param(
        [string]$RepoRoot,
        [string[]]$ExistingPaths = @()
    )

    $existingPathLookup = @{}
    foreach ($path in @($ExistingPaths)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$path)) {
            $existingPathLookup[[System.IO.Path]::GetFullPath([string]$path)] = $true
        }
    }

    return @(Get-EngineLogFiles -RepoRoot $RepoRoot |
        Where-Object { -not $existingPathLookup.ContainsKey($_.FullName) })
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
    $cpuFrameTime = Get-ProfileProperty $Telemetry "cpu_frame_time_ms"
    $memory = Get-ProfileProperty $Telemetry "memory"
    $renderStats = Get-ProfileProperty $Telemetry "render_stats"
    foreach ($metricContract in @(
        [PSCustomObject]@{ container = $cpuFrameTime; source = "avg"; record = "cpu_frame_time_avg_ms"; label = "CPU Avg" },
        [PSCustomObject]@{ container = $cpuFrameTime; source = "p95"; record = "cpu_frame_time_p95_ms"; label = "CPU P95" },
        [PSCustomObject]@{ container = $cpuFrameTime; source = "p99"; record = "cpu_frame_time_p99_ms"; label = "CPU P99" },
        [PSCustomObject]@{ container = $memory; source = "process_private_bytes_peak_mb"; record = "process_private_bytes_peak_mb"; label = "Private MB" },
        [PSCustomObject]@{ container = $memory; source = "engine_heap_peak_mb"; record = "engine_heap_peak_mb"; label = "Heap MB" },
        [PSCustomObject]@{ container = $renderStats; source = "draw_calls_avg"; record = "draw_calls_avg"; label = "Draw Calls" }
    )) {
        $rawValue = Get-ProfileProperty $metricContract.container $metricContract.source
        $validatedValue = ConvertTo-PerfGateFiniteDouble $rawValue
        if ($null -eq $validatedValue -or $validatedValue -lt 0.0) {
            Add-Failure $Record "Required current $($metricContract.label) value must be a finite non-negative number"
            continue
        }
        $Record.($metricContract.record) = [double]$validatedValue
    }

    if ([int64]$memory.engine_heap_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "Engine heap live bytes at shutdown: $($memory.engine_heap_shutdown_live_bytes)"
    }
    if ([bool]$memory.gpu_allocator_supported -and [int64]$memory.gpu_allocator_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "GPU allocator live bytes at shutdown: $($memory.gpu_allocator_shutdown_live_bytes)"
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

        if ($gpuRequired) {
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

    $hashOraclePath = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-sha256-{0}-{1}.bin" -f $PID, [Guid]::NewGuid().ToString("N"))
    try {
        [System.IO.File]::WriteAllBytes($hashOraclePath, [System.Text.Encoding]::ASCII.GetBytes("abc"))
        $hashOracleActual = Get-PerfGateFileSha256 -Path $hashOraclePath
        Assert-SelfTest ($hashOracleActual -eq "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") "File SHA256 helper did not match the known abc digest."
    }
    finally {
        Remove-Item -LiteralPath $hashOraclePath -Force -ErrorAction SilentlyContinue
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
        $logRoot = Join-Path $tempRoot "product/logs"
        New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
        $priorAppLog = Join-Path $logRoot "AshAppLogFile_prior.logfile"
        $priorEngineLog = Join-Path $logRoot "AshEngineLogFile_prior.logfile"
        "prior app" | Set-Content -LiteralPath $priorAppLog -Encoding UTF8
        "prior engine" | Set-Content -LiteralPath $priorEngineLog -Encoding UTF8
        $existingLogPaths = @($priorAppLog, $priorEngineLog)

        $currentAppLog = Join-Path $logRoot "AshAppLogFile_current.logfile"
        $currentEngineLog = Join-Path $logRoot "AshEngineLogFile_current.logfile"
        "current app" | Set-Content -LiteralPath $currentAppLog -Encoding UTF8
        "current engine" | Set-Content -LiteralPath $currentEngineLog -Encoding UTF8
        (Get-Item -LiteralPath $priorAppLog).LastWriteTime = (Get-Date).AddMinutes(1)
        (Get-Item -LiteralPath $priorEngineLog).LastWriteTime = (Get-Date).AddMinutes(1)
        (Get-Item -LiteralPath $currentAppLog).LastWriteTime = (Get-Date).AddMinutes(-1)
        (Get-Item -LiteralPath $currentEngineLog).LastWriteTime = (Get-Date).AddMinutes(-1)

        $freshLogs = @(Get-RunLogFiles -RepoRoot $tempRoot -ExistingPaths $existingLogPaths)
        Assert-SelfTest ($freshLogs.Count -eq 2) "Fresh log discovery must return exactly the current session pair."
        Assert-SelfTest (@($freshLogs.Name) -contains "AshAppLogFile_current.logfile") "Fresh log discovery omitted the current application log."
        Assert-SelfTest (@($freshLogs.Name) -contains "AshEngineLogFile_current.logfile") "Fresh log discovery omitted the current engine log."
        Assert-SelfTest (@($freshLogs.Name) -notcontains "AshAppLogFile_prior.logfile" -and @($freshLogs.Name) -notcontains "AshEngineLogFile_prior.logfile") "Fresh log discovery included a prior session after its late flush timestamp changed."

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

        $jsonWriterPath = Join-Path $tempRoot "deterministic-json-writer.json"
        $jsonWriterDocument = [PSCustomObject][ordered]@{
            schema_version = 1
            nested = [PSCustomObject][ordered]@{
                value = "stable"
            }
        }
        Write-PerfGateJsonFile -InputObject $jsonWriterDocument -LiteralPath $jsonWriterPath -Depth 8
        $firstJsonWriterBytes = [System.IO.File]::ReadAllBytes($jsonWriterPath)
        $firstJsonWriterText = [System.Text.Encoding]::UTF8.GetString($firstJsonWriterBytes)
        Assert-SelfTest (
            $firstJsonWriterBytes.Length -ge 1 -and
            -not (
                $firstJsonWriterBytes.Length -ge 3 -and
                $firstJsonWriterBytes[0] -eq 0xEF -and
                $firstJsonWriterBytes[1] -eq 0xBB -and
                $firstJsonWriterBytes[2] -eq 0xBF
            )
        ) "PerfGate JSON files must use UTF-8 without a BOM."
        Assert-SelfTest (-not $firstJsonWriterText.Contains("`r")) "PerfGate JSON files must use LF line endings."
        Assert-SelfTest ($firstJsonWriterText.EndsWith("`n") -and -not $firstJsonWriterText.EndsWith("`n`n")) "PerfGate JSON files must end with exactly one LF."
        Assert-SelfTest (-not [regex]::IsMatch($firstJsonWriterText, "(?m)[ `t]+$")) "PerfGate JSON files must not contain trailing whitespace."
        $jsonWriterRoundTrip = $firstJsonWriterText | ConvertFrom-Json
        Assert-SelfTest ($jsonWriterRoundTrip.schema_version -eq 1 -and $jsonWriterRoundTrip.nested.value -eq "stable") "PerfGate JSON writer must preserve the document structure."

        Write-PerfGateJsonFile -InputObject $jsonWriterDocument -LiteralPath $jsonWriterPath -Depth 8
        $secondJsonWriterBytes = [System.IO.File]::ReadAllBytes($jsonWriterPath)
        Assert-SelfTest (
            [System.Convert]::ToBase64String($firstJsonWriterBytes) -ceq
            [System.Convert]::ToBase64String($secondJsonWriterBytes)
        ) "PerfGate JSON writes must be byte-deterministic."
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
    $missingComparisonThresholdProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $missingComparisonThresholdProfile.PSObject.Properties.Remove("comparison_thresholds")
    Assert-SelfTestThrows {
        Assert-PerfGateComparisonProfileContract -ProfileConfig $missingComparisonThresholdProfile
    } "comparison_thresholds" "A gpu_timing=required profile without comparison_thresholds must fail preflight."

    $nullComparisonThresholdProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $nullComparisonThresholdProfile.comparison_thresholds = $null
    Assert-SelfTestThrows {
        Assert-PerfGateComparisonProfileContract -ProfileConfig $nullComparisonThresholdProfile
    } "comparison_thresholds" "A gpu_timing=required profile with null comparison_thresholds must fail preflight."

    $negativeFloorProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $negativeFloorProfile.comparison_thresholds.gpu_frame_avg_ms.absolute_floor = -0.01
    Assert-SelfTestThrows {
        Assert-PerfGateComparisonProfileContract -ProfileConfig $negativeFloorProfile
    } "gpu_frame_avg_ms" "A fixed GPU profile with a negative comparison floor must fail preflight."

    $wrongTierProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $wrongTierProfile.comparison_thresholds.gpu_pass_tiers[0].minimum_baseline_avg_ms = 0.25
    Assert-SelfTestThrows {
        Assert-PerfGateComparisonProfileContract -ProfileConfig $wrongTierProfile
    } "gpu_pass_tiers" "A fixed GPU profile with malformed pass tiers must fail preflight."

    $nonRequiredGpuComparisonProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $nonRequiredGpuComparisonProfile.gpu_timing = "optional"
    Assert-SelfTestThrows {
        Assert-PerfGateComparisonProfileContract -ProfileConfig $nonRequiredGpuComparisonProfile
    } "gpu_timing=required" "comparison_thresholds must not activate fixed comparison without required GPU timing and identity."

    Assert-PerfGateComparisonProfileContract -ProfileConfig $standardProfile
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
    $telemetryOffWithoutGpuMetadata = $validV2 | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $telemetryOffWithoutGpuMetadata.PSObject.Properties.Remove("gpu")
    $telemetryOffWithoutGpuMetadataRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $telemetryOffWithoutGpuMetadataRecord -Telemetry $telemetryOffWithoutGpuMetadata -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Off"
    Assert-SelfTest ($telemetryOffWithoutGpuMetadataRecord.status -eq "PASS" -and -not $telemetryOffWithoutGpuMetadataRecord.gpu_baseline_comparable) "Telemetry-off fixed-runtime A/B must validate schema v2 runtime metadata without requiring disabled GPU timing metadata."

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
        [PSCustomObject]@{ label = "missing gpu"; telemetry = $missingGpuTelemetry; telemetry_mode = "Profile"; failure_pattern = "GPU hardware metadata" },
        [PSCustomObject]@{ label = "missing backend_info"; telemetry = $missingBackendInfoTelemetry; telemetry_mode = "Profile"; failure_pattern = "backend_info" },
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
    $mainSource = $runnerSource.Substring($runnerSource.LastIndexOf('$repoRoot = Get-RepoRoot'))
    $comparisonPreflightIndex = $mainSource.IndexOf('Assert-PerfGateComparisonProfileContract -ProfileConfig $profileConfig')
    $timestampIndex = $mainSource.IndexOf('$timestamp =')
    $runPlanIndex = $mainSource.IndexOf('$records = @(New-PerfGateRunPlan')
    $reportSideEffectIndex = $mainSource.IndexOf('New-Item -ItemType Directory -Force -Path $reportRoot')
    $buildSideEffectIndex = $mainSource.IndexOf('Invoke-BatchCommand')
    Assert-SelfTest (
        $comparisonPreflightIndex -ge 0 -and
        $timestampIndex -gt $comparisonPreflightIndex -and
        $runPlanIndex -gt $comparisonPreflightIndex -and
        $reportSideEffectIndex -gt $comparisonPreflightIndex -and
        $buildSideEffectIndex -gt $comparisonPreflightIndex
    ) "Fixed GPU comparison contract validation must precede timestamp, run-plan, report, build, and process side effects."

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

    $toolsSpecPath = Join-Path (Get-RepoRoot) "docs/specs/modules/tools.md"
    $toolsSpec = Get-Content -Raw -LiteralPath $toolsSpecPath
    if ($toolsSpec -notmatch 'Start.*AssignProcessToJobObject.*(?:must not|不得).*spawn' -and
        $toolsSpec -notmatch 'Start.*AssignProcessToJobObject.*(?:must not|不得).*子进程') {
        $qualityContractFailures += "tools spec does not state the trusted launcher Start-to-Assign no-child contract"
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

    $missingCleanupProofRecord = [PSCustomObject]@{
        target = "Sandbox"
        backend = "Vulkan"
        status = "PASS"
        tree_termination_confirmed = $true
        gpu_baseline_comparable = $false
        required_gpu_metrics = @()
        cpu_frame_time_avg_ms = 1.0
        cpu_frame_time_p95_ms = 1.0
        cpu_frame_time_p99_ms = 1.0
        process_private_bytes_peak_mb = 1.0
        engine_heap_peak_mb = 1.0
        draw_calls_avg = 1.0
    }
    $missingCleanupProofBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $missingCleanupProofBaseline -Profile "Standard" -Configuration "Debug" -Records @($missingCleanupProofRecord) -ReportRoot "self-test"
    } "missing Job Object cleanup proof")) {
        $qualityContractFailures += "baseline bless accepted a record with no Job Object cleanup proof"
    }

    $notRunBlessRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "not-run.json" "not-run.out" "not-run.err"
    $notRunBlessRecord.tree_termination_confirmed = $true
    $notRunBlessRecord.job_cleanup_confirmed = $true
    $notRunBlessBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    if (-not (Test-SelfTestThrows {
        Update-BaselinesFromRecords -Baseline $notRunBlessBaseline -Profile "Standard" -Configuration "Debug" -Records @($notRunBlessRecord) -ReportRoot "self-test"
    } "terminal PASS or WARN")) {
        $qualityContractFailures += "baseline bless accepted a record that never reached a successful terminal status"
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

    $gateAProfile = ConvertFrom-Json @'
{
  "gpu_timing": "required",
  "min_gpu_coverage": 0.95,
  "required_gpu_metrics": [ "GPU.Frame", "GPU.GBuffer" ],
  "comparison_thresholds": {
    "cpu_frame_time_avg_ms": { "relative_percent": 5, "absolute_floor": 0.25 },
    "cpu_frame_time_p95_ms": { "relative_percent": 8, "absolute_floor": 0.50 },
    "cpu_frame_time_p99_ms": { "relative_percent": 12, "absolute_floor": 1.00 },
    "process_private_bytes_peak_mb": { "relative_percent": 5, "absolute_floor": 128 },
    "engine_heap_peak_mb": { "relative_percent": 10, "absolute_floor": 1 },
    "draw_calls_avg": { "relative_percent": 0, "absolute_floor": 0 },
    "gpu_frame_avg_ms": { "relative_percent": 5, "absolute_floor": 0.25 },
    "gpu_frame_p95_ms": { "relative_percent": 8, "absolute_floor": 0.50 },
    "gpu_pass_tiers": [
      {
        "minimum_baseline_avg_ms": 0.5,
        "avg": { "relative_percent": 8, "absolute_floor": 0.10 },
        "p95": { "relative_percent": 12, "absolute_floor": 0.20 }
      },
      {
        "minimum_baseline_avg_ms": 0.1,
        "avg": { "relative_percent": 15, "absolute_floor": 0.05 },
        "p95": { "relative_percent": 20, "absolute_floor": 0.10 }
      },
      {
        "minimum_baseline_avg_ms": 0.0,
        "avg": { "absolute_floor": 0.03 },
        "p95": { "absolute_floor": 0.05 }
      }
    ]
  }
}
'@
    $gateABaseline = ConvertFrom-Json @'
{
  "baselines": {
    "VegetationFullPipeline": {
      "Release": {
        "Sandbox": {
          "Vulkan": {
            "cpu_frame_time_avg_ms": 1.0,
            "cpu_frame_time_p95_ms": 2.0,
            "cpu_frame_time_p99_ms": 3.0,
            "process_private_bytes_peak_mb": 1000.0,
            "engine_heap_peak_mb": 10.0,
            "draw_calls_avg": 50.0,
            "gpu_metrics": {
              "GPU.Frame": { "avg": 10.0, "p95": 12.0 },
              "GPU.GBuffer": { "avg": 1.0, "p95": 1.2 }
            }
          }
        }
      }
    }
  }
}
'@
    $gateABaselineEntry = Get-BaselineEntry -Baseline $gateABaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
    Set-ObjectProperty $gateABaselineEntry "attribution" ([PSCustomObject]@{
        target = "Sandbox"; backend = "Vulkan"; configuration = "Release"
        adapter = "SelfTest GPU"; driver = "SelfTest Driver"; os_build = "10.0.26100.1"; source_sha = ("a" * 40)
    })
    Set-ObjectProperty $gateABaselineEntry "workload_fingerprint" ("c" * 64)
    Set-ObjectProperty $gateABaselineEntry "workload" ([PSCustomObject]@{ self_test = $true })
    $gateARecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $gateARecord.status = "PASS"
    $gateARecord.gpu_adapter = "SelfTest GPU"
    $gateARecord.gpu_driver = "SelfTest Driver"
    $gateARecord.os_build = "10.0.26100.1"
    $gateARecord.source_sha = ("b" * 40)
    $gateARecord.workload_fingerprint = ("c" * 64)
    $gateARecord.workload = [PSCustomObject]@{ self_test = $true }
    $gateARecord.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
    $gateARecord.gpu_metric_summaries = ConvertFrom-Json @'
{
  "GPU.Frame": { "avg": 10.51, "p95": 12.0 },
  "GPU.GBuffer": { "avg": 1.0, "p95": 1.2 }
}
'@
    Compare-RecordToBaseline -Record $gateARecord -Baseline $gateABaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($gateARecord.status -eq "WARN") "Gate A RED: GPU.Frame avg regression must produce WARN."
    Assert-SelfTest (@($gateARecord.warnings).Count -eq 1) "Gate A RED: GPU.Frame must emit exactly one dedicated warning and must not enter pass-tier comparison."

    $gateAPassRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $gateAPassRecord.status = "PASS"
    $gateAPassRecord.gpu_adapter = "SelfTest GPU"
    $gateAPassRecord.gpu_driver = "SelfTest Driver"
    $gateAPassRecord.os_build = "10.0.26100.1"
    $gateAPassRecord.source_sha = ("b" * 40)
    $gateAPassRecord.workload_fingerprint = ("c" * 64)
    $gateAPassRecord.workload = [PSCustomObject]@{ self_test = $true }
    $gateAPassRecord.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
    $gateAPassRecord.gpu_metric_summaries = ConvertFrom-Json @'
{
  "GPU.Frame": { "avg": 10.0, "p95": 12.0 },
  "GPU.GBuffer": { "avg": 1.11, "p95": 1.2 }
}
'@
    Compare-RecordToBaseline -Record $gateAPassRecord -Baseline $gateABaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($gateAPassRecord.status -eq "WARN" -and (@($gateAPassRecord.warnings) -join " ") -match "GPU.GBuffer avg") "Gate A RED: every required pass metric must use its baseline-avg tier and emit a per-metric WARN."

    foreach ($tierCase in @(
        [PSCustomObject]@{ label = "medium floor"; baseline_avg = 0.20; baseline_p95 = 0.30; current_avg = 0.251; current_p95 = 0.30; warning_count = 1 },
        [PSCustomObject]@{ label = "medium relative"; baseline_avg = 0.40; baseline_p95 = 0.30; current_avg = 0.461; current_p95 = 0.30; warning_count = 1 },
        [PSCustomObject]@{ label = "tiny absolute-only"; baseline_avg = 0.05; baseline_p95 = 0.08; current_avg = 0.081; current_p95 = 0.131; warning_count = 2 }
    )) {
        $tierBaseline = $gateABaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        $tierBaselineMetric = (Get-BaselineEntry -Baseline $tierBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan").gpu_metrics.'GPU.GBuffer'
        $tierBaselineMetric.avg = $tierCase.baseline_avg
        $tierBaselineMetric.p95 = $tierCase.baseline_p95
        $tierRecord = $gateARecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        $tierRecord.status = "PASS"
        $tierRecord.failures = @()
        $tierRecord.warnings = @()
        $tierRecord.baseline_deltas = @()
        $tierRecord.gpu_metric_summaries.'GPU.Frame'.avg = 10.0
        $tierRecord.gpu_metric_summaries.'GPU.GBuffer'.avg = $tierCase.current_avg
        $tierRecord.gpu_metric_summaries.'GPU.GBuffer'.p95 = $tierCase.current_p95
        Compare-RecordToBaseline -Record $tierRecord -Baseline $tierBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
        Assert-SelfTest ($tierRecord.status -eq "WARN" -and @($tierRecord.warnings).Count -eq $tierCase.warning_count) "GPU pass $($tierCase.label) tier did not use the approved larger-of threshold."
    }

    $gateACpuFloorRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $gateACpuFloorRecord.status = "PASS"
    $gateACpuFloorRecord.gpu_adapter = "SelfTest GPU"
    $gateACpuFloorRecord.gpu_driver = "SelfTest Driver"
    $gateACpuFloorRecord.os_build = "10.0.26100.1"
    $gateACpuFloorRecord.source_sha = ("b" * 40)
    $gateACpuFloorRecord.workload_fingerprint = ("c" * 64)
    $gateACpuFloorRecord.workload = [PSCustomObject]@{ self_test = $true }
    $gateACpuFloorRecord.cpu_frame_time_avg_ms = 1.26
    $gateACpuFloorRecord.cpu_frame_time_p95_ms = 2.0
    $gateACpuFloorRecord.cpu_frame_time_p99_ms = 3.0
    $gateACpuFloorRecord.process_private_bytes_peak_mb = 1000.0
    $gateACpuFloorRecord.engine_heap_peak_mb = 10.0
    $gateACpuFloorRecord.draw_calls_avg = 50.0
    $gateACpuFloorRecord.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
    $gateACpuFloorRecord.gpu_metric_summaries = ConvertFrom-Json @'
{
  "GPU.Frame": { "avg": 10.0, "p95": 12.0 },
  "GPU.GBuffer": { "avg": 1.0, "p95": 1.2 }
}
'@
    Compare-RecordToBaseline -Record $gateACpuFloorRecord -Baseline $gateABaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($gateACpuFloorRecord.status -eq "WARN" -and (@($gateACpuFloorRecord.warnings) -join " ") -match "CPU Avg") "Gate A RED: CPU comparison must use the larger absolute floor when its relative threshold is smaller."

    $gateAMissingCurrentMetric = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $gateAMissingCurrentMetric.status = "PASS"
    $gateAMissingCurrentMetric.gpu_adapter = "SelfTest GPU"
    $gateAMissingCurrentMetric.gpu_driver = "SelfTest Driver"
    $gateAMissingCurrentMetric.os_build = "10.0.26100.1"
    $gateAMissingCurrentMetric.source_sha = ("b" * 40)
    $gateAMissingCurrentMetric.workload_fingerprint = ("c" * 64)
    $gateAMissingCurrentMetric.workload = [PSCustomObject]@{ self_test = $true }
    $gateAMissingCurrentMetric.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
    $gateAMissingCurrentMetric.gpu_metric_summaries = ConvertFrom-Json @'
{
  "GPU.Frame": { "avg": 10.0, "p95": 12.0 },
  "GPU.GBuffer": { "avg": 1.0 }
}
'@
    Compare-RecordToBaseline -Record $gateAMissingCurrentMetric -Baseline $gateABaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($gateAMissingCurrentMetric.status -eq "FAIL" -and (@($gateAMissingCurrentMetric.failures) -join " ") -match "GPU.GBuffer.*(current.*p95|p95.*current)") "Gate A RED: a missing current required GPU metric statistic must fail closed."

    $nonfiniteCurrentTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
    $nonfiniteCurrentTelemetry.cpu_frame_time_ms.avg = [double]::NaN
    $nonfiniteCurrentRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Test-TelemetryData -Record $nonfiniteCurrentRecord -Telemetry $nonfiniteCurrentTelemetry -ProfileConfig $gateAProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
    Assert-SelfTest ($nonfiniteCurrentRecord.status -eq "FAIL" -and (@($nonfiniteCurrentRecord.failures) -join " ") -match "CPU Avg.*finite") "Gate A RED: non-finite current CPU/memory/draw comparison metrics must fail closed before baseline lookup."

    $identityTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-identity-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $identityTestRoot | Out-Null
    try {
        $identityScenePath = Join-Path $identityTestRoot "scene.json"
        '{ "scene": "one" }' | Set-Content -LiteralPath $identityScenePath -Encoding UTF8
        $identityProfile = ConvertFrom-Json @'
{
  "scene": "scene.json",
  "window_width": 2560,
  "window_height": 1440,
  "fixed_camera": true,
  "vsync": false,
  "validation": false,
  "frame_cap": "off",
  "required_gpu_metrics": [ "GPU.GBuffer", "GPU.Frame" ]
}
'@
        $referenceIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $identityProfile -RepoRoot $identityTestRoot
        $normalizedEquivalentProfile = $identityProfile | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $normalizedEquivalentProfile.scene = ".\scene.json"
        $normalizedEquivalentProfile.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
        $normalizedEquivalentIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $normalizedEquivalentProfile -RepoRoot $identityTestRoot
        Assert-SelfTest ($normalizedEquivalentIdentity.fingerprint -eq $referenceIdentity.fingerprint) "Workload fingerprint must normalize an equivalent scene path and required-metric ordering."
        foreach ($mutation in @(
            { param($value) $value.window_width = 1920 },
            { param($value) $value.fixed_camera = $false },
            { param($value) $value.vsync = $true },
            { param($value) $value.validation = $true },
            { param($value) $value.frame_cap = "60" },
            { param($value) $value.required_gpu_metrics = @("GPU.Frame") }
        )) {
            $mutatedProfile = $identityProfile | ConvertTo-Json -Depth 8 | ConvertFrom-Json
            & $mutation $mutatedProfile
            $mutatedIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $mutatedProfile -RepoRoot $identityTestRoot
            Assert-SelfTest ($mutatedIdentity.fingerprint -ne $referenceIdentity.fingerprint) "Gate A RED: every workload contract field mutation must change the fingerprint."
        }

        '{ "scene": "two" }' | Set-Content -LiteralPath $identityScenePath -Encoding UTF8
        $contentMutatedIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $identityProfile -RepoRoot $identityTestRoot
        Assert-SelfTest ($contentMutatedIdentity.fingerprint -ne $referenceIdentity.fingerprint) "Gate A RED: a scene content mutation must change the workload fingerprint."
    }
    finally {
        Remove-Item -LiteralPath $identityTestRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $identityInjectionProfile = $vegetationProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Set-ObjectProperty $identityInjectionProfile "comparison_thresholds" $gateAProfile.comparison_thresholds
    $identityInjectionRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    Set-PerfGateRunIdentity `
        -Records @($identityInjectionRecord) `
        -ProfileConfig $identityInjectionProfile `
        -RepoRoot $repoRoot `
        -SourceSha ("d" * 40) `
        -OsBuild "10.0.26100.1"
    Assert-SelfTest (
        $identityInjectionRecord.baseline_identity_required -and
        $identityInjectionRecord.source_sha -eq ("d" * 40) -and
        $identityInjectionRecord.os_build -eq "10.0.26100.1" -and
        $identityInjectionRecord.workload_fingerprint -match '^[0-9a-f]{64}$' -and
        $null -ne $identityInjectionRecord.workload
    ) "Gate A RED: actual run records must receive source SHA, OS build, normalized workload fields, and fingerprint before execution."
    $identityMainSource = $runnerSource.Substring($runnerSource.LastIndexOf('$repoRoot = Get-RepoRoot'))
    $identityPlanIndex = $identityMainSource.IndexOf('$records = @(New-PerfGateRunPlan')
    $identityInjectionIndex = $identityMainSource.IndexOf('Set-PerfGateRunIdentity')
    $identitySideEffectIndex = $identityMainSource.IndexOf('New-Item -ItemType Directory -Force -Path $reportRoot')
    Assert-SelfTest (
        $identityInjectionIndex -gt $identityPlanIndex -and
        $identitySideEffectIndex -gt $identityInjectionIndex
    ) "Gate A RED: main runner must inject comparison identity into actual records before report/build/process side effects."

    $spreadRecords = @()
    foreach ($spreadSample in @(
        [PSCustomObject]@{ backend = "Vulkan"; cpu_avg = 10.0; cpu_p95 = 12.0; gpu_avg = 8.0; gpu_p95 = 10.0 },
        [PSCustomObject]@{ backend = "Vulkan"; cpu_avg = 10.1; cpu_p95 = 12.2; gpu_avg = 8.1; gpu_p95 = 10.2 },
        [PSCustomObject]@{ backend = "Vulkan"; cpu_avg = 10.2; cpu_p95 = 12.4; gpu_avg = 8.2; gpu_p95 = 10.4 },
        [PSCustomObject]@{ backend = "DX12"; cpu_avg = 20.0; cpu_p95 = 22.0; gpu_avg = 18.0; gpu_p95 = 20.0 },
        [PSCustomObject]@{ backend = "DX12"; cpu_avg = 20.4; cpu_p95 = 22.2; gpu_avg = 18.1; gpu_p95 = 20.2 },
        [PSCustomObject]@{ backend = "DX12"; cpu_avg = 20.8; cpu_p95 = 22.4; gpu_avg = 18.2; gpu_p95 = 20.4 }
    )) {
        $spreadRecords += [PSCustomObject]@{
            target = "Sandbox"
            backend = $spreadSample.backend
            cpu_frame_time_avg_ms = $spreadSample.cpu_avg
            cpu_frame_time_p95_ms = $spreadSample.cpu_p95
            gpu_frame_avg_ms = $spreadSample.gpu_avg
            gpu_frame_p95_ms = $spreadSample.gpu_p95
        }
    }
    $spreadSummary = @(Get-PerfGateThreeRunSpread -Records $spreadRecords)
    $vulkanSpread = @($spreadSummary | Where-Object { $_.backend -eq "Vulkan" })[0]
    $dx12Spread = @($spreadSummary | Where-Object { $_.backend -eq "DX12" })[0]
    Assert-SelfTest (
        $spreadSummary.Count -eq 2 -and
        $vulkanSpread.run_count -eq 3 -and
        [Math]::Abs([double]$vulkanSpread.cpu_frame_time_avg_ms.spread_ms - 0.2) -lt 0.000001 -and
        $vulkanSpread.status -eq "PASS" -and
        [Math]::Abs([double]$dx12Spread.cpu_frame_time_avg_ms.spread_ms - 0.8) -lt 0.000001 -and
        $dx12Spread.status -eq "FAIL"
    ) "Gate A RED: three-run spread must group target+backend independently and apply avg/p95 noise contracts without mixing Vulkan and DX12."

    $queuePhaseSpreadRecords = @()
    foreach ($queuePhaseSample in @(
        [PSCustomObject]@{ cpu_p95 = 15.6162; gpu_p95 = 15.6162 },
        [PSCustomObject]@{ cpu_p95 = 16.6046; gpu_p95 = 16.6046 },
        [PSCustomObject]@{ cpu_p95 = 16.0000; gpu_p95 = 16.0000 }
    )) {
        $queuePhaseSpreadRecords += [PSCustomObject]@{
            target = "Sandbox"
            backend = "DX12"
            cpu_frame_time_avg_ms = 14.7
            cpu_frame_time_p95_ms = $queuePhaseSample.cpu_p95
            gpu_frame_avg_ms = 14.7
            gpu_frame_p95_ms = $queuePhaseSample.gpu_p95
        }
    }
    $queuePhaseSpread = @(Get-PerfGateThreeRunSpread -Records $queuePhaseSpreadRecords)[0]
    Assert-SelfTest (
        [double]$queuePhaseSpread.cpu_frame_time_p95_ms.relative_percent -eq 8.0 -and
        [double]$queuePhaseSpread.cpu_frame_time_p95_ms.absolute_floor_ms -eq 0.50 -and
        $queuePhaseSpread.cpu_frame_time_p95_ms.status -eq "PASS" -and
        [double]$queuePhaseSpread.gpu_frame_p95_ms.relative_percent -eq 5.0 -and
        [double]$queuePhaseSpread.gpu_frame_p95_ms.absolute_floor_ms -eq 0.30 -and
        $queuePhaseSpread.gpu_frame_p95_ms.status -eq "FAIL" -and
        $queuePhaseSpread.status -eq "FAIL"
    ) "CPU p95 candidate stability must match its approved regression threshold without weakening GPU.Frame p95 stability."

    $comparableBaseline = $gateABaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $comparableEntry = Get-BaselineEntry -Baseline $comparableBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
    Set-ObjectProperty $comparableEntry "attribution" ([PSCustomObject]@{
        target = "Sandbox"
        backend = "Vulkan"
        configuration = "Release"
        adapter = "SelfTest GPU"
        driver = "SelfTest Driver"
        os_build = "10.0.26100.1"
        source_sha = ("a" * 40)
    })
    Set-ObjectProperty $comparableEntry "workload_fingerprint" $referenceIdentity.fingerprint
    Set-ObjectProperty $comparableEntry "workload" $referenceIdentity.workload

    $comparableRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $comparableRecord.configuration = "Release"
    $comparableRecord.status = "PASS"
    $comparableRecord.baseline_identity_required = $true
    $comparableRecord.cpu_frame_time_avg_ms = 1.0
    $comparableRecord.cpu_frame_time_p95_ms = 2.0
    $comparableRecord.cpu_frame_time_p99_ms = 3.0
    $comparableRecord.process_private_bytes_peak_mb = 1000.0
    $comparableRecord.engine_heap_peak_mb = 10.0
    $comparableRecord.draw_calls_avg = 50.0
    $comparableRecord.required_gpu_metrics = @("GPU.Frame", "GPU.GBuffer")
    $comparableRecord.gpu_metric_summaries = ConvertFrom-Json '{ "GPU.Frame": { "coverage": 0.96, "avg": 10.0, "p95": 12.0 }, "GPU.GBuffer": { "coverage": 0.96, "avg": 1.0, "p95": 1.2 } }'
    $comparableRecord.gpu_adapter = "SelfTest GPU"
    $comparableRecord.gpu_driver = "SelfTest Driver"
    Set-ObjectProperty $comparableRecord "os_build" "10.0.26100.1"
    Set-ObjectProperty $comparableRecord "source_sha" ("b" * 40)
    Set-ObjectProperty $comparableRecord "workload_fingerprint" $referenceIdentity.fingerprint
    Set-ObjectProperty $comparableRecord "workload" $referenceIdentity.workload

    $differentSourceRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Compare-RecordToBaseline -Record $differentSourceRecord -Baseline $comparableBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($differentSourceRecord.status -eq "PASS" -and $differentSourceRecord.baseline_status -eq "COMPARED") "Gate A RED: source SHA must be attributed but is allowed to differ across comparisons."

    $telemetryOffComparisonRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $telemetryOffComparisonRecord.gpu_baseline_comparable = $false
    $telemetryOffComparisonRecord.gpu_adapter = "n/a"
    $telemetryOffComparisonRecord.gpu_driver = "n/a"
    $telemetryOffComparisonRecord.gpu_metric_summaries = [PSCustomObject]@{}
    Compare-RecordToBaseline -Record $telemetryOffComparisonRecord -Baseline $comparableBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($telemetryOffComparisonRecord.status -eq "PASS" -and $telemetryOffComparisonRecord.baseline_status -eq "NOT_COMPARED") "Gate A RED: TelemetryMode Off A/B must remain runnable when a GPU baseline exists, while staying excluded from comparison and bless."

    foreach ($coreRegression in @(
        [PSCustomObject]@{ name = "cpu_frame_time_avg_ms"; label = "CPU Avg"; current = 1.26 },
        [PSCustomObject]@{ name = "cpu_frame_time_p95_ms"; label = "CPU P95"; current = 2.51 },
        [PSCustomObject]@{ name = "cpu_frame_time_p99_ms"; label = "CPU P99"; current = 4.01 },
        [PSCustomObject]@{ name = "process_private_bytes_peak_mb"; label = "Private MB"; current = 1128.01 },
        [PSCustomObject]@{ name = "engine_heap_peak_mb"; label = "Heap MB"; current = 11.01 },
        [PSCustomObject]@{ name = "draw_calls_avg"; label = "Draw Calls"; current = 50.01 }
    )) {
        $coreRegressionRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        Set-ObjectProperty $coreRegressionRecord $coreRegression.name $coreRegression.current
        Compare-RecordToBaseline -Record $coreRegressionRecord -Baseline $comparableBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
        Assert-SelfTest (
            $coreRegressionRecord.status -eq "WARN" -and
            @($coreRegressionRecord.warnings).Count -eq 1 -and
            (@($coreRegressionRecord.warnings) -join " ") -match ([regex]::Escape($coreRegression.label))
        ) "Approved CPU/memory/draw thresholds must emit one warning for $($coreRegression.name)."
    }

    $missingCoreBaseline = $comparableBaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    (Get-BaselineEntry -Baseline $missingCoreBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan").PSObject.Properties.Remove("cpu_frame_time_p99_ms")
    $missingCoreBaselineRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Compare-RecordToBaseline -Record $missingCoreBaselineRecord -Baseline $missingCoreBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($missingCoreBaselineRecord.status -eq "FAIL" -and (@($missingCoreBaselineRecord.failures) -join " ") -match "CPU P99.*baseline.*finite") "Missing baseline CPU/memory/draw metrics must fail closed."

    $allMetricsBaselineRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
    $allMetricsBaselineRecord.configuration = "Release"
    $allMetricsBaselineRecord.status = "PASS"
    $allMetricsBaselineRecord.tree_termination_confirmed = $true
    $allMetricsBaselineRecord.job_cleanup_confirmed = $true
    $allMetricsBaselineRecord.baseline_identity_required = $true
    $allMetricsBaselineRecord.cpu_frame_time_avg_ms = 2.0
    $allMetricsBaselineRecord.cpu_frame_time_p95_ms = 2.5
    $allMetricsBaselineRecord.cpu_frame_time_p99_ms = 3.0
    $allMetricsBaselineRecord.process_private_bytes_peak_mb = 100.0
    $allMetricsBaselineRecord.engine_heap_peak_mb = 10.0
    $allMetricsBaselineRecord.draw_calls_avg = 50.0
    $allMetricsBaselineRecord.required_gpu_metrics = @($vegetationProfile.required_gpu_metrics)
    $allMetricsBaselineRecord.gpu_metric_summaries = $validV2.gpu.metrics | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $allMetricsBaselineRecord.gpu_adapter = "SelfTest GPU"
    $allMetricsBaselineRecord.gpu_driver = "SelfTest Driver"
    $allMetricsBaselineRecord.os_build = "10.0.26100.1"
    $allMetricsBaselineRecord.source_sha = ("a" * 40)
    $allMetricsBaselineRecord.workload_fingerprint = ("e" * 64)
    $allMetricsBaselineRecord.workload = [PSCustomObject]@{ self_test = "all-required-metrics" }
    $allMetricsBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    Update-BaselinesFromRecords -Baseline $allMetricsBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Records @($allMetricsBaselineRecord) -ReportRoot "self-test"

    foreach ($requiredMetricName in @($vegetationProfile.required_gpu_metrics)) {
        foreach ($statName in @("avg", "p95")) {
            $metricRegressionRecord = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
            $metricRegressionRecord.status = "PASS"
            $metricRegressionRecord.failures = @()
            $metricRegressionRecord.warnings = @()
            $metricRegressionRecord.baseline_deltas = @()
            $metricRegressionRecord.baseline_status = "NOT_COMPARED"
            $metric = Get-ProfileProperty $metricRegressionRecord.gpu_metric_summaries ([string]$requiredMetricName)
            $baselineValue = [double](Get-GpuMetricSummaryValue $metric $statName)
            $allowedIncrease = if ($requiredMetricName -eq "GPU.Frame") {
                if ($statName -eq "avg") { [Math]::Max($baselineValue * 0.05, 0.25) } else { [Math]::Max($baselineValue * 0.08, 0.50) }
            }
            elseif ($statName -eq "avg") {
                [Math]::Max($baselineValue * 0.08, 0.10)
            }
            else {
                [Math]::Max($baselineValue * 0.12, 0.20)
            }
            Set-ObjectProperty $metric $statName ($baselineValue + $allowedIncrease + 0.01)
            Compare-RecordToBaseline -Record $metricRegressionRecord -Baseline $allMetricsBaseline -ProfileConfig $vegetationProfile -Profile "VegetationFullPipeline" -Configuration "Release"
            Assert-SelfTest (
                $metricRegressionRecord.status -eq "WARN" -and
                @($metricRegressionRecord.warnings).Count -eq 1 -and
                (@($metricRegressionRecord.warnings) -join " ") -match ([regex]::Escape("$requiredMetricName $statName"))
            ) "Every required GPU metric avg/p95 regression must emit exactly one per-metric warning; failed for $requiredMetricName $statName."
        }
    }

    $tinyMetricName = "GPU.ToneMapAndOverlays"
    $tinyBaseline = $allMetricsBaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $tinyBaselineEntry = Get-BaselineEntry -Baseline $tinyBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
    $tinyBaselineMetric = Get-ProfileProperty $tinyBaselineEntry.gpu_metrics $tinyMetricName
    Set-ObjectProperty $tinyBaselineMetric "avg" 0.09
    Set-ObjectProperty $tinyBaselineMetric "p95" 0.10

    $tinyTailOnlyRecord = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $tinyTailOnlyMetric = Get-ProfileProperty $tinyTailOnlyRecord.gpu_metric_summaries $tinyMetricName
    Set-ObjectProperty $tinyTailOnlyMetric "avg" 0.11
    Set-ObjectProperty $tinyTailOnlyMetric "p95" 0.16
    Compare-RecordToBaseline -Record $tinyTailOnlyRecord -Baseline $tinyBaseline -ProfileConfig $vegetationProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    $tinyTailOnlyDelta = @($tinyTailOnlyRecord.baseline_deltas | Where-Object { $_.metric -eq "gpu.$tinyMetricName.p95" })
    Assert-SelfTest (
        $tinyTailOnlyRecord.status -eq "PASS" -and
        @($tinyTailOnlyRecord.warnings).Count -eq 0 -and
        $tinyTailOnlyDelta.Count -eq 1 -and
        $tinyTailOnlyDelta[0].status -eq "PASS" -and
        [bool]$tinyTailOnlyDelta[0].threshold_exceeded -and
        -not [bool]$tinyTailOnlyDelta[0].corroborated
    ) "Tiny-pass p95-only threshold excursions must remain visible without warning unless avg also exceeds its threshold."

    $tinyCorroboratedRecord = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $tinyCorroboratedMetric = Get-ProfileProperty $tinyCorroboratedRecord.gpu_metric_summaries $tinyMetricName
    Set-ObjectProperty $tinyCorroboratedMetric "avg" 0.13
    Set-ObjectProperty $tinyCorroboratedMetric "p95" 0.16
    Compare-RecordToBaseline -Record $tinyCorroboratedRecord -Baseline $tinyBaseline -ProfileConfig $vegetationProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    $tinyCorroboratedDelta = @($tinyCorroboratedRecord.baseline_deltas | Where-Object { $_.metric -eq "gpu.$tinyMetricName.p95" })
    Assert-SelfTest (
        $tinyCorroboratedRecord.status -eq "WARN" -and
        @($tinyCorroboratedRecord.warnings).Count -eq 2 -and
        $tinyCorroboratedDelta.Count -eq 1 -and
        $tinyCorroboratedDelta[0].status -eq "WARN" -and
        [bool]$tinyCorroboratedDelta[0].threshold_exceeded -and
        [bool]$tinyCorroboratedDelta[0].corroborated
    ) "Tiny-pass p95 threshold excursions must warn when avg independently corroborates the regression."

    $missingBaselineMetricDocument = $allMetricsBaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    (Get-BaselineEntry -Baseline $missingBaselineMetricDocument -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan").gpu_metrics.PSObject.Properties.Remove("GPU.GBuffer")
    $missingBaselineMetricRecord = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Compare-RecordToBaseline -Record $missingBaselineMetricRecord -Baseline $missingBaselineMetricDocument -ProfileConfig $vegetationProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($missingBaselineMetricRecord.status -eq "FAIL" -and (@($missingBaselineMetricRecord.failures) -join " ") -match "GPU.GBuffer.*baseline.*missing") "Missing baseline required GPU metrics must fail closed."

    $nonfiniteBaselineDocument = $allMetricsBaseline | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    (Get-BaselineEntry -Baseline $nonfiniteBaselineDocument -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan").gpu_metrics.'GPU.GBuffer'.avg = [double]::NaN
    $nonfiniteBaselineRecord = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Compare-RecordToBaseline -Record $nonfiniteBaselineRecord -Baseline $nonfiniteBaselineDocument -ProfileConfig $vegetationProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($nonfiniteBaselineRecord.status -eq "FAIL" -and (@($nonfiniteBaselineRecord.failures) -join " ") -match "GPU.GBuffer avg.*baseline.*finite") "Non-finite baseline required GPU metrics must fail closed."

    $missingTierProfile = $gateAProfile | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $missingTierProfile.comparison_thresholds.PSObject.Properties.Remove("gpu_pass_tiers")
    $missingTierRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    Compare-RecordToBaseline -Record $missingTierRecord -Baseline $comparableBaseline -ProfileConfig $missingTierProfile -Profile "VegetationFullPipeline" -Configuration "Release"
    Assert-SelfTest ($missingTierRecord.status -eq "FAIL" -and (@($missingTierRecord.failures) -join " ") -match "gpu_pass_tiers") "Gate A RED: missing or malformed GPU pass tiers must fail closed instead of silently skipping required metrics."

    foreach ($attributionMutation in @(
        { param($value) $value.gpu_adapter = "Other GPU" },
        { param($value) $value.gpu_driver = "Other Driver" },
        { param($value) $value.os_build = "10.0.99999.1" },
        { param($value) $value.workload_fingerprint = ("f" * 64) }
    )) {
        $notComparableRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        & $attributionMutation $notComparableRecord
        Compare-RecordToBaseline -Record $notComparableRecord -Baseline $comparableBaseline -ProfileConfig $gateAProfile -Profile "VegetationFullPipeline" -Configuration "Release"
        Assert-SelfTest ($notComparableRecord.status -eq "FAIL" -and $notComparableRecord.baseline_status -eq "NOT_COMPARABLE") "Gate A RED: adapter, driver, OS build, and workload fingerprint mismatches must be NOT_COMPARABLE + FAIL."
    }

    $blessRoundTripRecord = $comparableRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
    $blessRoundTripRecord.status = "PASS"
    $blessRoundTripRecord.tree_termination_confirmed = $true
    $blessRoundTripRecord.job_cleanup_confirmed = $true
    $blessRoundTripBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
    Update-BaselinesFromRecords -Baseline $blessRoundTripBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Records @($blessRoundTripRecord) -ReportRoot "self-test"
    $blessRoundTripEntry = Get-BaselineEntry -Baseline $blessRoundTripBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
    $blessAttribution = Get-ProfileProperty $blessRoundTripEntry "attribution"
    Assert-SelfTest (
        $null -ne $blessAttribution -and
        $blessAttribution.adapter -eq $blessRoundTripRecord.gpu_adapter -and
        $blessAttribution.driver -eq $blessRoundTripRecord.gpu_driver -and
        $blessAttribution.os_build -eq $blessRoundTripRecord.os_build -and
        $blessAttribution.source_sha -eq $blessRoundTripRecord.source_sha -and
        $blessRoundTripEntry.workload_fingerprint -eq $blessRoundTripRecord.workload_fingerprint -and
        $null -ne $blessRoundTripEntry.workload
    ) "Gate A RED: baseline bless must persist adapter, driver, OS build, source SHA, workload fingerprint, and readable workload fields."

    $reportImportRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-report-import-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
    try {
        $scenePath = Join-Path $reportImportRoot "product/assets/scenes/VegetationBaseline.scene.json"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $scenePath) | Out-Null
        '{ "self_test": true }' | Set-Content -LiteralPath $scenePath -Encoding UTF8
        $candidateRoot = Join-Path $reportImportRoot "Intermediate/test-reports/perf-gate/candidate"
        New-Item -ItemType Directory -Force -Path $candidateRoot | Out-Null
        $candidateSummaryPath = Join-Path $candidateRoot "summary.json"
        $candidateBaselinePath = Join-Path $reportImportRoot "tools/perf/perf_gate_baselines.json"
        $candidateIdentity = New-PerfGateWorkloadIdentity -ProfileConfig $vegetationProfile -RepoRoot $reportImportRoot
        $candidateSourceSha = ("9" * 40)

        $candidateVulkan = $allMetricsBaselineRecord | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        $candidateVulkan.root_exited = $true
        $candidateVulkan.job_kill_on_close = $true
        $candidateVulkan.job_assigned = $true
        $candidateVulkan.job_active_processes_after_cleanup = 0
        $candidateVulkan.exit_code = 0
        $candidateVulkan.frames_sampled = 100
        $candidateVulkan.gpu_coverage = 1.0
        $candidateVulkan.gpu_submitted = 100
        $candidateVulkan.gpu_resolved = 100
        $candidateVulkan.gpu_valid = 100
        $candidateVulkan.gpu_invalid = 0
        $candidateVulkan.source_sha = $candidateSourceSha
        $candidateVulkan.workload_fingerprint = $candidateIdentity.fingerprint
        $candidateVulkan.workload = $candidateIdentity.workload
        $candidateVulkan.actual_width = [int]$vegetationProfile.window_width
        $candidateVulkan.actual_height = [int]$vegetationProfile.window_height
        $candidateVulkan.extent_stable = $true
        $candidateVulkan.validation = [bool]$vegetationProfile.validation
        $candidateVulkan.vsync = [bool]$vegetationProfile.vsync
        $candidateVulkan.fixed_camera = [bool]$vegetationProfile.fixed_camera
        $candidateVulkan.frame_cap = [string]$vegetationProfile.frame_cap
        $candidateVulkan.telemetry_mode = "Profile"
        $candidateVulkan.gpu_baseline_comparable = $true
        $candidateVulkan.baseline_status = "MISSING"
        $candidateVulkan.failures = @()
        $candidateVulkan.warnings = @()

        $candidateDx12 = $candidateVulkan | ConvertTo-Json -Depth 16 | ConvertFrom-Json
        $candidateDx12.backend = "DX12"
        $candidateDx12.gpu_driver = "SelfTest DX12 Driver"
        $candidateSummary = [PSCustomObject][ordered]@{
            schema_version = 2
            profile = "VegetationFullPipeline"
            profile_source = "catalog"
            configuration = "Release"
            telemetry_mode = "Profile"
            gpu_baseline_comparable = $true
            status = "PASS"
            baseline_path = $candidateBaselinePath
            baseline_blessed = $false
            report_root = $candidateRoot
            runs = @($candidateVulkan, $candidateDx12)
        }
        Write-PerfGateJsonFile -InputObject $candidateSummary -LiteralPath $candidateSummaryPath -Depth 16
        $candidateSummarySha = Get-PerfGateFileSha256 -Path $candidateSummaryPath

        $importBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
        $importResult = Import-PerfGateBaselineFromReport `
            -Baseline $importBaseline `
            -BaselinePath $candidateBaselinePath `
            -Profile "VegetationFullPipeline" `
            -Configuration "Release" `
            -ProfileConfig $vegetationProfile `
            -RepoRoot $reportImportRoot `
            -ReportPath $candidateSummaryPath `
            -ExpectedReportSha256 $candidateSummarySha `
            -CurrentSourceSha $candidateSourceSha
        $importedVulkan = Get-BaselineEntry -Baseline $importResult.baseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan"
        $importedDx12 = Get-BaselineEntry -Baseline $importResult.baseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "DX12"
        Assert-SelfTest (
            $null -ne $importedVulkan -and
            $null -ne $importedDx12 -and
            $importedVulkan.source_report_sha256 -eq $candidateSummarySha -and
            $importedDx12.source_report_sha256 -eq $candidateSummarySha
        ) "Protected report import must produce both baseline entries and persist the approved report SHA-256."

        $hashMismatchBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
        Assert-SelfTestThrows {
            Import-PerfGateBaselineFromReport -Baseline $hashMismatchBaseline -BaselinePath $candidateBaselinePath -Profile "VegetationFullPipeline" -Configuration "Release" -ProfileConfig $vegetationProfile -RepoRoot $reportImportRoot -ReportPath $candidateSummaryPath -ExpectedReportSha256 ("0" * 64) -CurrentSourceSha $candidateSourceSha | Out-Null
        } "SHA-256.*mismatch" "Protected report import must reject a report whose bytes do not match the approved SHA-256."
        Assert-SelfTest (@($hashMismatchBaseline.baselines.PSObject.Properties).Count -eq 0) "Report hash rejection must not mutate the baseline object."

        $sourceMismatchBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
        Assert-SelfTestThrows {
            Import-PerfGateBaselineFromReport -Baseline $sourceMismatchBaseline -BaselinePath $candidateBaselinePath -Profile "VegetationFullPipeline" -Configuration "Release" -ProfileConfig $vegetationProfile -RepoRoot $reportImportRoot -ReportPath $candidateSummaryPath -ExpectedReportSha256 $candidateSummarySha -CurrentSourceSha ("8" * 40) | Out-Null
        } "source SHA.*current" "Protected report import must reject candidate evidence from a different tool commit."
        Assert-SelfTest (@($sourceMismatchBaseline.baselines.PSObject.Properties).Count -eq 0) "Report source rejection must not mutate the baseline object."

        $outsideSummaryPath = Join-Path $reportImportRoot "outside-summary.json"
        Copy-Item -LiteralPath $candidateSummaryPath -Destination $outsideSummaryPath
        $outsideSummarySha = Get-PerfGateFileSha256 -Path $outsideSummaryPath
        Assert-SelfTestThrows {
            Import-PerfGateBaselineFromReport -Baseline $importBaseline -BaselinePath $candidateBaselinePath -Profile "VegetationFullPipeline" -Configuration "Release" -ProfileConfig $vegetationProfile -RepoRoot $reportImportRoot -ReportPath $outsideSummaryPath -ExpectedReportSha256 $outsideSummarySha -CurrentSourceSha $candidateSourceSha | Out-Null
        } "inside.*Intermediate.*perf-gate" "Protected report import must reject evidence outside the repository report root."

        $unsafeSummaryCases = @(
            [PSCustomObject]@{
                name = "already-blessed"
                expected = "unblessed PASS"
                mutate = { param($value) $value.baseline_blessed = $true }
            },
            [PSCustomObject]@{
                name = "warning"
                expected = "warnings.*empty"
                mutate = { param($value) $value.runs[0].warnings = @("self-test warning") }
            },
            [PSCustomObject]@{
                name = "warnings-property-missing"
                expected = "warnings.*empty"
                mutate = { param($value) $value.runs[0].PSObject.Properties.Remove("warnings") }
            },
            [PSCustomObject]@{
                name = "missing-required-metric"
                expected = "required GPU metrics.*current profile"
                mutate = { param($value) $value.runs[0].required_gpu_metrics = @($value.runs[0].required_gpu_metrics | Select-Object -Skip 1) }
            },
            [PSCustomObject]@{
                name = "incomplete-cleanup"
                expected = "cleanup"
                mutate = { param($value) $value.runs[0].job_cleanup_confirmed = $false }
            },
            [PSCustomObject]@{
                name = "identity-disabled"
                expected = "identity.*required"
                mutate = { param($value) $value.runs[0].baseline_identity_required = $false }
            },
            [PSCustomObject]@{
                name = "readable-workload-mismatch"
                expected = "readable workload.*current profile"
                mutate = { param($value) $value.runs[0].workload.scene = "product/assets/scenes/not-approved.scene.json" }
            },
            [PSCustomObject]@{
                name = "invalid-cpu-metric"
                expected = "CPU/memory/draw metrics"
                mutate = { param($value) $value.runs[0].cpu_frame_time_avg_ms = "NaN" }
            }
        )
        foreach ($unsafeCase in $unsafeSummaryCases) {
            $unsafeSummary = $candidateSummary | ConvertTo-Json -Depth 16 | ConvertFrom-Json
            & $unsafeCase.mutate $unsafeSummary
            $unsafePath = Join-Path $candidateRoot ("summary-{0}.json" -f $unsafeCase.name)
            Write-PerfGateJsonFile -InputObject $unsafeSummary -LiteralPath $unsafePath -Depth 16
            $unsafeSha = Get-PerfGateFileSha256 -Path $unsafePath
            $unsafeBaseline = ConvertFrom-Json '{ "schema_version": 1, "baselines": {} }'
            Assert-SelfTestThrows {
                Import-PerfGateBaselineFromReport -Baseline $unsafeBaseline -BaselinePath $candidateBaselinePath -Profile "VegetationFullPipeline" -Configuration "Release" -ProfileConfig $vegetationProfile -RepoRoot $reportImportRoot -ReportPath $unsafePath -ExpectedReportSha256 $unsafeSha -CurrentSourceSha $candidateSourceSha | Out-Null
            } $unsafeCase.expected "Protected report import must reject unsafe '$($unsafeCase.name)' evidence."
            Assert-SelfTest (@($unsafeBaseline.baselines.PSObject.Properties).Count -eq 0) "Unsafe '$($unsafeCase.name)' evidence must not mutate the baseline object."
        }

        Assert-SelfTestThrows {
            Assert-PerfGateOptions -BlessBaseline $false -DryRun $false -TelemetryMode "Profile" -BlessBaselineFromReport $candidateSummaryPath -ExpectedReportSha256 ""
        } "ExpectedReportSha256.*required" "Report import must require an approved SHA-256."
        Assert-SelfTestThrows {
            Assert-PerfGateOptions -BlessBaseline $true -DryRun $false -TelemetryMode "Profile" -BlessBaselineFromReport $candidateSummaryPath -ExpectedReportSha256 $candidateSummarySha
        } "cannot be used with -BlessBaseline" "Report import and live bless must be mutually exclusive."
        Assert-SelfTestThrows {
            Assert-PerfGateOptions -BlessBaseline $false -DryRun $false -TelemetryMode "Profile" -BlessBaselineFromReport "" -ExpectedReportSha256 $candidateSummarySha
        } "BlessBaselineFromReport.*required" "An approved SHA-256 without a report path must be rejected."

        $atomicBaselinePath = Join-Path $candidateRoot "atomic-baseline.json"
        Write-PerfGateJsonFile -InputObject ([PSCustomObject]@{ old = $true }) -LiteralPath $atomicBaselinePath
        Write-PerfGateJsonFileAtomically -InputObject $importResult.baseline -LiteralPath $atomicBaselinePath -Depth 16
        $atomicBaseline = Get-Content -LiteralPath $atomicBaselinePath -Raw | ConvertFrom-Json
        Assert-SelfTest (
            $null -ne (Get-BaselineEntry -Baseline $atomicBaseline -Profile "VegetationFullPipeline" -Configuration "Release" -Target "Sandbox" -Backend "Vulkan") -and
            @(Get-ChildItem -LiteralPath $candidateRoot -Filter ".atomic-baseline.json.*.tmp").Count -eq 0
        ) "Protected report import must publish the validated baseline atomically and remove its temporary file."
    }
    finally {
        Remove-Item -LiteralPath $reportImportRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "RunPerfGate self-test PASS"
}

if ($SelfTest) {
    Invoke-RunPerfGateSelfTest
    exit 0
}

$repoRoot = Get-RepoRoot
$baselinePath = if ([string]::IsNullOrWhiteSpace($BaselinePath)) { Join-Path $repoRoot "tools/perf/perf_gate_baselines.json" } else { $BaselinePath }
if (-not [System.IO.Path]::IsPathRooted($baselinePath)) {
    $baselinePath = Join-Path $repoRoot $baselinePath
}
$baselinePath = [System.IO.Path]::GetFullPath($baselinePath)
$profilesPath = Join-Path $repoRoot "tools/perf/perf_gate_profiles.json"
$profileCatalog = Import-PerfGateProfileCatalog -ProfilesPath $profilesPath -BaselinePath $baselinePath
$baseline = $profileCatalog.baseline
$profileConfig = Get-PerfGateProfileConfig -Catalog $profileCatalog -Profile $Profile
$Configuration = Resolve-PerfGateConfiguration -ProfileConfig $profileConfig -RequestedConfiguration $Configuration
Assert-PerfGateOptions `
    -BlessBaseline ([bool]$BlessBaseline) `
    -DryRun ([bool]$DryRun) `
    -TelemetryMode $TelemetryMode `
    -BlessBaselineFromReport $BlessBaselineFromReport `
    -ExpectedReportSha256 $ExpectedReportSha256
Assert-PerfGateComparisonProfileContract -ProfileConfig $profileConfig

if (-not [string]::IsNullOrWhiteSpace($BlessBaselineFromReport)) {
    $importReportPath = if ([System.IO.Path]::IsPathRooted($BlessBaselineFromReport)) {
        [System.IO.Path]::GetFullPath($BlessBaselineFromReport)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BlessBaselineFromReport))
    }
    $importResult = Import-PerfGateBaselineFromReport `
        -Baseline $baseline `
        -BaselinePath $baselinePath `
        -Profile $Profile `
        -Configuration $Configuration `
        -ProfileConfig $profileConfig `
        -RepoRoot $repoRoot `
        -ReportPath $importReportPath `
        -ExpectedReportSha256 $ExpectedReportSha256 `
        -CurrentSourceSha (Get-PerfGateSourceSha -RepoRoot $repoRoot)
    Write-PerfGateJsonFileAtomically -InputObject $importResult.baseline -LiteralPath $baselinePath -Depth 16
    Write-Host "Baseline imported from approved report: $($importResult.report_path)"
    Write-Host "Approved report SHA-256: $($importResult.report_sha256)"
    Write-Host "Baseline updated: $baselinePath"
    exit 0
}

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
Set-PerfGateRunIdentity `
    -Records $records `
    -ProfileConfig $profileConfig `
    -RepoRoot $repoRoot

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

    $existingRunLogPaths = @(Get-EngineLogFiles -RepoRoot $repoRoot | ForEach-Object { $_.FullName })
    $matrixMayContinue = Invoke-GateProcess `
        -Record $record `
        -RunDirectory (Split-Path -Parent $record.executable) `
        -Arguments $record.arguments `
        -TimeoutSeconds ([double]$profileConfig.timeout_seconds)

    $runLogs = Get-RunLogFiles -RepoRoot $repoRoot -ExistingPaths $existingRunLogPaths
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
    Write-PerfGateJsonFile -InputObject $baseline -LiteralPath $baselinePath -Depth 16
    $summary.baseline_blessed = $true
}
Write-PerfGateJsonFile -InputObject $summary -LiteralPath (Join-Path $reportRoot "summary.json") -Depth 16

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
$markdown += ""
$markdown += "## Comparison identity"
$markdown += ""
$identityRows = New-Object 'System.Collections.Generic.List[object[]]'
foreach ($record in $records) {
    $workloadText = if ($null -eq $record.workload) { "n/a" } else { $record.workload | ConvertTo-Json -Depth 8 -Compress }
    $fingerprintText = if ([string]::IsNullOrWhiteSpace([string]$record.workload_fingerprint)) { "n/a" } else { [string]$record.workload_fingerprint }
    $identityRows.Add([object[]]@(
        $record.target,
        $record.backend,
        $record.gpu_adapter,
        $record.gpu_driver,
        $record.os_build,
        $record.source_sha,
        $fingerprintText,
        $workloadText
    )) | Out-Null
}
$markdown += New-MarkdownTable `
    -Headers @("Target", "Backend", "Adapter", "Driver", "OS build", "Source SHA", "Workload fingerprint", "Workload") `
    -Alignments @("Left", "Left", "Left", "Left", "Left", "Left", "Left", "Left") `
    -Rows $identityRows
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
