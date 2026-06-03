Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runnerScript = Join-Path $repoRoot.Path "scripts\AIDevDoctor.ps1"
$timestamp = "self-test-" + [DateTimeOffset]::UtcNow.ToString("yyyyMMdd-HHmmss")
$planTimestamp = $timestamp + "-validate-plan"

$output = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -Mode Report -Timestamp $timestamp -IncludePerfGate 2>&1
$exitCode = $LASTEXITCODE
$outputText = ($output | Out-String)

if ($exitCode -ne 0) {
    throw "AIDevDoctor self-test failed with exit code $exitCode.`n$outputText"
}

$reportRoot = Join-Path $repoRoot.Path "Intermediate\test-reports\ai-dev\$timestamp"
$contextPath = Join-Path $reportRoot "context.json"
$reportPath = Join-Path $reportRoot "report.md"
$promptPath = Join-Path $reportRoot "prompt.md"
$validationPlanPath = Join-Path $reportRoot "validation-plan.md"

foreach ($path in @($contextPath, $reportPath, $promptPath, $validationPlanPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "AIDevDoctor self-test expected output file missing: $path"
    }
}

$context = Get-Content -LiteralPath $contextPath -Raw | ConvertFrom-Json
if (-not $context.repository.root) {
    throw "AIDevDoctor context.json is missing repository.root"
}
if (-not $context.risk.touched_subsystems) {
    throw "AIDevDoctor context.json is missing risk.touched_subsystems"
}
if (-not $context.outputs.report_path -or -not $context.outputs.prompt_path) {
    throw "AIDevDoctor context.json is missing output paths"
}
if (-not $context.outputs.validation_plan_path) {
    throw "AIDevDoctor context.json is missing validation_plan_path"
}
if (-not $context.evidence_freshness) {
    throw "AIDevDoctor context.json is missing evidence_freshness"
}
if (-not $context.evidence_freshness.summary) {
    throw "AIDevDoctor context.json is missing evidence_freshness.summary"
}
if (-not $context.evidence_freshness.perf_gate) {
    throw "AIDevDoctor context.json is missing evidence_freshness.perf_gate"
}
if (-not $context.evidence_freshness.logs) {
    throw "AIDevDoctor context.json is missing evidence_freshness.logs"
}
if (-not $context.diagnostics) {
    throw "AIDevDoctor context.json is missing diagnostics"
}
if (-not $context.diagnostics.findings) {
    throw "AIDevDoctor context.json is missing diagnostics.findings"
}
if (-not $context.diagnostics.next_steps) {
    throw "AIDevDoctor context.json is missing diagnostics.next_steps"
}
if (-not $context.ai_review_contract) {
    throw "AIDevDoctor context.json is missing ai_review_contract"
}
if (-not $context.ai_review_contract.boundaries) {
    throw "AIDevDoctor context.json is missing ai_review_contract.boundaries"
}
if (@($context.ai_review_contract.boundaries | Where-Object { $_ -match "project/src/editor" }).Count -eq 0) {
    throw "AIDevDoctor ai_review_contract should include the Editor boundary"
}
if (-not $context.ai_review_contract.required_behaviors) {
    throw "AIDevDoctor context.json is missing ai_review_contract.required_behaviors"
}
if (-not $context.change_signals) {
    throw "AIDevDoctor context.json is missing change_signals"
}
if (-not $context.change_signals.summary) {
    throw "AIDevDoctor context.json is missing change_signals.summary"
}
if (-not $context.change_groups) {
    throw "AIDevDoctor context.json is missing change_groups"
}
if (-not $context.change_groups.summary) {
    throw "AIDevDoctor context.json is missing change_groups.summary"
}
if (-not $context.change_groups.items) {
    throw "AIDevDoctor context.json is missing change_groups.items"
}
$groupedPathCount = 0
foreach ($group in @($context.change_groups.items)) {
    $groupedPathCount += [int]$group.count
}
if ($groupedPathCount -ne @($context.git.dirty_paths).Count) {
    throw "AIDevDoctor change_groups count mismatch: grouped $groupedPathCount path(s), dirty_paths has $(@($context.git.dirty_paths).Count)"
}
if ($context.change_groups.summary -notmatch [regex]::Escape("Grouped $groupedPathCount dirty path(s)")) {
    throw "AIDevDoctor change_groups summary does not use the total grouped path count: $($context.change_groups.summary)"
}
if (-not $context.validation.plan) {
    throw "AIDevDoctor context.json is missing validation.plan"
}
if (-not $context.validation.plan.commands) {
    throw "AIDevDoctor context.json is missing validation.plan.commands"
}
if (-not $context.validation.plan.manual_checks) {
    throw "AIDevDoctor context.json is missing validation.plan.manual_checks"
}
if ([int]$context.validation.coverage_matrix.uncovered_required_count -gt 0) {
    $coveragePlanChecks = @($context.validation.plan.manual_checks | Where-Object { $_ -match "Validation Coverage Matrix" })
    if ($coveragePlanChecks.Count -eq 0) {
        throw "AIDevDoctor validation.plan.manual_checks should reference the Validation Coverage Matrix when required cells need attention"
    }
}
if (-not $context.validation.gaps) {
    throw "AIDevDoctor context.json is missing validation.gaps"
}
if (-not $context.validation.gaps.summary) {
    throw "AIDevDoctor context.json is missing validation.gaps.summary"
}
if (-not $context.validation.gaps.items) {
    throw "AIDevDoctor context.json is missing validation.gaps.items"
}
if (-not $context.validation.evidence_index) {
    throw "AIDevDoctor context.json is missing validation.evidence_index"
}
if (-not $context.validation.evidence_index.summary) {
    throw "AIDevDoctor context.json is missing validation.evidence_index.summary"
}
if (-not $context.validation.evidence_index.items) {
    throw "AIDevDoctor context.json is missing validation.evidence_index.items"
}
if (-not $context.validation.coverage_matrix) {
    throw "AIDevDoctor context.json is missing validation.coverage_matrix"
}
if (-not $context.validation.coverage_matrix.summary) {
    throw "AIDevDoctor context.json is missing validation.coverage_matrix.summary"
}
if (-not $context.validation.coverage_matrix.cells) {
    throw "AIDevDoctor context.json is missing validation.coverage_matrix.cells"
}
$coverageCells = @($context.validation.coverage_matrix.cells)
if ($coverageCells.Count -ne 4) {
    throw "AIDevDoctor validation.coverage_matrix should contain 4 target/backend cells, found $($coverageCells.Count)"
}
foreach ($expectedCell in @("Sandbox/Vulkan", "Sandbox/DX12", "Editor/Vulkan", "Editor/DX12")) {
    $cellParts = $expectedCell -split "/"
    $foundCell = @($coverageCells | Where-Object { $_.target -eq $cellParts[0] -and $_.backend -eq $cellParts[1] })
    if ($foundCell.Count -ne 1) {
        throw "AIDevDoctor validation.coverage_matrix missing cell: $expectedCell"
    }
    if (-not $foundCell[0].coverage_status) {
        throw "AIDevDoctor validation.coverage_matrix cell missing coverage_status: $expectedCell"
    }
}
$diffCommand = @($context.validation.plan.commands | Where-Object { $_.name -eq "diff-whitespace-check" } | Select-Object -First 1)
if ($diffCommand) {
    $untrackedPaths = @($context.git.status_entries |
        Where-Object { $_ -match "^\?\?\s+(.+)$" } |
        ForEach-Object { (($matches[1] -replace "\\", "/").Trim('"')) })
    foreach ($untrackedPath in $untrackedPaths) {
        if (-not [string]::IsNullOrWhiteSpace($untrackedPath) -and $diffCommand.command -match [regex]::Escape($untrackedPath)) {
            throw "AIDevDoctor diff-whitespace-check command should not include untracked path: $untrackedPath"
        }
    }
}
$runtimeConfigSignals = @($context.change_signals.items | Where-Object { $_.name -eq "runtime-config-change" })
foreach ($signal in $runtimeConfigSignals) {
    foreach ($example in @($signal.examples)) {
        if ($example -match "^(README\.md|docs/)") {
            throw "AIDevDoctor runtime-config-change should not be triggered by documentation text: $example"
        }
    }
}

