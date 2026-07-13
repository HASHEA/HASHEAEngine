Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runnerScript = Join-Path $repoRoot.Path "scripts\RunPerfGate.ps1"

$output = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest -NoTracy -Scenario Empty 2>&1
$exitCode = $LASTEXITCODE
$outputText = ($output | Out-String)

if ($exitCode -ne 0) {
    throw "RunPerfGate self-test failed with exit code $exitCode.`n$outputText"
}

if ($outputText -notmatch "RunPerfGate self-test PASS") {
    throw "RunPerfGate self-test did not report PASS.`n$outputText"
}

$standardOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest 2>&1
$standardExitCode = $LASTEXITCODE
$standardOutputText = ($standardOutput | Out-String)
if ($standardExitCode -ne 0 -or $standardOutputText -notmatch "RunPerfGate self-test PASS") {
    throw "RunPerfGate ordinary compatibility self-test failed.`n$standardOutputText"
}

$emptyOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest -Scenario Empty -Configuration Release -DryRun 2>&1
$emptyExitCode = $LASTEXITCODE
$emptyOutputText = ($emptyOutput | Out-String)
if ($emptyExitCode -ne 0 -or $emptyOutputText -notmatch "RunPerfGate self-test PASS") {
    throw "RunPerfGate Empty scenario self-test failed.`n$emptyOutputText"
}

$timingOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest -Scenario Empty -Configuration Debug -TimingValidation -DryRun 2>&1
$timingExitCode = $LASTEXITCODE
$timingOutputText = ($timingOutput | Out-String)
if ($timingExitCode -ne 0 -or $timingOutputText -notmatch "RunPerfGate self-test PASS") {
    throw "RunPerfGate TimingValidation self-test failed.`n$timingOutputText"
}

foreach ($requiredMarker in @(
    "Task 7 Empty run plan PASS",
    "Task 7 telemetry contract PASS",
    "Task 7 state restoration PASS",
    "Task 7 manifest hash PASS"
)) {
    if ($emptyOutputText -notmatch [regex]::Escape($requiredMarker)) {
        throw "RunPerfGate Empty self-test did not report '$requiredMarker'.`n$emptyOutputText"
    }
}
if ($timingOutputText -notmatch [regex]::Escape("Task 7 timing validation plan PASS")) {
    throw "RunPerfGate TimingValidation self-test did not verify its run plan.`n$timingOutputText"
}

$requiredNoTracyPlan = @(
    "premake5.exe --no-tracy vs2022",
    "InvokeMSBuild.ps1 -MSBuildPath <msbuild> -SolutionPath AshEngine.sln -Target Clean -Configuration Release -Platform x64",
    "build_editor.bat Release x64",
    "Editor Vulkan Empty Release",
    "Editor DX12 Empty Release",
    "premake5.exe vs2022"
)
foreach ($expectedStep in $requiredNoTracyPlan) {
    if ($outputText -notmatch [regex]::Escape($expectedStep)) {
        throw "RunPerfGate no-Tracy plan did not contain '$expectedStep'.`n$outputText"
    }
}
if ($outputText -notmatch [regex]::Escape("Perf gate report ids are high-resolution and unique")) {
    throw "RunPerfGate self-test did not verify collision-resistant report ids.`n$outputText"
}

$previousErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    $unsafeOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -SelfTest -NoTracy -Scenario Empty -SkipBuild 2>&1
    $unsafeExitCode = $LASTEXITCODE
    $unsafeOutputText = ($unsafeOutput | Out-String)
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
if ($unsafeExitCode -eq 0) {
    throw "RunPerfGate accepted unsafe -NoTracy -SkipBuild.`n$unsafeOutputText"
}
if ($unsafeOutputText -notmatch [regex]::Escape("-NoTracy cannot be used with -SkipBuild")) {
    throw "RunPerfGate rejected -NoTracy -SkipBuild without the expected safety diagnostic.`n$unsafeOutputText"
}

function Assert-RejectedInvocation {
    param(
        [string[]]$Arguments,
        [string]$ExpectedDiagnostic
    )

    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $rejectedOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript @Arguments 2>&1
        $rejectedExitCode = $LASTEXITCODE
        $rejectedOutputText = ($rejectedOutput | Out-String)
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($rejectedExitCode -eq 0 -or $rejectedOutputText -notmatch [regex]::Escape($ExpectedDiagnostic)) {
        throw "RunPerfGate did not safely reject '$($Arguments -join ' ')'.`n$rejectedOutputText"
    }
}

Assert-RejectedInvocation -Arguments @("-SelfTest", "-NoTracy", "-Scenario", "Empty", "-DryRun") -ExpectedDiagnostic "-NoTracy cannot be used with -DryRun"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-NoTracy", "-Scenario", "Empty", "-BlessBaseline") -ExpectedDiagnostic "-NoTracy cannot be used with -BlessBaseline"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-NoTracy", "-Scenario", "Sandbox") -ExpectedDiagnostic "Unsupported perf gate scenario 'Sandbox'"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-NoTracy", "-Scenario", "Empty", "-Configuration", "Debug") -ExpectedDiagnostic "-NoTracy requires -Configuration Release"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-Scenario", "Empty", "-Configuration", "Debug") -ExpectedDiagnostic "ordinary -Scenario Empty requires -Configuration Release"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-Scenario", "Empty", "-Configuration", "Release", "-TimingValidation") -ExpectedDiagnostic "-TimingValidation requires -Configuration Debug"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-TimingValidation", "-Configuration", "Debug") -ExpectedDiagnostic "-TimingValidation requires -Scenario Empty"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-NoTracy", "-Scenario", "Empty", "-TimingValidation") -ExpectedDiagnostic "-NoTracy cannot be combined with -TimingValidation"
Assert-RejectedInvocation -Arguments @("-SelfTest", "-Scenario", "Empty", "-TimingValidation", "-BlessBaseline") -ExpectedDiagnostic "-TimingValidation cannot be used with -BlessBaseline"

Write-Host "TestRunPerfGate PASS"
