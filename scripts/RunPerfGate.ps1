[CmdletBinding()]
param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [string]$BaselinePath = "",
    [string]$Scenario = "",
    [switch]$SkipBuild,
    [switch]$DryRun,
    [switch]$BlessBaseline,
    [switch]$NoTracy,
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

function Get-MSBuildPath {
    $command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $candidates = @(& $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath $candidate) {
                return [string]$candidate
            }
        }
    }

    throw "MSBuild.exe was not found."
}

function New-NoTracyCommandPlan {
    param([string]$MSBuildPath)

    $cleanCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/InvokeMSBuild.ps1 -MSBuildPath $(Quote-Argument $MSBuildPath) -SolutionPath AshEngine.sln -Target Clean -Configuration Release -Platform x64"
    return @(
        [PSCustomObject]@{ phase = "NoTracyBuild"; kind = "Command"; command_line = "premake5.exe --no-tracy vs2022"; log_name = "generate_no_tracy.log" },
        [PSCustomObject]@{ phase = "NoTracyBuild"; kind = "Command"; command_line = $cleanCommand; log_name = "clean_no_tracy.log" },
        [PSCustomObject]@{ phase = "NoTracyBuild"; kind = "Command"; command_line = "build_editor.bat Release x64"; log_name = "build_editor_no_tracy.log" },
        [PSCustomObject]@{ phase = "PerfRuns"; kind = "Run"; command_line = "Editor Vulkan Empty Release"; log_name = ""; target = "Editor"; backend = "Vulkan"; scenario = "Empty"; configuration = "Release" },
        [PSCustomObject]@{ phase = "PerfRuns"; kind = "Run"; command_line = "Editor DX12 Empty Release"; log_name = ""; target = "Editor"; backend = "DX12"; scenario = "Empty"; configuration = "Release" },
        [PSCustomObject]@{ phase = "RestoreStandard"; kind = "Command"; command_line = "premake5.exe vs2022"; log_name = "restore_generate_standard.log" },
        [PSCustomObject]@{ phase = "RestoreStandard"; kind = "Command"; command_line = $cleanCommand; log_name = "restore_clean_standard.log" },
        [PSCustomObject]@{ phase = "RestoreStandard"; kind = "Command"; command_line = "build_editor.bat Release x64"; log_name = "restore_build_editor_standard.log" }
    )
}

function Get-NoTracyGateTargetsFromPlan {
    param([object[]]$Plan)

    $runSteps = @($Plan | Where-Object { $_.phase -eq "PerfRuns" -and $_.kind -eq "Run" })
    if ($runSteps.Count -ne 2) {
        throw "No-Tracy plan must contain exactly two run descriptors."
    }
    foreach ($runStep in $runSteps) {
        if ($runStep.target -ne "Editor" -or $runStep.scenario -ne "Empty" -or $runStep.configuration -ne "Release" -or
            $runStep.backend -notin @("Vulkan", "DX12")) {
            throw "No-Tracy plan run descriptors must be Editor Vulkan/DX12 Empty Release."
        }
    }
    if ($runSteps[0].backend -ne "Vulkan" -or $runSteps[1].backend -ne "DX12") {
        throw "No-Tracy plan backend order must be Vulkan then DX12."
    }
    return [PSCustomObject]@{
        target = "Editor"
        backends = @($runSteps | ForEach-Object { $_.backend })
    }
}

function Invoke-NoTracyCommandPhase {
    param(
        [object[]]$Plan,
        [string]$Phase,
        [string]$RepoRoot,
        [string]$BuildLogRoot,
        [switch]$ContinueAfterFailure,
        [scriptblock]$CommandInvoker
    )

    $failures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($step in @($Plan | Where-Object { $_.phase -eq $Phase -and $_.kind -eq "Command" })) {
        try {
            if ($null -ne $CommandInvoker) {
                & $CommandInvoker $step
            }
            else {
                Invoke-BatchCommand -RepoRoot $RepoRoot -CommandLine $step.command_line -LogPath (Join-Path $BuildLogRoot $step.log_name)
            }
        }
        catch {
            if (-not $ContinueAfterFailure) {
                throw
            }
            $failures.Add($_.Exception.Message) | Out-Null
        }
    }

    if ($failures.Count -gt 0) {
        throw "Standard Tracy build restoration failed: $($failures -join ' | ')"
    }
}

function New-PerfGateRunId {
    return ("{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fffffff"), [guid]::NewGuid().ToString("N").Substring(0, 8))
}

function Invoke-WithRequiredRestore {
    param(
        [scriptblock]$Body,
        [scriptblock]$Restore
    )

    $bodyOutput = @()
    $bodyFailure = $null
    $restoreFailure = $null
    try {
        $bodyOutput = @(& $Body)
    }
    catch {
        $bodyFailure = $_
    }
    try {
        $null = @(& $Restore)
    }
    catch {
        $restoreFailure = $_
    }

    if ($null -ne $bodyFailure -and $null -ne $restoreFailure) {
        throw "Required workflow and restoration both failed. Body: $($bodyFailure.Exception.Message) | Restore: $($restoreFailure.Exception.Message)"
    }
    if ($null -ne $bodyFailure) {
        throw $bodyFailure.Exception
    }
    if ($null -ne $restoreFailure) {
        throw $restoreFailure.Exception
    }
    return $bodyOutput
}

function Assert-NoTracyRunRecords {
    param([object[]]$Records)

    $expected = @(
        [PSCustomObject]@{ target = "Editor"; backend = "Vulkan" },
        [PSCustomObject]@{ target = "Editor"; backend = "DX12" }
    )
    if ($Records.Count -ne $expected.Count) {
        throw "No-Tracy proof must produce exactly Editor/Vulkan then Editor/DX12 run records."
    }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if ($Records[$index].target -ne $expected[$index].target -or
            $Records[$index].backend -ne $expected[$index].backend) {
            throw "No-Tracy proof must produce exactly Editor/Vulkan then Editor/DX12 run records."
        }
        if ($Records[$index].status -notin @("PASS", "WARN", "FAIL")) {
            throw "No-Tracy proof records must have terminal PASS/WARN/FAIL status."
        }
    }
}

