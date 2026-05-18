param(
    [string]$Profile = "Standard",
    [string]$Configuration = "",
    [switch]$SkipBuild,
    [switch]$DryRun
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
        process_private_bytes_peak_mb = 0.0
        engine_heap_peak_mb = 0.0
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
        [object]$ProfileConfig
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
    $Record.process_private_bytes_peak_mb = [double]$telemetry.memory.process_private_bytes_peak_mb
    $Record.engine_heap_peak_mb = [double]$telemetry.memory.engine_heap_peak_mb

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
}

$repoRoot = Get-RepoRoot
$baselinePath = Join-Path $repoRoot "tools/perf/perf_gate_baselines.json"
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
$profileConfig = Get-ProfileProperty $baseline.profiles $Profile
if ($null -eq $profileConfig) {
    throw "Unknown perf gate profile '$Profile'."
}
if ([string]::IsNullOrWhiteSpace($Configuration)) {
    $Configuration = $profileConfig.configuration
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
                "--smoke-test-seconds=$([double]$profileConfig.warmup_seconds + [double]$profileConfig.sample_seconds + 10.0)"
            )
            Invoke-GateProcess -Record $record -RunDirectory $runDir -Arguments $arguments -TimeoutSeconds ([double]$profileConfig.timeout_seconds)

            $runLogs = Get-RunLogFiles -RepoRoot $repoRoot -Since $runStart
            Test-LogForDiagnostics -Record $record -LogFiles $runLogs

            if ($record.status -ne "FAIL") {
                Test-Telemetry -Record $record -ProfileConfig $profileConfig
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
    report_root = $reportRoot
    runs = $records
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot "summary.json") -Encoding UTF8

$markdown = @()
$markdown += "# AshEngine Perf Gate Summary"
$markdown += ""
$markdown += "Status: $overall"
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
        [Math]::Round([double]$record.cpu_frame_time_p95_ms, 4),
        [Math]::Round([double]$record.process_private_bytes_peak_mb, 2),
        [Math]::Round([double]$record.engine_heap_peak_mb, 2),
        $failureText,
        $warningText
    )) | Out-Null
}
$markdown += New-MarkdownTable `
    -Headers @("Target", "Backend", "Status", "Frames", "CPU Avg ms", "CPU P95 ms", "Private MB", "Heap MB", "Failures", "Warnings") `
    -Alignments @("Left", "Left", "Left", "Right", "Right", "Right", "Right", "Right", "Left", "Left") `
    -Rows $summaryRows
$markdown | Set-Content -LiteralPath (Join-Path $reportRoot "summary.md") -Encoding UTF8

Write-Host "Perf gate report: $reportRoot"
Write-Host "Status: $overall"
if ($overall -eq "FAIL") {
    exit 1
}
exit 0
