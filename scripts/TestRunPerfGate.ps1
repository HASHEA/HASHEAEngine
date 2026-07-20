Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runnerScript = Join-Path $repoRoot.Path "scripts\RunPerfGate.ps1"
$runnerLauncher = Join-Path $repoRoot.Path "RunPerfGate.bat"
$menuScript = Join-Path $repoRoot.Path "scripts\RunPerfGateMenu.ps1"

$output = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest 2>&1
$exitCode = $LASTEXITCODE
$outputText = ($output | Out-String)

if ($exitCode -ne 0) {
    throw "RunPerfGate self-test failed with exit code $exitCode.`n$outputText"
}

if ($outputText -notmatch "RunPerfGate self-test PASS") {
    throw "RunPerfGate self-test did not report PASS.`n$outputText"
}

$profilesPath = Join-Path $repoRoot.Path "tools\perf\perf_gate_profiles.json"
$profileDocument = Get-Content -Raw -LiteralPath $profilesPath | ConvertFrom-Json
$vegetationProfile = $profileDocument.profiles.VegetationFullPipeline
$comparisonThresholdsProperty = $vegetationProfile.PSObject.Properties["comparison_thresholds"]
if ($null -eq $comparisonThresholdsProperty) {
    throw "Gate A RED: VegetationFullPipeline is missing comparison_thresholds."
}
$comparisonThresholds = $comparisonThresholdsProperty.Value
foreach ($expectedThreshold in @(
    [PSCustomObject]@{ name = "cpu_frame_time_avg_ms"; relative = 5.0; absolute = 0.25 },
    [PSCustomObject]@{ name = "cpu_frame_time_p95_ms"; relative = 8.0; absolute = 0.50 },
    [PSCustomObject]@{ name = "cpu_frame_time_p99_ms"; relative = 12.0; absolute = 1.00 },
    [PSCustomObject]@{ name = "process_private_bytes_peak_mb"; relative = 5.0; absolute = 128.0 },
    [PSCustomObject]@{ name = "engine_heap_peak_mb"; relative = 10.0; absolute = 1.0 },
    [PSCustomObject]@{ name = "draw_calls_avg"; relative = 0.0; absolute = 0.0 },
    [PSCustomObject]@{ name = "gpu_frame_avg_ms"; relative = 5.0; absolute = 0.25 },
    [PSCustomObject]@{ name = "gpu_frame_p95_ms"; relative = 8.0; absolute = 0.50 }
)) {
    $thresholdProperty = $comparisonThresholds.PSObject.Properties[$expectedThreshold.name]
    if ($null -eq $thresholdProperty -or
        [double]$thresholdProperty.Value.relative_percent -ne $expectedThreshold.relative -or
        [double]$thresholdProperty.Value.absolute_floor -ne $expectedThreshold.absolute) {
        throw "VegetationFullPipeline threshold '$($expectedThreshold.name)' does not match the approved Gate A contract."
    }
}

$tiers = @($comparisonThresholds.gpu_pass_tiers)
if ($tiers.Count -ne 3 -or
    [double]$tiers[0].minimum_baseline_avg_ms -ne 0.5 -or [double]$tiers[0].avg.relative_percent -ne 8.0 -or [double]$tiers[0].avg.absolute_floor -ne 0.10 -or [double]$tiers[0].p95.relative_percent -ne 12.0 -or [double]$tiers[0].p95.absolute_floor -ne 0.20 -or
    [double]$tiers[1].minimum_baseline_avg_ms -ne 0.1 -or [double]$tiers[1].avg.relative_percent -ne 15.0 -or [double]$tiers[1].avg.absolute_floor -ne 0.05 -or [double]$tiers[1].p95.relative_percent -ne 20.0 -or [double]$tiers[1].p95.absolute_floor -ne 0.10 -or
    [double]$tiers[2].minimum_baseline_avg_ms -ne 0.0 -or $null -ne $tiers[2].avg.PSObject.Properties["relative_percent"] -or [double]$tiers[2].avg.absolute_floor -ne 0.03 -or $null -ne $tiers[2].p95.PSObject.Properties["relative_percent"] -or [double]$tiers[2].p95.absolute_floor -ne 0.05) {
    throw "VegetationFullPipeline GPU pass tiers do not match the approved Gate A contract."
}

$runBatProbeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ash-run-bat-test-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N"))
try {
    $runBatProbeBin = Join-Path $runBatProbeRoot "product\bin64\Debug-windows-x86_64"
    $runBatProbeConfig = Join-Path $runBatProbeRoot "product\config"
    New-Item -ItemType Directory -Force -Path $runBatProbeBin, $runBatProbeConfig | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot.Path "run.bat") -Destination (Join-Path $runBatProbeRoot "run.bat")
    "[RHI]`nBackend=Vulkan" | Set-Content -LiteralPath (Join-Path $runBatProbeConfig "Engine.ini") -Encoding ASCII

    $probeExecutable = Join-Path $runBatProbeBin "Sandbox.exe"
    Add-Type -TypeDefinition @'
using System;

