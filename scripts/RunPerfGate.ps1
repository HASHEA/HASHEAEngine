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
    [switch]$TimingValidation,
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

function Get-Utf8Sha256 {
    param([string]$Value)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
        $hash = $sha256.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($hash) -replace "-", "").ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-FileSha256 {
    param([string]$Path)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [System.IO.File]::OpenRead([System.IO.Path]::GetFullPath($Path))
        $hash = $sha256.ComputeHash($stream)
        return ([System.BitConverter]::ToString($hash) -replace "-", "")
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        $sha256.Dispose()
    }
}

function Test-IsJsonNativeInt64 {
    param([object]$Value)

    if (-not (Test-IsJsonNativeNumber $Value)) {
        return $false
    }
    try {
        $decimalValue = [decimal]$Value
    }
    catch {
        return $false
    }
    return $decimalValue -eq [decimal]::Truncate($decimalValue) -and
        $decimalValue -ge [decimal][int64]::MinValue -and
        $decimalValue -le [decimal][int64]::MaxValue
}

function Assert-EmptyManifestHash {
    param([string]$ManifestPath)

    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "Missing Empty scenario manifest: $ManifestPath"
    }
    try {
        $manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
    }
    catch {
        throw "Failed to parse Empty scenario manifest '$ManifestPath': $($_.Exception.Message)"
    }

    $manifestSchemaVersion = Get-ProfileProperty $manifest "schema_version"
    $manifestSceneName = Get-ProfileProperty $manifest "scene"
    $hashAlgorithm = Get-ProfileProperty $manifest "hash_algorithm"
    if (-not (Test-IsJsonNativeInt64 $manifestSchemaVersion) -or [int64]$manifestSchemaVersion -ne 1 -or
        $manifestSceneName -isnot [string] -or $manifestSceneName -ne "TerrainPerfEmpty.scene.json" -or
        $hashAlgorithm -isnot [string] -or $hashAlgorithm -ne "SHA-256") {
        throw "Empty scenario manifest metadata is invalid."
    }
    $canonicalContract = Get-ProfileProperty $manifest "canonical_contract"
    $canonicalJson = Get-ProfileProperty $manifest "canonical_contract_json"
    $expectedHash = Get-ProfileProperty $manifest "canonical_contract_sha256"
    if ($null -eq $canonicalContract -or $canonicalJson -isnot [string] -or
        $expectedHash -isnot [string] -or $expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Empty scenario manifest must contain canonical_contract, canonical_contract_json, and a SHA-256 hash."
    }
    try {
        $parsedCanonicalContract = $canonicalJson | ConvertFrom-Json
    }
    catch {
        throw "Empty scenario manifest canonical_contract_json is not valid JSON: $($_.Exception.Message)"
    }
    $actualHash = Get-Utf8Sha256 -Value $canonicalJson
    if ($actualHash -ne $expectedHash.ToLowerInvariant()) {
        throw "Empty scenario manifest canonical_contract_json SHA-256 mismatch: expected $expectedHash, actual $actualHash."
    }

    $parsedCanonicalJson = $parsedCanonicalContract | ConvertTo-Json -Depth 100 -Compress
    if ($parsedCanonicalJson -cne $canonicalJson) {
        throw "Empty scenario manifest canonical_contract_json is not in the canonical JSON form."
    }
    $manifestContractJson = $canonicalContract | ConvertTo-Json -Depth 100 -Compress
    if ($manifestContractJson -cne $canonicalJson) {
        throw "Empty scenario manifest canonical_contract does not match canonical_contract_json."
    }

    $scenePath = Join-Path (Split-Path -Parent $ManifestPath) $manifestSceneName
    if (-not (Test-Path -LiteralPath $scenePath)) {
        throw "Missing fixed Empty scenario scene referenced by manifest: $scenePath"
    }
    try {
        $scene = Get-Content -Raw -LiteralPath $scenePath | ConvertFrom-Json
    }
    catch {
        throw "Failed to parse fixed Empty scenario scene '$scenePath': $($_.Exception.Message)"
    }
    $sceneVersion = Get-ProfileProperty $scene "version"
    $nextEntityId = Get-ProfileProperty $scene "next_entity_id"
    $sceneName = Get-ProfileProperty $scene "name"
    $entities = @((Get-ProfileProperty $scene "entities"))
    $topLevelNames = @($scene.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
    if (-not (Test-IsJsonNativeInt64 $sceneVersion) -or [int64]$sceneVersion -ne 5 -or
        -not (Test-IsJsonNativeInt64 $nextEntityId) -or [int64]$nextEntityId -ne 5 -or
        $sceneName -isnot [string] -or $sceneName -ne "TerrainPerfEmpty" -or
        $entities.Count -ne 4 -or
        ($topLevelNames -join ",") -cne "entities,name,next_entity_id,scene_config,version") {
        throw "Fixed Empty scenario scene metadata or entity count is invalid."
    }

    $expectedRootJson = '{"id":1,"name":"TerrainPerfRoot","parent":0,"transform":{"position":[0.0,0.0,0.0],"rotation_euler_degrees":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}}'
    $actualRootJson = $entities[0] | ConvertTo-Json -Depth 100 -Compress
    if ($actualRootJson -cne $expectedRootJson) {
        throw "Fixed Empty scenario root entity drifted from its canonical value."
    }
    $sceneContract = [PSCustomObject][ordered]@{
        camera_entity = $entities[1]
        environment_entity = $entities[2]
        light_entity = $entities[3]
        render_config = Get-ProfileProperty $scene "scene_config"
    }
    $sceneContractJson = $sceneContract | ConvertTo-Json -Depth 100 -Compress
    if ($sceneContractJson -cne $canonicalJson) {
        throw "Fixed Empty scenario scene does not match the manifest canonical contract."
    }
    return $actualHash
}

function Get-EmptyFeasibilityContract {
    param([string]$ContractPath)

    if (-not (Test-Path -LiteralPath $ContractPath)) {
        throw "Missing immutable Empty feasibility contract: $ContractPath"
    }
    try {
        $contract = Get-Content -Raw -LiteralPath $ContractPath | ConvertFrom-Json
    }
    catch {
        throw "Failed to parse Empty feasibility contract '$ContractPath': $($_.Exception.Message)"
    }

    $renderOutput = Get-ProfileProperty $contract "render_output"
    $limits = Get-ProfileProperty $contract "limits"
    $cpuLimit = Get-ProfileProperty $limits "cpu_frame_time_p95_ms"
    $gpuLimit = Get-ProfileProperty $limits "gpu_frame_time_p95_ms"
    $contractSchemaVersion = Get-ProfileProperty $contract "schema_version"
    $contractScenario = Get-ProfileProperty $contract "scenario"
    $renderWidth = Get-ProfileProperty $renderOutput "width"
    $renderHeight = Get-ProfileProperty $renderOutput "height"
    if (-not (Test-IsJsonNativeInt64 $contractSchemaVersion) -or [int64]$contractSchemaVersion -ne 1 -or
        $contractScenario -isnot [string] -or $contractScenario -ne "Empty" -or
        -not (Test-IsJsonNativeInt64 $renderWidth) -or [int64]$renderWidth -ne 2560 -or
        -not (Test-IsJsonNativeInt64 $renderHeight) -or [int64]$renderHeight -ne 1440 -or
        -not (Test-IsJsonNativeNumber $cpuLimit) -or [double]$cpuLimit -ne 3.33 -or
        -not (Test-IsJsonNativeNumber $gpuLimit) -or [double]$gpuLimit -ne 3.33 -or
        (Get-ProfileProperty $contract "immutable") -isnot [bool] -or
        -not [bool](Get-ProfileProperty $contract "immutable") -or
        (Get-ProfileProperty $contract "blessable") -isnot [bool] -or
        [bool](Get-ProfileProperty $contract "blessable")) {
        throw "Empty feasibility contract must be immutable, non-blessable, 2560x1440, and use exact CPU/GPU P95 limits of 3.33 ms."
    }
    return $contract
}