$report = Get-Content -LiteralPath $reportPath -Raw
foreach ($expected in @("# AIDevDoctor Report", "## AI Review Contract", "## Risk Classification", "## Dirty Change Groups", "## Change Signals", "## Validation Evidence Index", "## Validation Coverage Matrix", "## Evidence Freshness", "## Validation Gaps", "## Diagnostic Findings", "## Suggested Next Steps", "## Validation Guidance", "## Validation Plan", "## Documentation Guidance")) {
    if ($report -notmatch [regex]::Escape($expected)) {
        throw "AIDevDoctor report.md missing section: $expected"
    }
}

$prompt = Get-Content -LiteralPath $promptPath -Raw
foreach ($expected in @("# AshEngine AI Development Context", "## AI Review Contract", "## Current Risk Summary", "## Dirty Change Groups", "## Change Signals", "## Validation Evidence Index", "## Validation Coverage Matrix", "## Evidence Freshness", "## Validation Gaps", "## Diagnostic Findings", "## Suggested Next Steps", "## Requested AI Review", "## Validation Plan")) {
    if ($prompt -notmatch [regex]::Escape($expected)) {
        throw "AIDevDoctor prompt.md missing section: $expected"
    }
}

$validationPlan = Get-Content -LiteralPath $validationPlanPath -Raw
foreach ($expected in @("# AIDevDoctor Validation Plan", "## Generated Commands", "## Manual Checks", "## AI Review Contract", "## Dirty Change Groups", "## Validation Evidence Index", "## Validation Coverage Matrix", "## Evidence Freshness", "## Validation Gaps", "## Risk Inputs")) {
    if ($validationPlan -notmatch [regex]::Escape($expected)) {
        throw "AIDevDoctor validation-plan.md missing section: $expected"
    }
}
if ($validationPlan -notmatch "(?s)## Manual Checks\s+\r?\n- ") {
    throw "AIDevDoctor validation-plan.md should list manual checks under the Manual Checks section"
}

$planOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript -Mode ValidatePlan -Timestamp $planTimestamp 2>&1
$planExitCode = $LASTEXITCODE
$planOutputText = ($planOutput | Out-String)
if ($planExitCode -ne 0) {
    throw "AIDevDoctor ValidatePlan self-test failed with exit code $planExitCode.`n$planOutputText"
}

$planRoot = Join-Path $repoRoot.Path "Intermediate\test-reports\ai-dev\$planTimestamp"
$planContextPath = Join-Path $planRoot "context.json"
$planOnlyPath = Join-Path $planRoot "validation-plan.md"
foreach ($path in @($planContextPath, $planOnlyPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "AIDevDoctor ValidatePlan expected output file missing: $path"
    }
}

$planContext = Get-Content -LiteralPath $planContextPath -Raw | ConvertFrom-Json
if ($planContext.mode -ne "ValidatePlan") {
    throw "AIDevDoctor ValidatePlan context mode mismatch: $($planContext.mode)"
}
if (-not $planContext.outputs.validation_plan_path) {
    throw "AIDevDoctor ValidatePlan context is missing validation_plan_path"
}

Write-Host "TestAIDevDoctor PASS"