public static class RunBatArgumentProbe
{
    public static int Main(string[] args)
    {
        Console.WriteLine("ARG_COUNT=" + args.Length);
        for (int index = 0; index < args.Length; ++index)
        {
            Console.WriteLine("ARG_" + index + "=" + args[index]);
        }
        return 0;
    }
}
'@ -Language CSharp -OutputAssembly $probeExecutable -OutputType ConsoleApplication

    $probeArguments = @(1..12 | ForEach-Object { "--probe-$_=value-$_" })
    $quotedProbeArguments = @($probeArguments | ForEach-Object { '"' + $_ + '"' })
    $probeCommand = '"' + (Join-Path $runBatProbeRoot "run.bat") + '" sandbox current Debug ' + ($quotedProbeArguments -join ' ')
    $probeOutput = & cmd.exe /d /s /c $probeCommand 2>&1
    $probeExitCode = $LASTEXITCODE
    $probeText = ($probeOutput | Out-String)
    if ($probeExitCode -ne 0) {
        throw "run.bat argument probe failed with exit code $probeExitCode.`n$probeText"
    }
    if ($probeText -notmatch "(?m)^ARG_COUNT=12\s*$") {
        throw "run.bat did not forward all 12 application arguments.`n$probeText"
    }
    for ($index = 0; $index -lt $probeArguments.Count; ++$index) {
        $expectedLine = "ARG_$index=$($probeArguments[$index])"
        if ($probeText -notlike "*$expectedLine*") {
            throw "run.bat did not preserve application argument '$($probeArguments[$index])'.`n$probeText"
        }
    }

    Copy-Item -LiteralPath $probeExecutable -Destination (Join-Path $runBatProbeBin "Editor.exe")
    $matrixProbeCommand = '"' + (Join-Path $runBatProbeRoot "run.bat") + '" all Debug ' + ($quotedProbeArguments -join ' ')
    $matrixProbeOutput = & cmd.exe /d /s /c $matrixProbeCommand 2>&1
    $matrixProbeExitCode = $LASTEXITCODE
    $matrixProbeText = ($matrixProbeOutput | Out-String)
    if ($matrixProbeExitCode -ne 0) {
        throw "run.bat matrix argument probe failed with exit code $matrixProbeExitCode.`n$matrixProbeText"
    }
    if ([regex]::Matches($matrixProbeText, "(?m)^ARG_COUNT=12\s*$").Count -ne 4) {
        throw "run.bat matrix did not forward all 12 application arguments to all four runs.`n$matrixProbeText"
    }
    for ($index = 0; $index -lt $probeArguments.Count; ++$index) {
        $expectedLine = "ARG_$index=$($probeArguments[$index])"
        if ([regex]::Matches($matrixProbeText, "(?m)^$([regex]::Escape($expectedLine))\s*$").Count -ne 4) {
            throw "run.bat matrix did not preserve application argument '$($probeArguments[$index])' for all four runs.`n$matrixProbeText"
        }
    }
}
finally {
    Remove-Item -LiteralPath $runBatProbeRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$dryRunOutput = & $runnerLauncher -Profile VegetationFullPipeline -DryRun 2>&1
$dryRunExitCode = $LASTEXITCODE
$dryRunText = ($dryRunOutput | Out-String)
if ($dryRunExitCode -ne 0) {
    throw "VegetationFullPipeline dry run failed with exit code $dryRunExitCode.`n$dryRunText"
}

$dryRunLines = @($dryRunOutput | ForEach-Object { [string]$_ } | Where-Object { $_ -like "DRY_RUN:*" })
if ($dryRunLines.Count -ne 2) {
    throw "Expected exactly two VegetationFullPipeline dry-run commands, got $($dryRunLines.Count).`n$dryRunText"
}
foreach ($requiredText in @(
    "BUILD_PLAN: build_sandbox.bat Release x64",
    "--scene=product/assets/scenes/VegetationBaseline.scene.json",
    "--window-width=2560",
    "--window-height=1440",
    "--perf-gate-vsync=off",
    "--perf-gate-validation=off",
    "--perf-gate-gpu-timing=on",
    "--perf-gate-drain-seconds=5"
)) {
    if ($dryRunText -notlike "*$requiredText*") {
        throw "VegetationFullPipeline dry run is missing '$requiredText'.`n$dryRunText"
    }
}
if ($dryRunText -notlike "*--rhi=vulkan*" -or $dryRunText -notlike "*--rhi=dx12*") {
    throw "VegetationFullPipeline dry run must propagate both process-local RHIs.`n$dryRunText"
}
if ($dryRunText -like "*build_editor.bat*" -or $dryRunText -match "DRY_RUN:.*Editor\.exe") {
    throw "VegetationFullPipeline dry run must not build or launch Editor.`n$dryRunText"
}

$previewOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $menuScript -Preview 2>&1
$previewExitCode = $LASTEXITCODE
$previewText = ($previewOutput | Out-String)
if ($previewExitCode -ne 0) {
    throw "RunPerfGate menu preview failed with exit code $previewExitCode.`n$previewText"
}
if ($previewText -notlike "*Available profiles: VegetationFullPipeline, Standard*") {
    throw "Menu preview did not load the shared profile catalog.`n$previewText"
}
if ($previewText -notlike "*RunPerfGate.ps1*" -or $previewText -notlike "*-Profile Standard*") {
    throw "Menu preview did not emit the default runner command.`n$previewText"
}

Write-Host "TestRunPerfGate PASS"