function New-StateFileSnapshots {
    param(
        [string[]]$Paths,
        [string]$BackupRoot
    )

    New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
    $snapshots = New-Object System.Collections.ArrayList
    for ($index = 0; $index -lt $Paths.Count; ++$index) {
        $path = [System.IO.Path]::GetFullPath($Paths[$index])
        $exists = Test-Path -LiteralPath $path
        $backupPath = Join-Path $BackupRoot ("{0:D2}.bin" -f $index)
        $hash = $null
        if ($exists) {
            [System.IO.File]::WriteAllBytes($backupPath, [System.IO.File]::ReadAllBytes($path))
            $hash = Get-FileSha256 -Path $path
        }
        $snapshots.Add([PSCustomObject]@{
            path = $path
            existed = $exists
            backup_path = $backupPath
            sha256 = $hash
        }) | Out-Null
    }
    return @($snapshots)
}

function Restore-StateFileSnapshots {
    param([object[]]$Snapshots)

    $failures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($snapshot in $Snapshots) {
        try {
            if ($snapshot.existed) {
                $parent = Split-Path -Parent $snapshot.path
                New-Item -ItemType Directory -Force -Path $parent | Out-Null
                [System.IO.File]::WriteAllBytes($snapshot.path, [System.IO.File]::ReadAllBytes($snapshot.backup_path))
                $restoredHash = Get-FileSha256 -Path $snapshot.path
                if ($restoredHash -ne $snapshot.sha256) {
                    throw "SHA-256 mismatch after restore"
                }
            }
            else {
                Remove-Item -LiteralPath $snapshot.path -Force -ErrorAction SilentlyContinue
                if (Test-Path -LiteralPath $snapshot.path) {
                    throw "file did not return to its original missing state"
                }
            }
        }
        catch {
            $failures.Add("$($snapshot.path): $($_.Exception.Message)") | Out-Null
        }
    }
    if ($failures.Count -gt 0) {
        throw "PerfGate state restoration failed: $($failures -join ' | ')"
    }
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

function Set-IniValue {
    param(
        [string]$ConfigPath,
        [string]$Section,
        [string]$Name,
        [string]$Value
    )

    $lines = Get-Content -LiteralPath $ConfigPath
    $output = New-Object System.Collections.Generic.List[string]
    $inSection = $false
    $sectionFound = $false
    $updated = $false
    $escapedName = [regex]::Escape($Name)

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            if ($inSection -and -not $updated) {
                $output.Add("$Name=$Value")
                $updated = $true
            }
            $inSection = [string]::Equals($matches[1].Trim(), $Section, [System.StringComparison]::OrdinalIgnoreCase)
            if ($inSection) {
                $sectionFound = $true
            }
            $output.Add($line)
            continue
        }
        if ($inSection -and $line -match "^\s*$escapedName\s*=") {
            $output.Add("$Name=$Value")
            $updated = $true
            continue
        }
        $output.Add($line)
    }

    if (-not $updated) {
        if (-not $sectionFound) {
            $output.Add("")
            $output.Add("[$Section]")
        }
        $output.Add("$Name=$Value")
    }

    Set-Content -LiteralPath $ConfigPath -Value $output -Encoding UTF8
}

function Set-EngineBackend {
    param(
        [string]$ConfigPath,
        [string]$Backend
    )

    Set-IniValue -ConfigPath $ConfigPath -Section "RHI" -Name "Backend" -Value $Backend
}

function Set-PerfGateEngineConfig {
    param(
        [string]$ConfigPath,
        [string]$Backend,
        [switch]$EmptyScenario,
        [switch]$TimingValidation
    )

    Set-EngineBackend -ConfigPath $ConfigPath -Backend $Backend
    if ($EmptyScenario) {
        Set-IniValue -ConfigPath $ConfigPath -Section "Rendering" -Name "VSync" -Value "false"
        Set-IniValue -ConfigPath $ConfigPath -Section "VulkanValidation" -Name "Enabled" -Value "false"
        Set-IniValue -ConfigPath $ConfigPath -Section "DX12Validation" -Name "Enabled" -Value "false"
    }
    if ($TimingValidation) {
        if ($Backend -eq "Vulkan") {
            Set-IniValue -ConfigPath $ConfigPath -Section "VulkanValidation" -Name "Enabled" -Value "true"
            Set-IniValue -ConfigPath $ConfigPath -Section "VulkanValidation" -Name "GpuAssisted" -Value "false"
            Set-IniValue -ConfigPath $ConfigPath -Section "VulkanValidation" -Name "SynchronizationValidation" -Value "true"
        }
        elseif ($Backend -eq "DX12") {
            Set-IniValue -ConfigPath $ConfigPath -Section "DX12Validation" -Name "Enabled" -Value "true"
            Set-IniValue -ConfigPath $ConfigPath -Section "DX12Validation" -Name "GpuValidation" -Value "true"
        }
        else {
            throw "Timing validation does not support backend '$Backend'."
        }
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
        scenario = ""
        configuration = ""
        timing_validation = $false
        arguments = @()
        command_line = ""
        gpu_frame_time_p95_ms = 0.0
        feasibility_contract_status = "NOT_APPLICABLE"
    }
}

