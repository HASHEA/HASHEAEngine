Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runnerScript = Join-Path $repoRoot.Path "scripts\RunPerfGate.ps1"
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

$dryRunOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -Profile VegetationFullPipeline -DryRun 2>&1
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