function Get-PerfGateOverallStatus {
    param([object[]]$Records)

    if ($null -eq $Records -or $Records.Count -eq 0) {
        return "FAIL"
    }

    $statuses = @()
    foreach ($record in $Records) {
        $status = [string](Get-ProfileProperty $record "status")
        if ($status -notin @("PASS", "WARN", "FAIL", "DRY_RUN")) {
            return "FAIL"
        }
        $statuses += $status
    }
    if ($statuses -contains "FAIL") {
        return "FAIL"
    }
    if ($statuses -contains "WARN") {
        return "WARN"
    }
    if ($statuses -contains "DRY_RUN") {
        return "DRY_RUN"
    }
    return "PASS"
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

function Test-IsJsonNativeNumber {
    param([object]$Value)

    return $Value -is [byte] -or $Value -is [sbyte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64] -or
        $Value -is [single] -or $Value -is [double] -or $Value -is [decimal]
}

function Read-JsonNumberField {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Record,
        [string]$Path,
        [switch]$Int64,
        [switch]$NonNegative
    )

    $result = [PSCustomObject]@{ valid = $false; value = 0.0; int64_value = 0L }
    $rawValue = Get-ProfileProperty $Object $Name
    $description = if ($Int64) {
        if ($NonNegative) { "a non-negative Int64 JSON integer" } else { "an Int64 JSON integer" }
    }
    else {
        if ($NonNegative) { "a finite non-negative JSON number" } else { "a finite JSON number" }
    }

    if (-not (Test-IsJsonNativeNumber $rawValue)) {
        Add-Failure $Record "$Path must be $description"
        return $result
    }

    $doubleValue = [double]$rawValue
    if ([double]::IsNaN($doubleValue) -or [double]::IsInfinity($doubleValue)) {
        Add-Failure $Record "$Path must be $description"
        return $result
    }

    if ($Int64) {
        try {
            $decimalValue = [decimal]$rawValue
        }
        catch {
            Add-Failure $Record "$Path must be $description"
            return $result
        }
        if ($decimalValue -ne [decimal]::Truncate($decimalValue) -or
            $decimalValue -lt [decimal][int64]::MinValue -or
            $decimalValue -gt [decimal][int64]::MaxValue -or
            ($NonNegative -and $decimalValue -lt 0)) {
            Add-Failure $Record "$Path must be $description"
            return $result
        }
        $result.valid = $true
        $result.int64_value = [int64]$decimalValue
        $result.value = [double]$result.int64_value
        return $result
    }

    if ($NonNegative -and $doubleValue -lt 0.0) {
        Add-Failure $Record "$Path must be $description"
        return $result
    }
    $result.valid = $true
    $result.value = $doubleValue
    return $result
}