function New-PerfGateRunPlan {
    param(
        [object[]]$Targets,
        [object]$ProfileConfig,
        [string]$Profile,
        [string]$Configuration,
        [string]$RepoRoot,
        [string]$ReportRoot,
        [string]$Scenario,
        [switch]$TimingValidation
    )

    $plan = New-Object System.Collections.ArrayList
    $scenePath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "product/assets/scenes/TerrainPerfEmpty.scene.json"))
    foreach ($target in $Targets) {
        foreach ($backendValue in @($target.backends)) {
            $targetName = [string]$target.target
            $backend = [string]$backendValue
            $exeName = if ($targetName -eq "Editor") { "Editor.exe" } else { "Sandbox.exe" }
            $runDirectory = Join-Path $RepoRoot "product/bin64/$Configuration-windows-x86_64"
            $executable = Join-Path $runDirectory $exeName
            $runName = "{0}-{1}" -f $targetName, $backend
            $telemetryPath = Join-Path $ReportRoot "$runName.json"
            $arguments = New-Object System.Collections.ArrayList
            foreach ($argument in @(
                "--perf-gate",
                "--perf-gate-profile=$Profile",
                "--perf-gate-output=$telemetryPath",
                "--perf-gate-target=$targetName",
                "--perf-gate-warmup-seconds=$($ProfileConfig.warmup_seconds)",
                "--perf-gate-sample-seconds=$($ProfileConfig.sample_seconds)"
            )) {
                $arguments.Add($argument) | Out-Null
            }
            if ($Scenario -eq "Empty") {
                $rhiArgument = if ($backend -eq "Vulkan") { "vulkan" } else { "dx12" }
                foreach ($argument in @(
                    "--rhi=$rhiArgument",
                    "--scene=$scenePath",
                    "--perf-gate-scenario=Empty",
                    "--perf-gate-width=2560",
                    "--perf-gate-height=1440"
                )) {
                    $arguments.Add($argument) | Out-Null
                }
                if ($TimingValidation) {
                    $arguments.Add("--perf-gate-timing-validation") | Out-Null
                }
            }

            $argumentArray = @($arguments)
            if (@($argumentArray | Where-Object { $_ -match '^--(run-for-frames|run-for-seconds|smoke-test)' }).Count -ne 0) {
                throw "PerfGate run plans must not use fixed frame/time/smoke success arguments."
            }
            $plan.Add([PSCustomObject]@{
                target = $targetName
                backend = $backend
                scenario = $Scenario
                configuration = $Configuration
                timing_validation = [bool]$TimingValidation
                run_directory = $runDirectory
                executable = $executable
                telemetry = $telemetryPath
                process_log = Join-Path $ReportRoot "$runName.stdout.log"
                process_error_log = Join-Path $ReportRoot "$runName.stderr.log"
                arguments = $argumentArray
                command_line = "$(Quote-Argument $executable) $(Join-Arguments $argumentArray)"
            }) | Out-Null
        }
    }
    return @($plan)
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
        [System.IO.FileInfo[]]$LogFiles,
        [switch]$WarningsAreFailures
    )

    $failurePatterns = @(
        "VUID-",
        "[Vulkan Validation] - ERROR",
        "Validation Error",
        "VK_VALIDATION_ERROR",
        "D3D12 ERROR",
        "DXGI ERROR",
        "GPU-Based Validation ERROR",
        "CORRUPTION"
    )
    $warningPatterns = @(
        "[Vulkan Validation] - WARNING",
        "D3D12 WARNING",
        "DXGI WARNING",
        "Validation Warning"
    )
    $failureRegexPatterns = @(
        '\[DX12 Validation\].*\bERROR\s*:'
    )
    $warningRegexPatterns = @(
        '\[DX12 Validation\].*\bWARNING\s*:'
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
                $message = "Diagnostic warning pattern '$pattern' found in $($logFile.Name)"
                if ($WarningsAreFailures) {
                    Add-Failure $Record $message
                }
                else {
                    Add-Warning $Record $message
                }
                break
            }
        }
        foreach ($pattern in $failureRegexPatterns) {
            $matches = Select-String -LiteralPath $logFile.FullName -Pattern $pattern -ErrorAction SilentlyContinue
            if ($matches) {
                Add-Failure $Record "Diagnostic failure pattern '$pattern' found in $($logFile.Name)"
                break
            }
        }
        foreach ($pattern in $warningRegexPatterns) {
            $matches = Select-String -LiteralPath $logFile.FullName -Pattern $pattern -ErrorAction SilentlyContinue
            if ($matches) {
                $message = "Diagnostic warning pattern '$pattern' found in $($logFile.Name)"
                if ($WarningsAreFailures) {
                    Add-Failure $Record $message
                }
                else {
                    Add-Warning $Record $message
                }
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
        [switch]$RequireSchemaV2,
        [switch]$EmptyScenario,
        [switch]$TimingValidation,
        [object]$FeasibilityContract
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
    if (($RequireSchemaV2 -or $EmptyScenario) -and $schemaVersion -ne 2) {
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
        if ($EmptyScenario) {
            $scenarioValue = Get-ProfileProperty $telemetry "scenario"
            if ($scenarioValue -isnot [string] -or $scenarioValue -ne "Empty") {
                Add-Failure $Record "Empty telemetry scenario must be the JSON string 'Empty'"
            }

            $timingValidationValue = Get-ProfileProperty $telemetry "timing_validation"
            if ($timingValidationValue -isnot [bool] -or [bool]$timingValidationValue -ne [bool]$TimingValidation) {
                Add-Failure $Record "Empty telemetry timing_validation must be the JSON boolean $([bool]$TimingValidation)"
            }

            $readiness = Get-ProfileProperty $telemetry "readiness"
            $readinessStatus = Get-ProfileProperty $readiness "status"
            if ($readinessStatus -isnot [string] -or $readinessStatus -ne "complete") {
                Add-Failure $Record "Empty telemetry readiness.status must be the JSON string 'complete'"
            }
            $submittedFrameIndex = Read-JsonNumberField `
                -Object $readiness `
                -Name "submitted_frame_index" `
                -Record $Record `
                -Path "readiness.submitted_frame_index" `
                -Int64 `
                -NonNegative
            if (-not $submittedFrameIndex.valid -or $submittedFrameIndex.int64_value -le 0) {
                Add-Failure $Record "Empty telemetry readiness.submitted_frame_index must be a positive JSON integer"
            }

            $renderOutput = Get-ProfileProperty $telemetry "render_output"
            $renderOutputStatus = Get-ProfileProperty $renderOutput "status"
            if ($renderOutputStatus -isnot [string] -or $renderOutputStatus -ne "complete") {
                Add-Failure $Record "Empty telemetry render_output.status must be the JSON string 'complete'"
            }
            foreach ($dimension in @("width", "height")) {
                $expectedDimension = if ($dimension -eq "width") { 2560L } else { 1440L }
                $dimensionResult = Read-JsonNumberField `
                    -Object $renderOutput `
                    -Name $dimension `
                    -Record $Record `
                    -Path "render_output.$dimension" `
                    -Int64 `
                    -NonNegative
                if ($dimensionResult.valid -and $dimensionResult.int64_value -ne $expectedDimension) {
                    Add-Failure $Record "Empty telemetry render_output.$dimension must be exactly $expectedDimension"
                }
            }

            $swapchain = Get-ProfileProperty $telemetry "swapchain"
            foreach ($dimension in @("width", "height")) {
                $dimensionResult = Read-JsonNumberField `
                    -Object $swapchain `
                    -Name $dimension `
                    -Record $Record `
                    -Path "swapchain.$dimension" `
                    -Int64 `
                    -NonNegative
                if ($dimensionResult.valid -and $dimensionResult.int64_value -le 0) {
                    Add-Failure $Record "Empty telemetry swapchain.$dimension must be a positive JSON integer"
                }
            }
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
                Read-JsonNumberField -Object $gpuFrameTimes -Name "p50" -Record $Record -Path "gpu_timing.frame_time_ms.p50" -NonNegative | Out-Null
                $gpuP95Result = Read-JsonNumberField -Object $gpuFrameTimes -Name "p95" -Record $Record -Path "gpu_timing.frame_time_ms.p95" -NonNegative
                Read-JsonNumberField -Object $gpuFrameTimes -Name "p99" -Record $Record -Path "gpu_timing.frame_time_ms.p99" -NonNegative | Out-Null
                if ($gpuP95Result.valid) {
                    $Record.gpu_frame_time_p95_ms = $gpuP95Result.value
                }
            }
        }

        $telemetryErrors = Get-ProfileProperty $telemetry "errors"
        if ($null -eq $telemetryErrors) {
            Add-Failure $Record "Telemetry schema v2 is missing errors"
        }
        else {
            $requiredErrorFlags = @("abnormal_exit", "backend_mismatch", "crashed", "timed_out", "gpu_timing")
            if ($EmptyScenario) {
                $requiredErrorFlags += @("render_output_mismatch", "incomplete")
            }
            foreach ($errorFlag in $requiredErrorFlags) {
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

    if ($EmptyScenario) {
        if ($TimingValidation) {
            $Record.feasibility_contract_status = "SKIPPED_TIMING_VALIDATION"
        }
        else {
            if ($null -eq $FeasibilityContract) {
                Add-Failure $Record "Empty telemetry requires the immutable feasibility contract"
                $Record.feasibility_contract_status = "FAIL"
            }
            else {
                $limits = Get-ProfileProperty $FeasibilityContract "limits"
                $cpuLimit = [double](Get-ProfileProperty $limits "cpu_frame_time_p95_ms")
                $gpuLimit = [double](Get-ProfileProperty $limits "gpu_frame_time_p95_ms")
                if ($Record.cpu_frame_time_p95_ms -gt $cpuLimit) {
                    Add-Failure $Record "Empty feasibility CPU frame time P95 $($Record.cpu_frame_time_p95_ms) ms exceeded immutable limit $cpuLimit ms"
                }
                if ($Record.gpu_frame_time_p95_ms -gt $gpuLimit) {
                    Add-Failure $Record "Empty feasibility GPU frame time P95 $($Record.gpu_frame_time_p95_ms) ms exceeded immutable limit $gpuLimit ms"
                }
                $Record.feasibility_contract_status = if ($Record.status -eq "FAIL") { "FAIL" } else { "PASS" }
            }
        }
    }

    if (-not $TimingValidation) {
        $targetKey = $Record.target.ToLowerInvariant()
        $capName = "{0}_private_bytes_mb" -f $targetKey
        $privateBytesCap = Get-ProfileProperty $ProfileConfig.absolute_caps $capName
        if ($null -ne $privateBytesCap -and $Record.process_private_bytes_peak_mb -gt [double]$privateBytesCap) {
            Add-Failure $Record "Private bytes peak $($Record.process_private_bytes_peak_mb) MB exceeded cap $privateBytesCap MB"
        }
    }

    if ($Record.status -eq "NOT_RUN") {
        $Record.status = if (@($Record.warnings).Count -gt 0) { "WARN" } else { "PASS" }
    }

    if (-not $TimingValidation -and $Record.status -ne "FAIL") {
        Compare-RecordToBaseline -Record $Record -Baseline $Baseline -ProfileConfig $ProfileConfig -Profile $Profile -Configuration $Configuration
    }
}

function Invoke-PerfGateRuns {
    param(
        [object[]]$RunPlan,
        [object]$ProfileConfig,
        [object]$Baseline,
        [string]$Profile,
        [string]$Configuration,
        [string]$RepoRoot,
        [string]$EngineConfig,
        [object[]]$StateSnapshots,
        [object]$FeasibilityContract,
        [switch]$DryRun,
        [switch]$RequireSchemaV2,
        [switch]$EmptyScenario,
        [switch]$TimingValidation
    )

    $records = New-Object System.Collections.ArrayList
    foreach ($step in $RunPlan) {
        $record = New-RunRecord $step.target $step.backend $step.executable $step.telemetry $step.process_log $step.process_error_log
        $record.scenario = $step.scenario
        $record.configuration = $step.configuration
        $record.timing_validation = [bool]$step.timing_validation
        $record.arguments = @($step.arguments)
        $record.command_line = $step.command_line
        $records.Add($record) | Out-Null

        Write-Host "Perf gate run: $($step.command_line)"
        if ($DryRun) {
            $record.status = "DRY_RUN"
            continue
        }

        Restore-StateFileSnapshots -Snapshots $StateSnapshots
        Set-PerfGateEngineConfig `
            -ConfigPath $EngineConfig `
            -Backend $step.backend `
            -EmptyScenario:$EmptyScenario `
            -TimingValidation:$TimingValidation
        Remove-Item -LiteralPath $step.telemetry -Force -ErrorAction SilentlyContinue

        if (-not (Test-Path -LiteralPath $step.executable)) {
            Add-Failure $record "Missing executable: $($step.executable)"
            continue
        }

        $runStart = Get-Date
        Invoke-GateProcess `
            -Record $record `
            -RunDirectory $step.run_directory `
            -Arguments @($step.arguments) `
            -TimeoutSeconds ([double]$ProfileConfig.timeout_seconds)

        $runLogs = New-Object System.Collections.ArrayList
        foreach ($logFile in @(Get-RunLogFiles -RepoRoot $RepoRoot -Since $runStart)) {
            $runLogs.Add($logFile) | Out-Null
        }
        foreach ($processLog in @($step.process_log, $step.process_error_log)) {
            if (Test-Path -LiteralPath $processLog) {
                $runLogs.Add((Get-Item -LiteralPath $processLog)) | Out-Null
            }
        }
        Test-LogForDiagnostics `
            -Record $record `
            -LogFiles @($runLogs) `
            -WarningsAreFailures:$TimingValidation

        if ($record.status -ne "FAIL") {
            Test-Telemetry `
                -Record $record `
                -ProfileConfig $ProfileConfig `
                -Baseline $Baseline `
                -Profile $Profile `
                -Configuration $Configuration `
                -RequireSchemaV2:$RequireSchemaV2 `
                -EmptyScenario:$EmptyScenario `
                -TimingValidation:$TimingValidation `
                -FeasibilityContract $FeasibilityContract
        }
    }
    return @($records)
}

function Invoke-RunPerfGateSelfTest {
    $fileHashProbe = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllBytes($fileHashProbe, [System.Text.Encoding]::ASCII.GetBytes("abc"))
        if ((Get-FileSha256 -Path $fileHashProbe) -cne "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") {
            throw "Binary file SHA-256 helper did not produce the canonical digest."
        }
    }
    finally {
        Remove-Item -LiteralPath $fileHashProbe -Force -ErrorAction SilentlyContinue
    }

    $profileConfig = ConvertFrom-Json @'
{
  "warmup_seconds": 5,
  "sample_seconds": 10,
  "timeout_seconds": 30,
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
    $feasibilityContract = ConvertFrom-Json @'
{
  "schema_version": 1,
  "scenario": "Empty",
  "render_output": { "width": 2560, "height": 1440 },
  "limits": { "cpu_frame_time_p95_ms": 3.33, "gpu_frame_time_p95_ms": 3.33 },
  "immutable": true,
  "blessable": false
}
'@
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
            scenario = "Empty"
            timing_validation = $false
            readiness = [PSCustomObject]@{ status = "complete"; submitted_frame_index = 16 }
            render_output = [PSCustomObject]@{ status = "complete"; width = 2560; height = 1440 }
            swapchain = [PSCustomObject]@{ width = 1920; height = 1080 }
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
                render_output_mismatch = $false
                incomplete = $false
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

        $emptyRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $validV2Path "stdout.log" "stderr.log"
        Test-Telemetry `
            -Record $emptyRecord `
            -ProfileConfig $profileConfig `
            -Baseline $emptyBaseline `
            -Profile "Standard" `
            -Configuration "Release" `
            -EmptyScenario `
            -FeasibilityContract $feasibilityContract
        if ($emptyRecord.status -ne "PASS" -or
            $emptyRecord.feasibility_contract_status -ne "PASS" -or
            $emptyRecord.gpu_frame_time_p95_ms -ne 1.0) {
            throw "Expected valid fixed Empty telemetry (including a distinct swapchain extent) to pass strictly: $(@($emptyRecord.failures) -join '; ')"
        }

        $invalidEmpty = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $invalidEmpty.scenario = "Other"
        $invalidEmpty.readiness.status = "waiting"
        $invalidEmpty.readiness.submitted_frame_index = 0
        $invalidEmpty.render_output.status = "incomplete"
        $invalidEmpty.render_output.width = 2559
        $invalidEmpty.errors.render_output_mismatch = $true
        $invalidEmpty.errors.PSObject.Properties.Remove("incomplete")
        $invalidEmptyPath = Join-Path $telemetryRoot "invalid-empty.json"
        $invalidEmpty | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $invalidEmptyPath -Encoding UTF8
        $invalidEmptyRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $invalidEmptyPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $invalidEmptyRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -EmptyScenario -FeasibilityContract $feasibilityContract
        $invalidEmptyFailures = @($invalidEmptyRecord.failures) -join "; "
        foreach ($expectedFailure in @("scenario", "readiness.status", "submitted_frame_index", "render_output.status", "render_output.width", "render_output_mismatch", "errors.incomplete")) {
            if ($invalidEmptyRecord.status -ne "FAIL" -or $invalidEmptyFailures -notmatch [regex]::Escape($expectedFailure)) {
                throw "Empty telemetry contract did not reject invalid '$expectedFailure': $invalidEmptyFailures"
            }
        }

        $feasibilityFailures = New-Object System.Collections.ArrayList
        foreach ($case in @(
            [PSCustomObject]@{ name = "cpu"; cpu_p95 = 3.34; gpu_p95 = 1.0; diagnostic = "Empty feasibility CPU frame time P95" },
            [PSCustomObject]@{ name = "gpu"; cpu_p95 = 1.2; gpu_p95 = 3.34; diagnostic = "Empty feasibility GPU frame time P95" }
        )) {
            $invalidFeasibility = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
            $invalidFeasibility.cpu_frame_time_ms.p95 = $case.cpu_p95
            $invalidFeasibility.gpu_timing.frame_time_ms.p95 = $case.gpu_p95
            $invalidFeasibilityPath = Join-Path $telemetryRoot ("invalid-feasibility-{0}.json" -f $case.name)
            $invalidFeasibility | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $invalidFeasibilityPath -Encoding UTF8
            $invalidFeasibilityRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $invalidFeasibilityPath "stdout.log" "stderr.log"
            Test-Telemetry -Record $invalidFeasibilityRecord -ProfileConfig $profileConfig -Baseline $emptyBaseline -Profile "Standard" -Configuration "Release" -EmptyScenario -FeasibilityContract $feasibilityContract
            if ($invalidFeasibilityRecord.status -ne "FAIL" -or
                $invalidFeasibilityRecord.feasibility_contract_status -ne "FAIL" -or
                (@($invalidFeasibilityRecord.failures) -join "; ") -notmatch [regex]::Escape($case.diagnostic)) {
                throw "Empty feasibility $($case.name) P95=3.34 ms was not rejected before baseline comparison."
            }
            $feasibilityFailures.Add($invalidFeasibilityRecord) | Out-Null
        }

        $blessProbe = ConvertFrom-Json '{ "baselines": {} }'
        $blessProbeBefore = $blessProbe | ConvertTo-Json -Depth 16 -Compress
        $contractBeforeBless = $feasibilityContract | ConvertTo-Json -Depth 16 -Compress
        $feasibilityOverall = Get-PerfGateOverallStatus -Records @($feasibilityFailures)
        if ($feasibilityOverall -ne "FAIL" -and $feasibilityOverall -ne "DRY_RUN") {
            Update-BaselinesFromRecords -Baseline $blessProbe -Profile "Standard" -Configuration "Release" -Records @($feasibilityFailures) -ReportRoot "self-test-bless"
        }
        if ($feasibilityOverall -ne "FAIL" -or
            ($blessProbe | ConvertTo-Json -Depth 16 -Compress) -cne $blessProbeBefore -or
            ($feasibilityContract | ConvertTo-Json -Depth 16 -Compress) -cne $contractBeforeBless) {
            throw "Baseline blessing bypassed or mutated the immutable Empty feasibility failure."
        }

        $timingTelemetry = $validV2 | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $timingTelemetry.timing_validation = $true
        $timingTelemetry.cpu_frame_time_ms.p95 = 99.0
        $timingTelemetry.gpu_timing.frame_time_ms.p95 = 99.0
        $timingTelemetryPath = Join-Path $telemetryRoot "timing-validation.json"
        $timingTelemetry | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $timingTelemetryPath -Encoding UTF8
        $timingProfile = $profileConfig | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $timingProfile.absolute_caps.editor_private_bytes_mb = 1
        $timingRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" $timingTelemetryPath "stdout.log" "stderr.log"
        Test-Telemetry -Record $timingRecord -ProfileConfig $timingProfile -Baseline $baseline -Profile "Standard" -Configuration "Debug" -EmptyScenario -TimingValidation -FeasibilityContract $feasibilityContract
        if ($timingRecord.status -ne "PASS" -or
            $timingRecord.feasibility_contract_status -ne "SKIPPED_TIMING_VALIDATION" -or
            $timingRecord.baseline_status -ne "NOT_COMPARED") {
            throw "TimingValidation did not require clean timing while skipping performance, feasibility, and baseline caps (status=$($timingRecord.status), feasibility=$($timingRecord.feasibility_contract_status), baseline=$($timingRecord.baseline_status), warnings=$(@($timingRecord.warnings).Count)): $(@($timingRecord.failures) -join '; ')"
        }

        Write-Host "Task 7 telemetry contract PASS"
    }
    finally {
        Remove-Item -LiteralPath $telemetryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $task7Root = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-perf-gate-task7-self-test-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $task7Root | Out-Null
    try {
        $task7Targets = @([PSCustomObject]@{ target = "Editor"; backends = @("Vulkan", "DX12") })
        $task7ReportRoot = Join-Path $task7Root "reports"
        $emptyRunPlan = @(New-PerfGateRunPlan `
            -Targets $task7Targets `
            -ProfileConfig $profileConfig `
            -Profile "Standard" `
            -Configuration "Release" `
            -RepoRoot $task7Root `
            -ReportRoot $task7ReportRoot `
            -Scenario "Empty")
        $expectedScenePath = [System.IO.Path]::GetFullPath((Join-Path $task7Root "product/assets/scenes/TerrainPerfEmpty.scene.json"))
        if ($emptyRunPlan.Count -ne 2 -or
            $emptyRunPlan[0].target -ne "Editor" -or $emptyRunPlan[0].backend -ne "Vulkan" -or
            $emptyRunPlan[1].target -ne "Editor" -or $emptyRunPlan[1].backend -ne "DX12" -or
            @($emptyRunPlan | Where-Object { $_.configuration -ne "Release" -or $_.scenario -ne "Empty" -or $_.timing_validation }).Count -ne 0) {
            throw "Empty run plan must contain exactly Editor/Vulkan then Editor/DX12 Release runs."
        }
        foreach ($index in 0..1) {
            $step = $emptyRunPlan[$index]
            $expectedRhi = if ($index -eq 0) { "vulkan" } else { "dx12" }
            $requiredArguments = @(
                "--perf-gate",
                "--perf-gate-profile=Standard",
                "--perf-gate-output=$($step.telemetry)",
                "--perf-gate-target=Editor",
                "--perf-gate-warmup-seconds=5",
                "--perf-gate-sample-seconds=10",
                "--rhi=$expectedRhi",
                "--scene=$expectedScenePath",
                "--perf-gate-scenario=Empty",
                "--perf-gate-width=2560",
                "--perf-gate-height=1440"
            )
            foreach ($requiredArgument in $requiredArguments) {
                if (@($step.arguments | Where-Object { $_ -eq $requiredArgument }).Count -ne 1) {
                    throw "Empty run plan is missing exact argument '$requiredArgument'."
                }
            }
            if (@($step.arguments | Where-Object { $_ -match '^--(run-for-frames|run-for-seconds|smoke-test)' }).Count -ne 0 -or
                $step.command_line -ne "$(Quote-Argument $step.executable) $(Join-Arguments @($step.arguments))") {
                throw "Empty run plan used a fixed success limit or did not record the full command line."
            }
            Write-Host "Task 7 run plan: $($step.command_line)"
        }

        $ordinaryRunPlan = @(New-PerfGateRunPlan `
            -Targets @([PSCustomObject]@{ target = "Sandbox"; backends = @("Vulkan") }) `
            -ProfileConfig $profileConfig `
            -Profile "Standard" `
            -Configuration "Debug" `
            -RepoRoot $task7Root `
            -ReportRoot $task7ReportRoot `
            -Scenario "")
        if ($ordinaryRunPlan.Count -ne 1 -or
            @($ordinaryRunPlan[0].arguments | Where-Object { $_ -match '^--(run-for-frames|run-for-seconds|smoke-test)' }).Count -ne 0) {
            throw "Ordinary perf gate run plans must rely only on the process wall-clock timeout for failure."
        }

        $timingRunPlan = @(New-PerfGateRunPlan `
            -Targets $task7Targets `
            -ProfileConfig $profileConfig `
            -Profile "Standard" `
            -Configuration "Debug" `
            -RepoRoot $task7Root `
            -ReportRoot $task7ReportRoot `
            -Scenario "Empty" `
            -TimingValidation)
        if ($timingRunPlan.Count -ne 2 -or
            @($timingRunPlan | Where-Object { $_.configuration -ne "Debug" -or -not $_.timing_validation }).Count -ne 0 -or
            @($timingRunPlan | Where-Object { @($_.arguments | Where-Object { $_ -eq "--perf-gate-timing-validation" }).Count -ne 1 }).Count -ne 0) {
            throw "TimingValidation run plan must contain exactly two Debug runs with the timing flag."
        }
        Write-Host "Task 7 Empty run plan PASS"

        $configRoot = Join-Path $task7Root "config"
        $editorConfigRoot = Join-Path $configRoot "editor"
        New-Item -ItemType Directory -Force -Path $editorConfigRoot | Out-Null
        $statePaths = @(
            (Join-Path $configRoot "Engine.ini"),
            (Join-Path $editorConfigRoot "EditorSettings.json"),
            (Join-Path $editorConfigRoot "ViewportLayout.json"),
            (Join-Path $editorConfigRoot "imgui.ini")
        )
        $engineIniText = @'
[RHI]
Backend=DX12
[Rendering]
VSync=true
[VulkanValidation]
Enabled=false
GpuAssisted=false
SynchronizationValidation=false
[DX12Validation]
Enabled=false
GpuValidation=false
'@
        [System.IO.File]::WriteAllText($statePaths[0], $engineIniText, (New-Object System.Text.UTF8Encoding($false)))
        [System.IO.File]::WriteAllBytes($statePaths[1], [byte[]](1, 2, 3, 254))
        [System.IO.File]::WriteAllBytes($statePaths[2], [byte[]](4, 5, 6, 253))
        [System.IO.File]::WriteAllBytes($statePaths[3], [byte[]](7, 8, 9, 252))
        $snapshots = @(New-StateFileSnapshots -Paths $statePaths -BackupRoot (Join-Path $task7Root "state-backups"))
        if ($snapshots.Count -ne 4) {
            throw "Task 7 state snapshot did not materialize exactly four entries under Windows PowerShell 5.1."
        }
        foreach ($statePath in $statePaths) {
            [System.IO.File]::WriteAllBytes($statePath, [byte[]](99, 98, 97))
        }
        Restore-StateFileSnapshots -Snapshots $snapshots
        foreach ($snapshot in $snapshots) {
            if ((Get-FileSha256 -Path $snapshot.path) -ne $snapshot.sha256) {
                throw "Task 7 state restore did not reproduce '$($snapshot.path)' byte-for-byte."
            }
        }

        $readIniValue = {
            param([string]$Path, [string]$Section, [string]$Name)
            $activeSection = ""
            $escapedName = [regex]::Escape($Name)
            foreach ($line in Get-Content -LiteralPath $Path) {
                if ($line -match '^\s*\[(.+)\]\s*$') {
                    $activeSection = $matches[1].Trim()
                    continue
                }
                if ([string]::Equals($activeSection, $Section, [System.StringComparison]::OrdinalIgnoreCase) -and
                    $line -match "^\s*$escapedName\s*=\s*(.*?)\s*$") {
                    return $matches[1]
                }
            }
            return $null
        }

        Set-PerfGateEngineConfig -ConfigPath $statePaths[0] -Backend "Vulkan" -EmptyScenario
        if ((& $readIniValue $statePaths[0] "Rendering" "VSync") -ne "false" -or
            (& $readIniValue $statePaths[0] "VulkanValidation" "Enabled") -ne "false" -or
            (& $readIniValue $statePaths[0] "DX12Validation" "Enabled") -ne "false") {
            throw "Ordinary Empty config must disable VSync and both validation layers."
        }
        Restore-StateFileSnapshots -Snapshots $snapshots
        Set-PerfGateEngineConfig -ConfigPath $statePaths[0] -Backend "Vulkan" -EmptyScenario -TimingValidation
        if ((& $readIniValue $statePaths[0] "Rendering" "VSync") -ne "false" -or
            (& $readIniValue $statePaths[0] "VulkanValidation" "Enabled") -ne "true" -or
            (& $readIniValue $statePaths[0] "VulkanValidation" "GpuAssisted") -ne "false" -or
            (& $readIniValue $statePaths[0] "VulkanValidation" "SynchronizationValidation") -ne "true" -or
            (& $readIniValue $statePaths[0] "DX12Validation" "Enabled") -ne "false") {
            throw "TimingValidation Vulkan config did not enforce VSync=false, core/synchronization validation, and GPU-assisted validation disabled."
        }
        Restore-StateFileSnapshots -Snapshots $snapshots
        Set-PerfGateEngineConfig -ConfigPath $statePaths[0] -Backend "DX12" -EmptyScenario -TimingValidation
        if ((& $readIniValue $statePaths[0] "Rendering" "VSync") -ne "false" -or
            (& $readIniValue $statePaths[0] "VulkanValidation" "Enabled") -ne "false" -or
            (& $readIniValue $statePaths[0] "DX12Validation" "Enabled") -ne "true" -or
            (& $readIniValue $statePaths[0] "DX12Validation" "GpuValidation") -ne "true") {
            throw "TimingValidation DX12 config did not enforce VSync=false and the required validation settings."
        }
        Restore-StateFileSnapshots -Snapshots $snapshots
        foreach ($snapshot in $snapshots) {
            if ((Get-FileSha256 -Path $snapshot.path) -ne $snapshot.sha256) {
                throw "Task 7 final state restore did not reproduce '$($snapshot.path)' byte-for-byte."
            }
        }
        Write-Host "Task 7 state restoration PASS"

        $diagnosticLog = Join-Path $task7Root "diagnostic-warning.log"
        Set-Content -LiteralPath $diagnosticLog -Value "[Vulkan Validation] - WARNING" -Encoding UTF8
        $ordinaryDiagnosticRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $ordinaryDiagnosticRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog))
        if (@($ordinaryDiagnosticRecord.failures).Count -ne 0 -or @($ordinaryDiagnosticRecord.warnings).Count -ne 1) {
            throw "Ordinary validation warnings must remain warnings."
        }
        $timingDiagnosticRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $timingDiagnosticRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog)) -WarningsAreFailures
        if ($timingDiagnosticRecord.status -ne "FAIL") {
            throw "TimingValidation must fail on validation warnings."
        }
        Set-Content -LiteralPath $diagnosticLog -Value "[Vulkan Validation] - ERROR" -Encoding UTF8
        $errorDiagnosticRecord = New-RunRecord "Editor" "Vulkan" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $errorDiagnosticRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog)) -WarningsAreFailures
        if ($errorDiagnosticRecord.status -ne "FAIL") {
            throw "TimingValidation must fail on validation errors."
        }
        Set-Content -LiteralPath $diagnosticLog -Value "[DX12 Validation][DXGI][Present] WARNING : Category=1 MessageID=2 Message=test" -Encoding UTF8
        $ordinaryDx12WarningRecord = New-RunRecord "Editor" "DX12" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $ordinaryDx12WarningRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog))
        if (@($ordinaryDx12WarningRecord.failures).Count -ne 0 -or @($ordinaryDx12WarningRecord.warnings).Count -ne 1) {
            throw "Ordinary DX12 validation warnings must remain warnings."
        }
        $timingDx12WarningRecord = New-RunRecord "Editor" "DX12" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $timingDx12WarningRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog)) -WarningsAreFailures
        if ($timingDx12WarningRecord.status -ne "FAIL") {
            throw "TimingValidation must fail on the actual DX12 validation warning format."
        }
        Set-Content -LiteralPath $diagnosticLog -Value "[DX12 Validation][D3D12][BeginFrame] ERROR : Category=1 MessageID=2 Message=test" -Encoding UTF8
        $timingDx12ErrorRecord = New-RunRecord "Editor" "DX12" "Editor.exe" "telemetry.json" "stdout.log" "stderr.log"
        Test-LogForDiagnostics -Record $timingDx12ErrorRecord -LogFiles @((Get-Item -LiteralPath $diagnosticLog)) -WarningsAreFailures
        if ($timingDx12ErrorRecord.status -ne "FAIL") {
            throw "TimingValidation must fail on the actual DX12 validation error format."
        }
        Write-Host "Task 7 timing validation plan PASS"

        $cameraEntity = ConvertFrom-Json '{"camera":{"primary":true},"id":2,"name":"Camera","parent":1,"transform":{"position":[0.0,0.0,0.0],"rotation_euler_degrees":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}}'
        $environmentEntity = ConvertFrom-Json '{"environment":{"active":true},"id":3,"name":"Environment","parent":1,"transform":{"position":[0.0,0.0,0.0],"rotation_euler_degrees":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}}'
        $lightEntity = ConvertFrom-Json '{"id":4,"light":{"sunlight":true},"name":"Light","parent":1,"transform":{"position":[0.0,0.0,0.0],"rotation_euler_degrees":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}}'
        $renderConfig = ConvertFrom-Json '{"tonemap":{"exposure":1.0}}'
        $canonicalContract = [PSCustomObject][ordered]@{
            camera_entity = $cameraEntity
            environment_entity = $environmentEntity
            light_entity = $lightEntity
            render_config = $renderConfig
        }
        $canonicalJson = $canonicalContract | ConvertTo-Json -Depth 100 -Compress
        $manifestPath = Join-Path $task7Root "manifest.json"
        $scenePath = Join-Path $task7Root "TerrainPerfEmpty.scene.json"
        $scene = [PSCustomObject][ordered]@{
            entities = @(
                (ConvertFrom-Json '{"id":1,"name":"TerrainPerfRoot","parent":0,"transform":{"position":[0.0,0.0,0.0],"rotation_euler_degrees":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}}'),
                $cameraEntity,
                $environmentEntity,
                $lightEntity
            )
            name = "TerrainPerfEmpty"
            next_entity_id = 5
            scene_config = $renderConfig
            version = 5
        }
        $manifest = [PSCustomObject][ordered]@{
            schema_version = 1
            scene = "TerrainPerfEmpty.scene.json"
            hash_algorithm = "SHA-256"
            canonical_contract = $canonicalContract
            canonical_contract_json = $canonicalJson
            canonical_contract_sha256 = Get-Utf8Sha256 -Value $canonicalJson
        }
        $scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        if ((Assert-EmptyManifestHash -ManifestPath $manifestPath) -ne $manifest.canonical_contract_sha256) {
            throw "Task 7 manifest validation did not return its recomputed UTF-8 SHA-256."
        }
        $manifest.canonical_contract_sha256 = "0" * 64
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $hashMismatchRejected = $false
        try {
            Assert-EmptyManifestHash -ManifestPath $manifestPath | Out-Null
        }
        catch {
            $hashMismatchRejected = $_.Exception.Message -match "SHA-256 mismatch"
        }
        if (-not $hashMismatchRejected) {
            throw "Task 7 manifest validation accepted a canonical contract hash mismatch."
        }
        $manifest.canonical_contract_json = "not-json"
        $manifest.canonical_contract_sha256 = Get-Utf8Sha256 -Value $manifest.canonical_contract_json
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $invalidCanonicalJsonRejected = $false
        try {
            Assert-EmptyManifestHash -ManifestPath $manifestPath | Out-Null
        }
        catch {
            $invalidCanonicalJsonRejected = $_.Exception.Message -match "not valid JSON"
        }
        if (-not $invalidCanonicalJsonRejected) {
            throw "Task 7 manifest validation accepted invalid canonical_contract_json text."
        }

        $manifest.canonical_contract_json = $canonicalJson
        $manifest.canonical_contract_sha256 = Get-Utf8Sha256 -Value $canonicalJson
        $manifest.canonical_contract = $canonicalJson | ConvertFrom-Json
        $manifest.canonical_contract.camera_entity.id = 99
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $objectDriftRejected = $false
        try {
            Assert-EmptyManifestHash -ManifestPath $manifestPath | Out-Null
        }
        catch {
            $objectDriftRejected = $_.Exception.Message -match "canonical_contract does not match"
        }
        if (-not $objectDriftRejected) {
            throw "Task 7 manifest validation accepted canonical_contract object drift."
        }

        $manifest.canonical_contract = $canonicalJson | ConvertFrom-Json
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $scene.entities[1].id = 99
        $scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
        $sceneDriftRejected = $false
        try {
            Assert-EmptyManifestHash -ManifestPath $manifestPath | Out-Null
        }
        catch {
            $sceneDriftRejected = $_.Exception.Message -match "scene does not match"
        }
        if (-not $sceneDriftRejected) {
            throw "Task 7 manifest validation accepted fixed scene contract drift."
        }

        $contractProbePath = Join-Path $task7Root "feasibility-contract.json"
        $feasibilityContract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $contractProbePath -Encoding UTF8
        Get-EmptyFeasibilityContract -ContractPath $contractProbePath | Out-Null
        foreach ($invalidContractCase in @("schema", "width", "height", "immutable", "blessable")) {
            $invalidContract = $feasibilityContract | ConvertTo-Json -Depth 8 | ConvertFrom-Json
            switch ($invalidContractCase) {
                "schema" { $invalidContract.schema_version = "1"; break }
                "width" { $invalidContract.render_output.width = "2560"; break }
                "height" { $invalidContract.render_output.height = "1440"; break }
                "immutable" { $invalidContract.immutable = "true"; break }
                "blessable" { $invalidContract.blessable = "false"; break }
            }
            $invalidContract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $contractProbePath -Encoding UTF8
            $invalidContractRejected = $false
            try {
                Get-EmptyFeasibilityContract -ContractPath $contractProbePath | Out-Null
            }
            catch {
                $invalidContractRejected = $true
            }
            if (-not $invalidContractRejected) {
                throw "Task 7 feasibility contract accepted non-native '$invalidContractCase' JSON field type."
            }
        }
        Write-Host "Task 7 manifest hash PASS"
    }
    finally {
        Remove-Item -LiteralPath $task7Root -Recurse -Force -ErrorAction SilentlyContinue
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
if ($NoTracy -and $TimingValidation) {
    throw "-NoTracy cannot be combined with -TimingValidation."
}
if ($TimingValidation) {
    if ($Scenario -ne "Empty") {
        throw "-TimingValidation requires -Scenario Empty."
    }
    if ($BlessBaseline) {
        throw "-TimingValidation cannot be used with -BlessBaseline."
    }
    if (-not [string]::IsNullOrWhiteSpace($Configuration) -and $Configuration -ne "Debug") {
        throw "-TimingValidation requires -Configuration Debug."
    }
    $Configuration = "Debug"
}
elseif ($NoTracy) {
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
elseif ($Scenario -eq "Empty") {
    if (-not [string]::IsNullOrWhiteSpace($Configuration) -and $Configuration -ne "Release") {
        throw "ordinary -Scenario Empty requires -Configuration Release."
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
$statePaths = @(
    $engineConfig,
    (Join-Path $repoRoot "product/config/editor/EditorSettings.json"),
    (Join-Path $repoRoot "product/config/editor/ViewportLayout.json"),
    (Join-Path $repoRoot "product/config/editor/imgui.ini")
)
$stateSnapshots = @(New-StateFileSnapshots -Paths $statePaths -BackupRoot (Join-Path $reportRoot "state-backups"))

$emptyManifestPath = $null
$emptyManifestSha256 = $null
$feasibilityContractPath = $null
$feasibilityContract = $null
if ($Scenario -eq "Empty") {
    $emptyScenePath = Join-Path $repoRoot "product/assets/scenes/TerrainPerfEmpty.scene.json"
    if (-not (Test-Path -LiteralPath $emptyScenePath)) {
        throw "Missing fixed Empty scenario scene: $emptyScenePath"
    }
    $emptyManifestPath = Join-Path $repoRoot "product/assets/scenes/TerrainPerfEmpty.manifest.json"
    $emptyManifestSha256 = Assert-EmptyManifestHash -ManifestPath $emptyManifestPath
    $feasibilityContractPath = Join-Path $repoRoot "tools/perf/terrain_feasibility_contract.json"
    $feasibilityContract = Get-EmptyFeasibilityContract -ContractPath $feasibilityContractPath
}

$noTracyPlan = $null
if ($NoTracy) {
    $noTracyPlan = @(New-NoTracyCommandPlan -MSBuildPath (Get-MSBuildPath))
}
$gateTargets = if ($Scenario -eq "Empty" -and $NoTracy) {
    @(Get-NoTracyGateTargetsFromPlan -Plan $noTracyPlan)
}
elseif ($Scenario -eq "Empty") {
    @([PSCustomObject]@{ target = "Editor"; backends = @("Vulkan", "DX12") })
}
else {
    @($profileConfig.targets)
}
$runPlan = @(New-PerfGateRunPlan `
    -Targets $gateTargets `
    -ProfileConfig $profileConfig `
    -Profile $Profile `
    -Configuration $Configuration `
    -RepoRoot $repoRoot `
    -ReportRoot $reportRoot `
    -Scenario $Scenario `
    -TimingValidation:$TimingValidation)

$runBody = {
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
        -RunPlan $runPlan `
        -ProfileConfig $profileConfig `
        -Baseline $baseline `
        -Profile $Profile `
        -Configuration $Configuration `
        -RepoRoot $repoRoot `
        -EngineConfig $engineConfig `
        -StateSnapshots $stateSnapshots `
        -FeasibilityContract $feasibilityContract `
        -DryRun:$DryRun `
        -RequireSchemaV2:$NoTracy `
        -EmptyScenario:($Scenario -eq "Empty") `
        -TimingValidation:$TimingValidation
}

$stateProtectedBody = {
    if ($NoTracy) {
        Invoke-WithRequiredRestore `
            -Body $runBody `
            -Restore {
            Invoke-NoTracyCommandPhase `
                -Plan $noTracyPlan `
                -Phase "RestoreStandard" `
                -RepoRoot $repoRoot `
                -BuildLogRoot $buildLogRoot `
                -ContinueAfterFailure
            }
    }
    else {
        & $runBody
    }
}
$records = @(Invoke-WithRequiredRestore `
    -Body $stateProtectedBody `
    -Restore { Restore-StateFileSnapshots -Snapshots $stateSnapshots })

