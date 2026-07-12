[CmdletBinding()]
param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [string]$BaselinePath = "",
    [switch]$SkipBuild,
    [switch]$DryRun,
    [switch]$BlessBaseline,
    [switch]$SelfTest
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

function Quote-Argument {
    param([string]$Value)

    if ($null -eq $Value) {
        return '""'
    }
    if ($Value.Length -eq 0) {
        return '""'
    }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Join-Arguments {
    param([string[]]$Arguments)

    $quoted = @()
    foreach ($argument in $Arguments) {
        $quoted += Quote-Argument $argument
    }
    return ($quoted -join " ")
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

function Set-EngineBackend {
    param(
        [string]$ConfigPath,
        [string]$Backend
    )

    $lines = Get-Content -LiteralPath $ConfigPath
    $output = New-Object System.Collections.Generic.List[string]
    $inRhi = $false
    $updated = $false

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            $inRhi = ($matches[1].Trim().ToLowerInvariant() -eq "rhi")
            $output.Add($line)
            continue
        }
        if ($inRhi -and $line -match '^\s*Backend\s*=') {
            $output.Add("Backend=$Backend")
            $updated = $true
            continue
        }
        $output.Add($line)
    }

    if (-not $updated) {
        $output.Add("")
        $output.Add("[RHI]")
        $output.Add("Backend=$Backend")
    }

    Set-Content -LiteralPath $ConfigPath -Value $output -Encoding UTF8
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
        executable = $Executable
        telemetry = $TelemetryPath
        process_log = $ProcessLogPath
        process_error_log = $ProcessErrorLogPath
        engine_logs = @()
        exit_code = $null
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
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_avg_ms" "CPU Avg" $Record.cpu_frame_time_avg_ms (Get-ProfileProperty $entry "cpu_frame_time_avg_ms") ([double]$ProfileConfig.warn_thresholds.cpu_frame_time_avg_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_p95_ms" "CPU P95" $Record.cpu_frame_time_p95_ms (Get-ProfileProperty $entry "cpu_frame_time_p95_ms") ([double]$ProfileConfig.warn_thresholds.cpu_frame_time_p95_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "cpu_frame_time_p99_ms" "CPU P99" $Record.cpu_frame_time_p99_ms (Get-ProfileProperty $entry "cpu_frame_time_p99_ms") ([double]$ProfileConfig.warn_thresholds.cpu_frame_time_p99_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "process_private_bytes_peak_mb" "Private MB" $Record.process_private_bytes_peak_mb (Get-ProfileProperty $entry "process_private_bytes_peak_mb") ([double]$ProfileConfig.warn_thresholds.private_bytes_peak_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "engine_heap_peak_mb" "Heap MB" $Record.engine_heap_peak_mb (Get-ProfileProperty $entry "engine_heap_peak_mb") ([double]$ProfileConfig.warn_thresholds.engine_heap_peak_percent))
    Add-BaselineDelta $Record (New-BaselineDelta "draw_calls_avg" "Draw Calls" $Record.draw_calls_avg (Get-ProfileProperty $entry "draw_calls_avg") ([double]$ProfileConfig.warn_thresholds.draw_call_count_percent))
}

function New-BaselineEntry {
    param(
        [object]$Record,
        [string]$ReportRoot
    )

    return [PSCustomObject][ordered]@{
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
}

function Update-BaselinesFromRecords {
    param(
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [object[]]$Records,
        [string]$ReportRoot
    )

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

function Invoke-GateProcess {
    param(
        [object]$Record,
        [string]$RunDirectory,
        [string[]]$Arguments,
        [double]$TimeoutSeconds
    )

    $processLogDirectory = Split-Path -Parent $Record.process_log
    New-Item -ItemType Directory -Force -Path $processLogDirectory | Out-Null

    New-Item -ItemType File -Force -Path $Record.process_log | Out-Null
    New-Item -ItemType File -Force -Path $Record.process_error_log | Out-Null

    $commandLine = "{0} {1} > {2} 2> {3}" -f `
        (Quote-Argument $Record.executable), `
        (Join-Arguments $Arguments), `
        (Quote-Argument $Record.process_log), `
        (Quote-Argument $Record.process_error_log)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = "/d /s /c `"$commandLine`""
    $psi.WorkingDirectory = $RunDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    Set-SanitizedPathEnvironment $psi

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi

    $timeoutMilliseconds = [Math]::Max(1, [int]([double]$TimeoutSeconds * 1000.0))
    try {
        if (-not $process.Start()) {
            Add-Failure $Record "Failed to start process"
            return
        }

        if (-not $process.WaitForExit($timeoutMilliseconds)) {
            try {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
            }
            catch {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
            Add-Failure $Record "Process timed out after $TimeoutSeconds seconds"
            return
        }

        $process.WaitForExit()
        $process.Refresh()
        $exitCode = $process.ExitCode
        $Record.exit_code = $exitCode
        if ($exitCode -ne 0) {
            Add-Failure $Record "Process exited with code $exitCode"
        }
    }
    finally {
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

    if ($telemetry.schema_version -ne 1) {
        Add-Failure $Record "Unsupported telemetry schema_version: $($telemetry.schema_version)"
    }
    if ($telemetry.backend_actual -ne $Record.backend) {
        Add-Failure $Record "Backend mismatch: requested $($Record.backend), actual $($telemetry.backend_actual)"
    }
    if ([int64]$telemetry.frames_sampled -le 0) {
        Add-Failure $Record "No sampled frames were reported"
    }

    $Record.frames_sampled = [int64]$telemetry.frames_sampled
    $Record.cpu_frame_time_avg_ms = [double]$telemetry.cpu_frame_time_ms.avg
    $Record.cpu_frame_time_p95_ms = [double]$telemetry.cpu_frame_time_ms.p95
    $Record.cpu_frame_time_p99_ms = [double]$telemetry.cpu_frame_time_ms.p99
    $Record.process_private_bytes_peak_mb = [double]$telemetry.memory.process_private_bytes_peak_mb
    $Record.engine_heap_peak_mb = [double]$telemetry.memory.engine_heap_peak_mb
    $Record.draw_calls_avg = [double]$telemetry.render_stats.draw_calls_avg

    if ([int64]$telemetry.memory.engine_heap_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "Engine heap live bytes at shutdown: $($telemetry.memory.engine_heap_shutdown_live_bytes)"
    }
    if ([bool]$telemetry.memory.gpu_allocator_supported -and [int64]$telemetry.memory.gpu_allocator_shutdown_live_bytes -ne 0) {
        Add-Failure $Record "GPU allocator live bytes at shutdown: $($telemetry.memory.gpu_allocator_shutdown_live_bytes)"
    }

    $targetKey = $Record.target.ToLowerInvariant()
    $capName = "{0}_private_bytes_mb" -f $targetKey
    $privateBytesCap = Get-ProfileProperty $ProfileConfig.absolute_caps $capName
    if ($null -ne $privateBytesCap -and $Record.process_private_bytes_peak_mb -gt [double]$privateBytesCap) {
        Add-Failure $Record "Private bytes peak $($Record.process_private_bytes_peak_mb) MB exceeded cap $privateBytesCap MB"
    }

    if ($Record.status -eq "NOT_RUN") {
        $Record.status = "PASS"
    }

    if ($Record.status -ne "FAIL") {
        Compare-RecordToBaseline -Record $Record -Baseline $Baseline -ProfileConfig $ProfileConfig -Profile $Profile -Configuration $Configuration
    }
}

function Invoke-RunPerfGateSelfTest {
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
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
$profileConfig = Get-ProfileProperty $baseline.profiles $Profile
if ($null -eq $profileConfig) {
    throw "Unknown perf gate profile '$Profile'."
}
if ([string]::IsNullOrWhiteSpace($Configuration)) {
    $Configuration = $profileConfig.configuration
}
if ($BlessBaseline -and $DryRun) {
    throw "-BlessBaseline cannot be used with -DryRun."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/perf-gate/$timestamp"
$buildLogRoot = Join-Path $reportRoot "build"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

if (-not $SkipBuild -and -not $DryRun) {
    Invoke-BatchCommand -RepoRoot $repoRoot -CommandLine "build_sandbox.bat $Configuration x64" -LogPath (Join-Path $buildLogRoot "build_sandbox.log")
    Invoke-BatchCommand -RepoRoot $repoRoot -CommandLine "build_editor.bat $Configuration x64" -LogPath (Join-Path $buildLogRoot "build_editor.log")
}

$engineConfig = Join-Path $repoRoot "product/config/Engine.ini"
$engineConfigBackup = Join-Path $reportRoot "Engine.ini.backup"
Copy-Item -LiteralPath $engineConfig -Destination $engineConfigBackup -Force

$records = @()
try {
    foreach ($target in @($profileConfig.targets)) {
        foreach ($backend in @($target.backends)) {
            $targetName = [string]$target.target
            $exeName = if ($targetName -eq "Editor") { "Editor.exe" } else { "Sandbox.exe" }
            $runDir = Join-Path $repoRoot "product/bin64/$Configuration-windows-x86_64"
            $exePath = Join-Path $runDir $exeName
            $runName = "{0}-{1}" -f $targetName, $backend
            $telemetryPath = Join-Path $reportRoot "$runName.json"
            $processLogPath = Join-Path $reportRoot "$runName.stdout.log"
            $processErrorLogPath = Join-Path $reportRoot "$runName.stderr.log"
            $record = New-RunRecord $targetName $backend $exePath $telemetryPath $processLogPath $processErrorLogPath
            $records += $record

            if (-not (Test-Path -LiteralPath $exePath)) {
                Add-Failure $record "Missing executable: $exePath"
                continue
            }

            if ($DryRun) {
                $record.status = "DRY_RUN"
                continue
            }

            Set-EngineBackend -ConfigPath $engineConfig -Backend $backend
            $runStart = Get-Date
            $arguments = @(
                "--perf-gate",
                "--perf-gate-profile=$Profile",
                "--perf-gate-output=$telemetryPath",
                "--perf-gate-target=$targetName",
                "--perf-gate-warmup-seconds=$($profileConfig.warmup_seconds)",
                "--perf-gate-sample-seconds=$($profileConfig.sample_seconds)",
                "--run-for-seconds=$([double]$profileConfig.warmup_seconds + [double]$profileConfig.sample_seconds + 10.0)"
            )
            Invoke-GateProcess -Record $record -RunDirectory $runDir -Arguments $arguments -TimeoutSeconds ([double]$profileConfig.timeout_seconds)

            $runLogs = Get-RunLogFiles -RepoRoot $repoRoot -Since $runStart
            Test-LogForDiagnostics -Record $record -LogFiles $runLogs

            if ($record.status -ne "FAIL") {
                Test-Telemetry -Record $record -ProfileConfig $profileConfig -Baseline $baseline -Profile $Profile -Configuration $Configuration
            }
        }
    }
}
finally {
    Copy-Item -LiteralPath $engineConfigBackup -Destination $engineConfig -Force
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
    schema_version = 1
    profile = $Profile
    configuration = $Configuration
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
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# AshEngine Perf Gate Summary"
$markdown += ""
$markdown += "Status: $overall"
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
    $summaryRows.Add([object[]]@(
        $record.target,
        $record.backend,
        $record.status,
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
        $failureText,
        $warningText
    )) | Out-Null
}
$markdown += New-MarkdownTable `
    -Headers @("Target", "Backend", "Status", "Frames", "CPU Avg ms", "CPU Avg delta", "CPU P95 ms", "CPU P95 delta", "CPU P99 delta", "Private MB", "Private delta", "Heap MB", "Heap delta", "Draw delta", "Failures", "Warnings") `
    -Alignments @("Left", "Left", "Left", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Left", "Left") `
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