function Read-JsonBooleanField {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Record,
        [string]$Path
    )

    $rawValue = Get-ProfileProperty $Object $Name
    $result = [PSCustomObject]@{ valid = $false; value = $false }
    if ($rawValue -isnot [bool]) {
        Add-Failure $Record "$Path must be a JSON boolean"
        return $result
    }
    $result.valid = $true
    $result.value = [bool]$rawValue
    return $result
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
        [string]$Configuration,
        [switch]$RequireSchemaV2
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

    $schemaVersionValue = Get-ProfileProperty $telemetry "schema_version"
    $schemaVersion = 0
    $schemaVersionParsed = [int]::TryParse([string]$schemaVersionValue, [ref]$schemaVersion)
    $schemaVersionIsJsonInteger = $schemaVersionValue -is [ValueType] -and $schemaVersionValue -isnot [bool]
    if ($schemaVersionIsJsonInteger) {
        $schemaVersionDouble = [double]$schemaVersionValue
        $schemaVersionIsJsonInteger = -not [double]::IsNaN($schemaVersionDouble) -and
            -not [double]::IsInfinity($schemaVersionDouble) -and
            $schemaVersionDouble -eq [Math]::Truncate($schemaVersionDouble) -and
            $schemaVersionDouble -ge [int]::MinValue -and $schemaVersionDouble -le [int]::MaxValue
    }
    if (-not $schemaVersionIsJsonInteger) {
        Add-Failure $Record "Telemetry schema_version must be a JSON integer"
    }
    if (-not $schemaVersionParsed) {
        $schemaVersion = 0
    }

    $framesSampled = 0L
    $framesSampledResult = Read-JsonNumberField `
        -Object $telemetry `
        -Name "frames_sampled" `
        -Record $Record `
        -Path "frames_sampled" `
        -Int64 `
        -NonNegative
    if ($framesSampledResult.valid) {
        $framesSampled = $framesSampledResult.int64_value
    }
    if ($RequireSchemaV2 -and $schemaVersion -ne 2) {
        Add-Failure $Record "This proof requires telemetry schema_version 2, got $schemaVersion"
    }
    if ($schemaVersion -ne 1 -and $schemaVersion -ne 2) {
        Add-Failure $Record "Unsupported telemetry schema_version: $schemaVersion"
    }
    if ($schemaVersion -eq 2) {
        $telemetryTargetValue = Get-ProfileProperty $telemetry "target"
        $telemetryTarget = [string]$telemetryTargetValue
        if ($telemetryTargetValue -isnot [string]) {
            Add-Failure $Record "Telemetry target must be a JSON string"
        }
        if ($telemetryTarget -ne $Record.target) {
            Add-Failure $Record "Target mismatch: requested $($Record.target), actual $telemetryTarget"
        }
        $gpuTiming = Get-ProfileProperty $telemetry "gpu_timing"
        if ($null -eq $gpuTiming) {
            Add-Failure $Record "Telemetry schema v2 is missing gpu_timing"
        }
        else {
            $gpuStatusValue = Get-ProfileProperty $gpuTiming "status"
            $gpuErrorValue = Get-ProfileProperty $gpuTiming "error"
            $gpuStatus = [string]$gpuStatusValue
            $gpuError = [string]$gpuErrorValue
            if ($gpuStatusValue -isnot [string]) {
                Add-Failure $Record "GPU timing status must be a JSON string"
            }
            if ($gpuErrorValue -isnot [string]) {
                Add-Failure $Record "GPU timing error must be a JSON string"
            }

            $expectedFrames = 0L
            $receivedFrames = 0L
            $expectedFramesResult = Read-JsonNumberField `
                -Object $gpuTiming `
                -Name "expected_frames" `
                -Record $Record `
                -Path "gpu_timing.expected_frames" `
                -Int64 `
                -NonNegative
            $receivedFramesResult = Read-JsonNumberField `
                -Object $gpuTiming `
                -Name "received_frames" `
                -Record $Record `
                -Path "gpu_timing.received_frames" `
                -Int64 `
                -NonNegative
            if ($expectedFramesResult.valid) {
                $expectedFrames = $expectedFramesResult.int64_value
            }
            if ($receivedFramesResult.valid) {
                $receivedFrames = $receivedFramesResult.int64_value
            }
            if ($gpuStatus -ne "complete") {
                Add-Failure $Record "GPU timing status was '$gpuStatus', expected 'complete'"
            }
            if ($gpuError -ne "Success") {
                Add-Failure $Record "GPU timing error was '$gpuError', expected 'Success'"
            }
            if ($expectedFrames -le 0) {
                Add-Failure $Record "GPU timing reported no expected frames"
            }
            if ($expectedFrames -ne $receivedFrames) {
                Add-Failure $Record "GPU timing frame mismatch: expected $expectedFrames, received $receivedFrames"
            }
            if ($expectedFrames -ne $framesSampled) {
                Add-Failure $Record "GPU timing sampled frame mismatch: sampled $($telemetry.frames_sampled), expected $expectedFrames"
            }

            $gpuFrameTimes = Get-ProfileProperty $gpuTiming "frame_time_ms"
            if ($null -eq $gpuFrameTimes) {
                Add-Failure $Record "Telemetry schema v2 is missing gpu_timing.frame_time_ms"
            }
            else {
                foreach ($percentile in @("p50", "p95", "p99")) {
                    Read-JsonNumberField `
                        -Object $gpuFrameTimes `
                        -Name $percentile `
                        -Record $Record `
                        -Path "gpu_timing.frame_time_ms.$percentile" `
                        -NonNegative | Out-Null
                }
            }
        }

        $telemetryErrors = Get-ProfileProperty $telemetry "errors"
        if ($null -eq $telemetryErrors) {
            Add-Failure $Record "Telemetry schema v2 is missing errors"
        }
        else {
            foreach ($errorFlag in @("abnormal_exit", "backend_mismatch", "crashed", "timed_out", "gpu_timing")) {
                $errorValue = Get-ProfileProperty $telemetryErrors $errorFlag
                if ($null -eq $errorValue) {
                    Add-Failure $Record "Telemetry schema v2 is missing errors.$errorFlag"
                }
                elseif ($errorValue -isnot [bool]) {
                    Add-Failure $Record "Telemetry error flag '$errorFlag' must be a JSON boolean false"
                }
                elseif ($errorValue) {
                    Add-Failure $Record "Telemetry error flag '$errorFlag' was true"
                }
            }
        }
    }
    $backendActualValue = Get-ProfileProperty $telemetry "backend_actual"
    if ($schemaVersion -eq 2 -and $backendActualValue -isnot [string]) {
        Add-Failure $Record "Telemetry backend_actual must be a JSON string"
    }
    $backendActual = [string]$backendActualValue
    if ($backendActual -ne $Record.backend) {
        Add-Failure $Record "Backend mismatch: requested $($Record.backend), actual $backendActual"
    }

    if ($schemaVersion -eq 2 -and (-not $framesSampledResult.valid -or $framesSampled -le 0)) {
        Add-Failure $Record "Telemetry frames_sampled must be a positive JSON integer"
    }
    elseif ($framesSampled -le 0) {
        Add-Failure $Record "No sampled frames were reported"
    }

    $Record.frames_sampled = $framesSampled
    if ($schemaVersion -eq 2) {
        $cpuFrameTime = Get-ProfileProperty $telemetry "cpu_frame_time_ms"
        $memory = Get-ProfileProperty $telemetry "memory"
        $renderStats = Get-ProfileProperty $telemetry "render_stats"

        $cpuAvgResult = Read-JsonNumberField -Object $cpuFrameTime -Name "avg" -Record $Record -Path "cpu_frame_time_ms.avg" -NonNegative
        $cpuP95Result = Read-JsonNumberField -Object $cpuFrameTime -Name "p95" -Record $Record -Path "cpu_frame_time_ms.p95" -NonNegative
        $cpuP99Result = Read-JsonNumberField -Object $cpuFrameTime -Name "p99" -Record $Record -Path "cpu_frame_time_ms.p99" -NonNegative
        $privateBytesResult = Read-JsonNumberField -Object $memory -Name "process_private_bytes_peak_mb" -Record $Record -Path "memory.process_private_bytes_peak_mb" -NonNegative
        $engineHeapPeakResult = Read-JsonNumberField -Object $memory -Name "engine_heap_peak_mb" -Record $Record -Path "memory.engine_heap_peak_mb" -NonNegative
        $engineHeapShutdownResult = Read-JsonNumberField -Object $memory -Name "engine_heap_shutdown_live_bytes" -Record $Record -Path "memory.engine_heap_shutdown_live_bytes" -Int64 -NonNegative
        $gpuAllocatorSupportedResult = Read-JsonBooleanField -Object $memory -Name "gpu_allocator_supported" -Record $Record -Path "memory.gpu_allocator_supported"
        $gpuAllocatorShutdownResult = Read-JsonNumberField -Object $memory -Name "gpu_allocator_shutdown_live_bytes" -Record $Record -Path "memory.gpu_allocator_shutdown_live_bytes" -Int64 -NonNegative
        $drawCallsResult = Read-JsonNumberField -Object $renderStats -Name "draw_calls_avg" -Record $Record -Path "render_stats.draw_calls_avg" -NonNegative

        $Record.cpu_frame_time_avg_ms = if ($cpuAvgResult.valid) { $cpuAvgResult.value } else { 0.0 }
        $Record.cpu_frame_time_p95_ms = if ($cpuP95Result.valid) { $cpuP95Result.value } else { 0.0 }
        $Record.cpu_frame_time_p99_ms = if ($cpuP99Result.valid) { $cpuP99Result.value } else { 0.0 }
        $Record.process_private_bytes_peak_mb = if ($privateBytesResult.valid) { $privateBytesResult.value } else { 0.0 }
        $Record.engine_heap_peak_mb = if ($engineHeapPeakResult.valid) { $engineHeapPeakResult.value } else { 0.0 }
        $Record.draw_calls_avg = if ($drawCallsResult.valid) { $drawCallsResult.value } else { 0.0 }

        if ($engineHeapShutdownResult.valid -and $engineHeapShutdownResult.int64_value -ne 0) {
            Add-Failure $Record "Engine heap live bytes at shutdown: $($engineHeapShutdownResult.int64_value)"
        }
        if ($gpuAllocatorSupportedResult.valid -and $gpuAllocatorSupportedResult.value -and
            $gpuAllocatorShutdownResult.valid -and $gpuAllocatorShutdownResult.int64_value -ne 0) {
            Add-Failure $Record "GPU allocator live bytes at shutdown: $($gpuAllocatorShutdownResult.int64_value)"
        }
    }
    else {
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
    }

    $targetKey = $Record.target.ToLowerInvariant()
    $capName = "{0}_private_bytes_mb" -f $targetKey
    $privateBytesCap = Get-ProfileProperty $ProfileConfig.absolute_caps $capName
    if ($null -ne $privateBytesCap -and $Record.process_private_bytes_peak_mb -gt [double]$privateBytesCap) {
        Add-Failure $Record "Private bytes peak $($Record.process_private_bytes_peak_mb) MB exceeded cap $privateBytesCap MB"
    }

    if ($Record.status -eq "NOT_RUN") {
        $Record.status = if (@($Record.warnings).Count -gt 0) { "WARN" } else { "PASS" }
    }

    if ($Record.status -ne "FAIL") {
        Compare-RecordToBaseline -Record $Record -Baseline $Baseline -ProfileConfig $ProfileConfig -Profile $Profile -Configuration $Configuration
    }
}

function Invoke-PerfGateRuns {
    param(
        [object[]]$Targets,
        [object]$ProfileConfig,
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [string]$RepoRoot,
        [string]$ReportRoot,
        [string]$EngineConfig,
        [switch]$DryRun,
        [switch]$RequireSchemaV2
    )

    $records = New-Object System.Collections.ArrayList
    foreach ($target in $Targets) {
        foreach ($backend in @($target.backends)) {
            $targetName = [string]$target.target
            $exeName = if ($targetName -eq "Editor") { "Editor.exe" } else { "Sandbox.exe" }
            $runDir = Join-Path $RepoRoot "product/bin64/$Configuration-windows-x86_64"
            $exePath = Join-Path $runDir $exeName
            $runName = "{0}-{1}" -f $targetName, $backend
            $telemetryPath = Join-Path $ReportRoot "$runName.json"
            $processLogPath = Join-Path $ReportRoot "$runName.stdout.log"
            $processErrorLogPath = Join-Path $ReportRoot "$runName.stderr.log"
            $record = New-RunRecord $targetName $backend $exePath $telemetryPath $processLogPath $processErrorLogPath
            $records.Add($record) | Out-Null
            Remove-Item -LiteralPath $telemetryPath -Force -ErrorAction SilentlyContinue

            if (-not (Test-Path -LiteralPath $exePath)) {
                Add-Failure $record "Missing executable: $exePath"
                continue
            }

            if ($DryRun) {
                $record.status = "DRY_RUN"
                continue
            }

            Set-EngineBackend -ConfigPath $EngineConfig -Backend $backend
            $runStart = Get-Date
            $arguments = @(
                "--perf-gate",
                "--perf-gate-profile=$Profile",
                "--perf-gate-output=$telemetryPath",
                "--perf-gate-target=$targetName",
                "--perf-gate-warmup-seconds=$($ProfileConfig.warmup_seconds)",
                "--perf-gate-sample-seconds=$($ProfileConfig.sample_seconds)",
                "--run-for-seconds=$([double]$ProfileConfig.warmup_seconds + [double]$ProfileConfig.sample_seconds + 10.0)"
            )
            Invoke-GateProcess -Record $record -RunDirectory $runDir -Arguments $arguments -TimeoutSeconds ([double]$ProfileConfig.timeout_seconds)

            $runLogs = Get-RunLogFiles -RepoRoot $RepoRoot -Since $runStart
            Test-LogForDiagnostics -Record $record -LogFiles $runLogs

            if ($record.status -ne "FAIL") {
                Test-Telemetry -Record $record -ProfileConfig $ProfileConfig -Baseline $Baseline -Profile $Profile -Configuration $Configuration -RequireSchemaV2:$RequireSchemaV2
            }
        }
    }
    return @($records)
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
  },
  "absolute_caps": {
    "sandbox_private_bytes_mb": 4096,
    "editor_private_bytes_mb": 6144
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

    $telemetryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-gate-self-test-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $telemetryRoot | Out-Null
    try {
        $validV2 = [PSCustomObject]@{
            schema_version = 2
            target = "Editor"
            backend_actual = "Vulkan"
            frames_sampled = 3
            gpu_timing = [PSCustomObject]@{
                status = "complete"
                error = "Success"
                expected_frames = 3
                received_frames = 3
                frame_time_ms = [PSCustomObject]@{ p50 = 0.8; p95 = 1.0; p99 = 1.1 }
            }
            cpu_frame_time_ms = [PSCustomObject]@{ avg = 1.0; p95 = 1.2; p99 = 1.3 }
            memory = [PSCustomObject]@{
                process_private_bytes_peak_mb = 64.0
                engine_heap_peak_mb = 8.0
                engine_heap_shutdown_live_bytes = 0
                gpu_allocator_supported = $true
                gpu_allocator_shutdown_live_bytes = 0
            }
            render_stats = [PSCustomObject]@{ draw_calls_avg = 1.0 }
            errors = [PSCustomObject]@{
                abnormal_exit = $false
                backend_mismatch = $false
                crashed = $false
                timed_out = $false
                gpu_timing = $false
            }
        }
        $validV2Path = Join-Path $telemetryRoot "valid-v2.json"
        $validV2 | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $validV2Path -Encoding UTF8
        $v2Record = New-RunRecord "Editor" "Vulkan" "Editor.exe" $validV2Path "stdout.log" "stderr.log"
        Test-Telemetry -Record $v2Record -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2:$NoTracy
        if ($v2Record.status -ne "PASS") {
            throw "Expected complete schema v2 telemetry to pass, got '$($v2Record.status)': $(@($v2Record.failures) -join '; ')"
        }

        $warningRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $validV2Path "stdout.log" "stderr.log"
        Add-Warning $warningRecord "injected validation warning"
        Test-Telemetry -Record $warningRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2:$NoTracy
        if ($warningRecord.status -ne "WARN") {
            throw "Expected a pre-telemetry warning to survive final telemetry status, got '$($warningRecord.status)'."
        }

        $invalidV2 = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $invalidV2.frames_sampled = 4
        $invalidV2.gpu_timing.received_frames = 2
        $invalidV2.gpu_timing.frame_time_ms.p50 = "0.8"
        $invalidV2.gpu_timing.frame_time_ms.p95 = -1.0
        $invalidV2.errors.backend_mismatch = "false"
        $invalidV2.errors.timed_out = $true
        $invalidV2Path = Join-Path $telemetryRoot "invalid-v2.json"
        $invalidV2 | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $invalidV2Path -Encoding UTF8
        $invalidV2Record = New-RunRecord "Editor" "Vulkan" "Editor.exe" $invalidV2Path "stdout.log" "stderr.log"
        Test-Telemetry -Record $invalidV2Record -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2:$NoTracy
        $invalidV2Failures = @($invalidV2Record.failures) -join "; "
        if ($invalidV2Record.status -ne "FAIL" -or
            $invalidV2Failures -notmatch "GPU timing frame mismatch" -or
            $invalidV2Failures -notmatch "GPU timing sampled frame mismatch" -or
            $invalidV2Failures -notmatch "gpu_timing.frame_time_ms.p50 must be a finite non-negative JSON number" -or
            $invalidV2Failures -notmatch "gpu_timing.frame_time_ms.p95 must be a finite non-negative JSON number" -or
            $invalidV2Failures -notmatch "backend_mismatch.*must be a JSON boolean false" -or
            $invalidV2Failures -notmatch "timed_out.*true") {
            throw "Expected incomplete schema v2 GPU timing to fail strictly."
        }

        $legacyV1 = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $legacyV1.schema_version = 1
        $legacyV1.PSObject.Properties.Remove("gpu_timing")
        $legacyV1Path = Join-Path $telemetryRoot "legacy-v1.json"
        $legacyV1 | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $legacyV1Path -Encoding UTF8
        $v1Record = New-RunRecord "Editor" "Vulkan" "Editor.exe" $legacyV1Path "stdout.log" "stderr.log"
        Test-Telemetry -Record $v1Record -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2:$NoTracy
        if ($NoTracy) {
            if ($v1Record.status -ne "FAIL" -or (@($v1Record.failures) -join "; ") -notmatch "requires telemetry schema_version 2") {
                throw "Expected no-Tracy proof to reject legacy schema v1 telemetry."
            }
        }
        elseif ($v1Record.status -ne "PASS") {
            throw "Expected ordinary perf gate to retain legacy schema v1 compatibility, got '$($v1Record.status)'."
        }

        $invalidTypes = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $invalidTypes.schema_version = "2"
        $invalidTypes.target = 42
        $invalidTypes.backend_actual = 12
        $invalidTypes.frames_sampled = "3"
        $invalidTypesPath = Join-Path $telemetryRoot "invalid-types-v2.json"
        $invalidTypes | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $invalidTypesPath -Encoding UTF8
        $invalidTypesRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $invalidTypesPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $invalidTypesRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2
        $invalidTypeFailures = @($invalidTypesRecord.failures) -join "; "
        if ($invalidTypesRecord.status -ne "FAIL" -or
            $invalidTypeFailures -notmatch "schema_version must be a JSON integer" -or
            $invalidTypeFailures -notmatch "target must be a JSON string" -or
            $invalidTypeFailures -notmatch "backend_actual must be a JSON string" -or
            $invalidTypeFailures -notmatch "frames_sampled must be a positive JSON integer") {
            throw "Expected schema v2 required fields to reject JSON strings and invalid types."
        }

        $unparseableSchema = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $unparseableSchema.schema_version = "two"
        $unparseableSchemaPath = Join-Path $telemetryRoot "unparseable-schema.json"
        $unparseableSchema | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $unparseableSchemaPath -Encoding UTF8
        $unparseableSchemaRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $unparseableSchemaPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $unparseableSchemaRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2
        if ($unparseableSchemaRecord.status -ne "FAIL" -or (@($unparseableSchemaRecord.failures) -join "; ") -notmatch "schema_version must be a JSON integer") {
            throw "Unparseable telemetry schema_version must become a structured gate failure."
        }

        $unparseableFrames = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $unparseableFrames.frames_sampled = "three"
        $unparseableFramesPath = Join-Path $telemetryRoot "unparseable-frames.json"
        $unparseableFrames | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $unparseableFramesPath -Encoding UTF8
        $unparseableFramesRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $unparseableFramesPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $unparseableFramesRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2
        if ($unparseableFramesRecord.status -ne "FAIL" -or (@($unparseableFramesRecord.failures) -join "; ") -notmatch "frames_sampled must be a positive JSON integer") {
            throw "Unparseable telemetry frames_sampled must become a structured gate failure."
        }

        $strictInvalid = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $strictInvalid.frames_sampled = [decimal]9223372036854775808
        $strictInvalid.gpu_timing.expected_frames = [decimal]9223372036854775808
        $strictInvalid.gpu_timing.received_frames = "3"
        $strictInvalid.cpu_frame_time_ms.avg = "NaN"
        $strictInvalid.cpu_frame_time_ms.p95 = "1.2"
        $strictInvalid.cpu_frame_time_ms.p99 = -0.5
        $strictInvalid.memory.process_private_bytes_peak_mb = "64"
        $strictInvalid.memory.PSObject.Properties.Remove("engine_heap_peak_mb")
        $strictInvalid.memory.engine_heap_shutdown_live_bytes = [decimal]9223372036854775808
        $strictInvalid.memory.gpu_allocator_supported = "false"
        $strictInvalid.memory.gpu_allocator_shutdown_live_bytes = -1
        $strictInvalid.PSObject.Properties.Remove("render_stats")
        $strictInvalidPath = Join-Path $telemetryRoot "strict-invalid-v2.json"
        $strictInvalid | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $strictInvalidPath -Encoding UTF8
        $strictInvalidRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $strictInvalidPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $strictInvalidRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -RequireSchemaV2
        $strictFailures = @($strictInvalidRecord.failures) -join "; "
        $requiredStrictFailures = @(
            "frames_sampled must be a positive JSON integer",
            "gpu_timing.expected_frames must be a non-negative Int64 JSON integer",
            "gpu_timing.received_frames must be a non-negative Int64 JSON integer",
            "cpu_frame_time_ms.avg must be a finite non-negative JSON number",
            "cpu_frame_time_ms.p95 must be a finite non-negative JSON number",
            "cpu_frame_time_ms.p99 must be a finite non-negative JSON number",
            "memory.process_private_bytes_peak_mb must be a finite non-negative JSON number",
            "memory.engine_heap_peak_mb must be a finite non-negative JSON number",
            "memory.engine_heap_shutdown_live_bytes must be a non-negative Int64 JSON integer",
            "memory.gpu_allocator_supported must be a JSON boolean",
            "memory.gpu_allocator_shutdown_live_bytes must be a non-negative Int64 JSON integer",
            "render_stats.draw_calls_avg must be a finite non-negative JSON number"
        )
        if ($strictInvalidRecord.status -ne "FAIL") {
            throw "Invalid schema v2 metric types did not produce FAIL."
        }
        foreach ($requiredFailure in $requiredStrictFailures) {
            if ($strictFailures -notmatch [regex]::Escape($requiredFailure)) {
                throw "Invalid schema v2 metrics missed structured failure '$requiredFailure': $strictFailures"
            }
        }
    }
    finally {
        Remove-Item -LiteralPath $telemetryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $materializationProbe = New-Object System.Collections.ArrayList
    $materializationProbe.Add((New-RunRecord "Editor" "Vulkan" "Editor.exe" "vulkan.json" "stdout.log" "stderr.log")) | Out-Null
    $materializationProbe.Add((New-RunRecord "Editor" "DX12" "Editor.exe" "dx12.json" "stdout.log" "stderr.log")) | Out-Null
    $materializedProbe = @($materializationProbe)
    if ($materializedProbe.Count -ne 2) {
        throw "Run record collection must materialize as a two-element array under Windows PowerShell 5.1."
    }

    $successfulRestoreOutput = @(Invoke-WithRequiredRestore -Body { @($materializationProbe) } -Restore { "ignored restore output" })
    if ($successfulRestoreOutput.Count -ne 2 -or $successfulRestoreOutput[0].backend -ne "Vulkan" -or $successfulRestoreOutput[1].backend -ne "DX12") {
        throw "Required-restore workflow did not preserve successful body output."
    }

    $bodyOnlyFailure = ""
    try {
        Invoke-WithRequiredRestore -Body { throw "body-only failure" } -Restore {}
    }
    catch {
        $bodyOnlyFailure = $_.Exception.Message
    }
    if ($bodyOnlyFailure -ne "body-only failure") {
        throw "Required-restore workflow did not preserve a body-only failure: '$bodyOnlyFailure'."
    }

    $restoreOnlyFailure = ""
    try {
        Invoke-WithRequiredRestore -Body {} -Restore { throw "restore-only failure" }
    }
    catch {
        $restoreOnlyFailure = $_.Exception.Message
    }
    if ($restoreOnlyFailure -ne "restore-only failure") {
        throw "Required-restore workflow did not preserve a restore-only failure: '$restoreOnlyFailure'."
    }

    $combinedFailure = ""
    try {
        Invoke-WithRequiredRestore -Body { throw "combined body failure" } -Restore { throw "combined restore failure" }
    }
    catch {
        $combinedFailure = $_.Exception.Message
    }
    if ($combinedFailure -notmatch "combined body failure" -or $combinedFailure -notmatch "combined restore failure") {
        throw "Required-restore workflow lost one side of a dual failure: '$combinedFailure'."
    }

    if ($NoTracy) {
        $plan = @(New-NoTracyCommandPlan -MSBuildPath "<msbuild>")
        $expectedPlan = @(
            "premake5.exe --no-tracy vs2022",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/InvokeMSBuild.ps1 -MSBuildPath <msbuild> -SolutionPath AshEngine.sln -Target Clean -Configuration Release -Platform x64",
            "build_editor.bat Release x64",
            "Editor Vulkan Empty Release",
            "Editor DX12 Empty Release",
            "premake5.exe vs2022",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/InvokeMSBuild.ps1 -MSBuildPath <msbuild> -SolutionPath AshEngine.sln -Target Clean -Configuration Release -Platform x64",
            "build_editor.bat Release x64"
        )
        if ($plan.Count -ne $expectedPlan.Count) {
            throw "Expected $($expectedPlan.Count) no-Tracy plan steps, got $($plan.Count)."
        }
        $expectedPhases = @("NoTracyBuild", "NoTracyBuild", "NoTracyBuild", "PerfRuns", "PerfRuns", "RestoreStandard", "RestoreStandard", "RestoreStandard")
        $expectedKinds = @("Command", "Command", "Command", "Run", "Run", "Command", "Command", "Command")
        for ($index = 0; $index -lt $expectedPlan.Count; ++$index) {
            if ($plan[$index].command_line -ne $expectedPlan[$index]) {
                throw "No-Tracy step $index mismatch: expected '$($expectedPlan[$index])', got '$($plan[$index].command_line)'."
            }
            if ($plan[$index].phase -ne $expectedPhases[$index] -or $plan[$index].kind -ne $expectedKinds[$index]) {
                throw "No-Tracy step $index had the wrong structured phase/kind."
            }
            Write-Host $plan[$index].command_line
        }

        $runDescriptors = @($plan | Where-Object { $_.phase -eq "PerfRuns" })
        if ($runDescriptors.Count -ne 2 -or
            $runDescriptors[0].target -ne "Editor" -or $runDescriptors[0].backend -ne "Vulkan" -or
            $runDescriptors[1].target -ne "Editor" -or $runDescriptors[1].backend -ne "DX12" -or
            @($runDescriptors | Where-Object { $_.scenario -ne "Empty" -or $_.configuration -ne "Release" }).Count -ne 0) {
            throw "No-Tracy plan run descriptors did not encode Editor Vulkan/DX12 Empty Release."
        }
        $derivedGateTargets = @(Get-NoTracyGateTargetsFromPlan -Plan $plan)
        if ($derivedGateTargets.Count -ne 1 -or $derivedGateTargets[0].target -ne "Editor" -or
            ($derivedGateTargets[0].backends -join ",") -ne "Vulkan,DX12") {
            throw "No-Tracy gate targets were not derived from the plan run descriptors."
        }

        $executedSteps = New-Object 'System.Collections.Generic.List[string]'
        $recordStep = { param($step) $executedSteps.Add($step.command_line) | Out-Null }
        $caughtInjectedFailure = $false
        try {
            Invoke-WithRequiredRestore `
                -Body {
                    Invoke-NoTracyCommandPhase -Plan $plan -Phase "NoTracyBuild" -RepoRoot "<repo>" -BuildLogRoot "<logs>" -CommandInvoker $recordStep
                    foreach ($runStep in @($plan | Where-Object { $_.phase -eq "PerfRuns" })) {
                        & $recordStep $runStep
                    }
                    throw "injected no-Tracy run failure"
                } `
                -Restore {
                    Invoke-NoTracyCommandPhase -Plan $plan -Phase "RestoreStandard" -RepoRoot "<repo>" -BuildLogRoot "<logs>" -ContinueAfterFailure -CommandInvoker $recordStep
                }
        }
        catch {
            if ($_.Exception.Message -ne "injected no-Tracy run failure") {
                throw
            }
            $caughtInjectedFailure = $true
        }
        if (-not $caughtInjectedFailure) {
            throw "No-Tracy workflow did not propagate an injected failure."
        }
        if ($executedSteps.Count -ne $expectedPlan.Count) {
            throw "No-Tracy fault-injection execution recorded $($executedSteps.Count) steps, expected $($expectedPlan.Count)."
        }
        for ($index = 0; $index -lt $expectedPlan.Count; ++$index) {
            if ($executedSteps[$index] -ne $expectedPlan[$index]) {
                throw "No-Tracy fault-injection step $index mismatch: expected '$($expectedPlan[$index])', got '$($executedSteps[$index])'."
            }
        }

        $validRunRecords = @(
            (New-RunRecord "Editor" "Vulkan" "Editor.exe" "vulkan.json" "stdout.log" "stderr.log"),
            (New-RunRecord "Editor" "DX12" "Editor.exe" "dx12.json" "stdout.log" "stderr.log")
        )
        $validRunRecords[0].status = "PASS"
        $validRunRecords[1].status = "WARN"
        Assert-NoTracyRunRecords -Records $validRunRecords

        $failedRunRecords = @(
            (New-RunRecord "Editor" "Vulkan" "Editor.exe" "vulkan.json" "stdout.log" "stderr.log"),
            (New-RunRecord "Editor" "DX12" "Editor.exe" "dx12.json" "stdout.log" "stderr.log")
        )
        $failedRunRecords[0].status = "FAIL"
        $failedRunRecords[1].status = "PASS"
        Assert-NoTracyRunRecords -Records $failedRunRecords

        foreach ($invalidStatus in @("NOT_RUN", "DRY_RUN", "UNKNOWN")) {
            $invalidStatusRecords = @(
                (New-RunRecord "Editor" "Vulkan" "Editor.exe" "vulkan.json" "stdout.log" "stderr.log"),
                (New-RunRecord "Editor" "DX12" "Editor.exe" "dx12.json" "stdout.log" "stderr.log")
            )
            $invalidStatusRecords[0].status = "PASS"
            $invalidStatusRecords[1].status = $invalidStatus
            $invalidStatusRejected = $false
            try {
                Assert-NoTracyRunRecords -Records $invalidStatusRecords
            }
            catch {
                if ($_.Exception.Message -notmatch "terminal PASS/WARN/FAIL status") {
                    throw
                }
                $invalidStatusRejected = $true
            }
            if (-not $invalidStatusRejected) {
                throw "No-Tracy proof accepted non-terminal status '$invalidStatus'."
            }
        }

        $invalidRunRecordsRejected = $false
        try {
            Assert-NoTracyRunRecords -Records @($validRunRecords[0])
        }
        catch {
            if ($_.Exception.Message -notmatch "exactly Editor/Vulkan then Editor/DX12") {
                throw
            }
            $invalidRunRecordsRejected = $true
        }
        if (-not $invalidRunRecordsRejected) {
            throw "No-Tracy proof accepted an incomplete run record set."
        }
    }

    $overallCases = @(
        [PSCustomObject]@{ name = "empty"; records = @(); expected = "FAIL" },
        [PSCustomObject]@{ name = "not-run"; records = @([PSCustomObject]@{ status = "NOT_RUN" }); expected = "FAIL" },
        [PSCustomObject]@{ name = "unknown"; records = @([PSCustomObject]@{ status = "UNKNOWN" }); expected = "FAIL" },
        [PSCustomObject]@{ name = "pass"; records = @([PSCustomObject]@{ status = "PASS" }); expected = "PASS" },
        [PSCustomObject]@{ name = "warn"; records = @([PSCustomObject]@{ status = "WARN" }); expected = "WARN" },
        [PSCustomObject]@{ name = "fail"; records = @([PSCustomObject]@{ status = "FAIL" }); expected = "FAIL" },
        [PSCustomObject]@{ name = "dry-run"; records = @([PSCustomObject]@{ status = "DRY_RUN" }); expected = "DRY_RUN" }
    )
    foreach ($overallCase in $overallCases) {
        $actualOverall = Get-PerfGateOverallStatus -Records $overallCase.records
        if ($actualOverall -ne $overallCase.expected) {
            throw "Overall status case '$($overallCase.name)' expected '$($overallCase.expected)', got '$actualOverall'."
        }
    }

    $runIds = @(1..4 | ForEach-Object { New-PerfGateRunId })
    if (@($runIds | Select-Object -Unique).Count -ne $runIds.Count -or
        @($runIds | Where-Object { $_ -notmatch '^\d{8}-\d{6}-\d{7}-[0-9a-f]{8}$' }).Count -ne 0) {
        throw "Perf gate report ids must combine high-resolution time with a unique suffix."
    }
    Write-Host "Perf gate report ids are high-resolution and unique"

    Write-Host "RunPerfGate self-test PASS"
}

if (-not [string]::IsNullOrWhiteSpace($Scenario) -and $Scenario -ne "Empty") {
    throw "Unsupported perf gate scenario '$Scenario'."
}
if (-not $NoTracy -and -not [string]::IsNullOrWhiteSpace($Scenario)) {
    throw "-Scenario is reserved for the no-Tracy proof until the fixed Empty harness is implemented."
}
if ($NoTracy) {
    if ($SkipBuild) {
        throw "-NoTracy cannot be used with -SkipBuild because Tracy-enabled objects could be reused."
    }
    if ($DryRun) {
        throw "-NoTracy cannot be used with -DryRun because the proof requires a fresh build."
    }
    if ($BlessBaseline) {
        throw "-NoTracy cannot be used with -BlessBaseline."
    }
    if ($Scenario -ne "Empty") {
        throw "-NoTracy requires -Scenario Empty."
    }
    if (-not [string]::IsNullOrWhiteSpace($Configuration) -and $Configuration -ne "Release") {
        throw "-NoTracy requires -Configuration Release."
    }
    $Configuration = "Release"
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

$timestamp = New-PerfGateRunId
$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/perf-gate/$timestamp"
$buildLogRoot = Join-Path $reportRoot "build"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

$engineConfig = Join-Path $repoRoot "product/config/Engine.ini"
$engineConfigBackup = Join-Path $reportRoot "Engine.ini.backup"
$engineConfigHash = (Get-FileHash -LiteralPath $engineConfig -Algorithm SHA256).Hash
$noTracyPlan = $null
if ($NoTracy) {
    $noTracyPlan = @(New-NoTracyCommandPlan -MSBuildPath (Get-MSBuildPath))
}
$gateTargets = if ($NoTracy) {
    @(Get-NoTracyGateTargetsFromPlan -Plan $noTracyPlan)
}
else {
    @($profileConfig.targets)
}

$runBody = {
    Copy-Item -LiteralPath $engineConfig -Destination $engineConfigBackup -Force
    try {
        if ($NoTracy) {
            Invoke-NoTracyCommandPhase -Plan $noTracyPlan -Phase "NoTracyBuild" -RepoRoot $repoRoot -BuildLogRoot $buildLogRoot
        }
        elseif (-not $SkipBuild -and -not $DryRun) {
            if ($Scenario -ne "Empty") {
                Invoke-BatchCommand -RepoRoot $repoRoot -CommandLine "build_sandbox.bat $Configuration x64" -LogPath (Join-Path $buildLogRoot "build_sandbox.log")
            }
            Invoke-BatchCommand -RepoRoot $repoRoot -CommandLine "build_editor.bat $Configuration x64" -LogPath (Join-Path $buildLogRoot "build_editor.log")
        }

        Invoke-PerfGateRuns `
            -Targets $gateTargets `
            -ProfileConfig $profileConfig `
            -Baseline $baseline `
            -Profile $Profile `
            -Configuration $Configuration `
            -RepoRoot $repoRoot `
            -ReportRoot $reportRoot `
            -EngineConfig $engineConfig `
            -DryRun:$DryRun `
            -RequireSchemaV2:$NoTracy
    }
    finally {
        Copy-Item -LiteralPath $engineConfigBackup -Destination $engineConfig -Force
        $restoredEngineConfigHash = (Get-FileHash -LiteralPath $engineConfig -Algorithm SHA256).Hash
        if ($restoredEngineConfigHash -ne $engineConfigHash) {
            throw "Engine.ini was not restored byte-for-byte after perf gate execution."
        }
    }
}

if ($NoTracy) {
    $records = @(Invoke-WithRequiredRestore `
        -Body $runBody `
        -Restore {
            Invoke-NoTracyCommandPhase `
                -Plan $noTracyPlan `
                -Phase "RestoreStandard" `
                -RepoRoot $repoRoot `
                -BuildLogRoot $buildLogRoot `
                -ContinueAfterFailure
        })
}
else {
    $records = @(& $runBody)
}

if ($NoTracy) {
    Assert-NoTracyRunRecords -Records $records
}

$overall = Get-PerfGateOverallStatus -Records $records

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