if ($NoTracy) {
    Assert-NoTracyRunRecords -Records $records
}

$overall = Get-PerfGateOverallStatus -Records $records

$summary = [PSCustomObject]@{
    schema_version = 1
    profile = $Profile
    configuration = $Configuration
    scenario = $Scenario
    timing_validation = [bool]$TimingValidation
    status = $overall
    baseline_path = $baselinePath
    baseline_blessed = $false
    empty_manifest_path = $emptyManifestPath
    empty_manifest_sha256 = $emptyManifestSha256
    feasibility_contract_path = $feasibilityContractPath
    report_root = $reportRoot
    run_plan = $runPlan
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
$markdown += "Scenario: $(if ([string]::IsNullOrWhiteSpace($Scenario)) { 'Standard matrix' } else { $Scenario })"
$markdown += "Timing validation: $([bool]$TimingValidation)"
$markdown += ""
$markdown += "Baseline: $baselinePath"
if ($BlessBaseline) {
    $markdown += "Baseline blessed: $($summary.baseline_blessed)"
}
$markdown += ""
$markdown += "## Run plan"
$markdown += ""
foreach ($step in $runPlan) {
    $markdown += "- $($step.command_line)"
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
        [Math]::Round([double]$record.gpu_frame_time_p95_ms, 4),
        $record.feasibility_contract_status,
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
    -Headers @("Target", "Backend", "Status", "Frames", "CPU Avg ms", "CPU Avg delta", "CPU P95 ms", "GPU P95 ms", "Feasibility", "CPU P95 delta", "CPU P99 delta", "Private MB", "Private delta", "Heap MB", "Heap delta", "Draw delta", "Failures", "Warnings") `
    -Alignments @("Left", "Left", "Left", "Right", "Right", "Right", "Right", "Right", "Left", "Right", "Right", "Right", "Right", "Right", "Right", "Right", "Left", "Left") `
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
