[CmdletBinding()]
param(
    [ValidateSet("Report", "ValidatePlan")]
    [string]$Mode = "Report",
    [string]$Timestamp = "",
    [switch]$IncludeLogs,
    [switch]$IncludePerfGate,
    [int]$MaxLogMatches = 80
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

function Invoke-Git {
    param(
        [string]$RepoRoot,
        [string[]]$Arguments
    )

    Push-Location $RepoRoot
    try {
        $output = & git @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            return @("git $($Arguments -join ' ') failed with exit code $exitCode", ($output | Out-String).Trim())
        }
        return @($output)
    }
    finally {
        Pop-Location
    }
}

function Convert-StatusLineToPath {
    param([string]$Line)

    if ([string]::IsNullOrWhiteSpace($Line) -or $Line.Length -lt 4) {
        return ""
    }

    $path = $Line.Substring(3).Trim()
    if ($path -match " -> ") {
        $parts = $path -split " -> "
        $path = $parts[$parts.Count - 1].Trim()
    }
    return ($path -replace "\\", "/").Trim('"')
}

function Read-JsonFile {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required AIDevDoctor file missing: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Test-ObjectProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    return ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name)
}

function Get-ObjectStringProperty {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Default = ""
    )

    if (Test-ObjectProperty -Object $Object -Name $Name) {
        return [string]$Object.$Name
    }
    return $Default
}

function ConvertTo-DateTimeOffsetOrNull {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }

    $parsed = [DateTimeOffset]::MinValue
    if ([DateTimeOffset]::TryParse($Value, [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Format-ListBlock {
    param([string[]]$Items)

    $clean = @($Items | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($clean.Count -eq 0) {
        return "- None"
    }
    return (($clean | ForEach-Object { "- $_" }) -join [Environment]::NewLine)
}

function Format-RuleBlock {
    param([object[]]$Rules)

    if ($Rules.Count -eq 0) {
        return "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($rule in $Rules) {
        $lines.Add(("- {0} [{1}] - {2}" -f $rule.name, $rule.risk, $rule.reason))
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ChangeSignalBlock {
    param([object[]]$Signals)

    if ($Signals.Count -eq 0) {
        return "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($signal in $Signals) {
        $examples = @($signal.examples)
        $exampleText = ""
        if ($examples.Count -gt 0) {
            $exampleText = " Examples: " + (($examples | Select-Object -First 3) -join "; ")
        }
        $lines.Add(("- [{0}] {1}/{2}: {3} Matches: {4}.{5}" -f $signal.severity, $signal.category, $signal.name, $signal.finding, $signal.match_count, $exampleText))
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ChangeGroupBlock {
    param([object]$ChangeGroups)

    $items = @($ChangeGroups.items)
    if ($items.Count -eq 0) {
        return "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$ChangeGroups.summary)
    $lines.Add("")
    foreach ($group in $items) {
        $paths = @($group.paths)
        $exampleText = (($paths | Select-Object -First 6) -join "; ")
        if ($paths.Count -gt 6) {
            $exampleText += "; ..."
        }
        $lines.Add(("- {0} [{1}] - {2} path(s). {3} Paths: {4}" -f $group.name, $group.intent, $group.count, $group.reason, $exampleText))
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-AIReviewContractBlock {
    param([object]$Contract)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$Contract.summary)
    $lines.Add("")
    $lines.Add("Policy sources:")
    foreach ($source in @($Contract.policy_sources)) {
        $lines.Add("- $source")
    }
    $lines.Add("")
    $lines.Add("Boundaries:")
    foreach ($boundary in @($Contract.boundaries)) {
        $lines.Add("- $boundary")
    }
    $lines.Add("")
    $lines.Add("Required behaviors:")
    foreach ($behavior in @($Contract.required_behaviors)) {
        $lines.Add("- $behavior")
    }
    $lines.Add("")
    $lines.Add("Review focus:")
    foreach ($focus in @($Contract.review_focus)) {
        $lines.Add("- $focus")
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationPlanBlock {
    param([object]$Plan)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$Plan.summary)
    $lines.Add("")
    $lines.Add("Commands:")

    $commands = @($Plan.commands)
    if ($commands.Count -eq 0) {
        $lines.Add("- None")
    }
    else {
        foreach ($command in $commands) {
            $lines.Add(('- [{0}] `{1}`' -f $command.name, $command.command))
            $lines.Add(("  Reason: {0}" -f $command.reason))
        }
    }

    $lines.Add("")
    $lines.Add("Manual checks:")
    $manualChecks = @($Plan.manual_checks)
    if ($manualChecks.Count -eq 0) {
        $lines.Add("- None")
    }
    else {
        foreach ($check in $manualChecks) {
            $lines.Add("- $check")
        }
    }

    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationCommandBlock {
    param([object]$Plan)

    $commands = @($Plan.commands)
    if ($commands.Count -eq 0) {
        return "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($command in $commands) {
        $lines.Add(('- [{0}] `{1}`' -f $command.name, $command.command))
        $lines.Add(("  Reason: {0}" -f $command.reason))
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationManualCheckBlock {
    param([object]$Plan)

    return Format-ListBlock @($Plan.manual_checks)
}

function Format-EvidenceFreshnessBlock {
    param([object]$Freshness)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$Freshness.summary)
    $lines.Add("")
    $lines.Add(("- Dirty baseline: {0}" -f $Freshness.dirty.summary))
    $lines.Add(("- PerfGate: [{0}] {1}" -f $Freshness.perf_gate.status, $Freshness.perf_gate.reason))
    $lines.Add(("- Logs: [{0}] {1}" -f $Freshness.logs.status, $Freshness.logs.reason))
    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationGapBlock {
    param([object]$Gaps)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$Gaps.summary)

    $items = @($Gaps.items)
    if ($items.Count -eq 0) {
        $lines.Add("")
        $lines.Add("- None")
        return ($lines -join [Environment]::NewLine)
    }

    $lines.Add("")
    foreach ($gap in $items) {
        $lines.Add(("- [{0}] {1}: {2}" -f $gap.severity, $gap.id, $gap.reason))
        $lines.Add(("  Next: {0}" -f $gap.next_step))
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationEvidenceIndexBlock {
    param([object]$EvidenceIndex)

    $items = @($EvidenceIndex.items)
    if ($items.Count -eq 0) {
        return [string]$EvidenceIndex.summary + [Environment]::NewLine + [Environment]::NewLine + "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$EvidenceIndex.summary)
    $lines.Add("")
    foreach ($item in $items) {
        $targetText = ""
        if (-not [string]::IsNullOrWhiteSpace([string]$item.target) -or -not [string]::IsNullOrWhiteSpace([string]$item.backend)) {
            $targetText = " target=$($item.target) backend=$($item.backend)"
        }
        $lines.Add(("- [{0}] {1} status={2}{3} modified={4}" -f $item.kind, $item.name, $item.status, $targetText, $item.modified_utc))
        if (-not [string]::IsNullOrWhiteSpace([string]$item.summary)) {
            $lines.Add(("  Summary: {0}" -f $item.summary))
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$item.path)) {
            $lines.Add(("  Path: {0}" -f $item.path))
        }
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-ValidationCoverageMatrixBlock {
    param([object]$CoverageMatrix)

    $cells = @($CoverageMatrix.cells)
    if ($cells.Count -eq 0) {
        return [string]$CoverageMatrix.summary + [Environment]::NewLine + [Environment]::NewLine + "- None"
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add([string]$CoverageMatrix.summary)
    $lines.Add("")
    foreach ($cell in $cells) {
        $requiredText = if ([bool]$cell.required) { "required" } else { "optional" }
        $freshText = if ([bool]$cell.fresh) { "fresh" } else { "not-fresh" }
        $lines.Add(("- [{0}] {1}/{2}: coverage={3}, evidence_status={4}, {5}, modified={6}" -f $requiredText, $cell.target, $cell.backend, $cell.coverage_status, $cell.evidence_status, $freshText, $cell.modified_utc))
        if (-not [string]::IsNullOrWhiteSpace([string]$cell.evidence_name)) {
            $lines.Add(("  Evidence: {0}" -f $cell.evidence_name))
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$cell.path)) {
            $lines.Add(("  Path: {0}" -f $cell.path))
        }
    }
    return ($lines -join [Environment]::NewLine)
}

function Format-Template {
    param(
        [string]$TemplatePath,
        [hashtable]$Values
    )

    $text = Get-Content -LiteralPath $TemplatePath -Raw
    foreach ($key in $Values.Keys) {
        $text = $text.Replace("{{" + $key + "}}", [string]$Values[$key])
    }
    return $text
}

function Get-HighestRisk {
    param(
        [object[]]$MatchedRules,
        [object]$ValidationRules
    )

    if ($MatchedRules.Count -eq 0) {
        return "none"
    }

    $rank = @{}
    for ($i = 0; $i -lt $ValidationRules.risk_order.Count; ++$i) {
        $rank[[string]$ValidationRules.risk_order[$i]] = $i
    }

    $highest = "none"
    foreach ($rule in $MatchedRules) {
        $risk = [string]$rule.risk
        if (-not $rank.ContainsKey($risk)) {
            continue
        }
        if ($rank[$risk] -gt $rank[$highest]) {
            $highest = $risk
        }
    }
    return $highest
}

function Get-MatchedRules {
    param(
        [string[]]$Paths,
        [object]$PathRules
    )

    $matchedByName = @{}
    foreach ($path in $Paths) {
        foreach ($rule in $PathRules.rules) {
            if ($path -match [string]$rule.path_regex) {
                $matchedByName[[string]$rule.name] = $rule
            }
        }
    }
    return @($matchedByName.Values)
}

function Get-DocumentationGuidance {
    param(
        [string[]]$Paths,
        [object]$DocRules,
        [object[]]$MatchedRules
    )

    $docs = New-Object System.Collections.Generic.List[string]
    foreach ($rule in $MatchedRules) {
        foreach ($doc in @($rule.docs)) {
            if (-not [string]::IsNullOrWhiteSpace($doc)) {
                $docs.Add([string]$doc)
            }
        }
    }

    foreach ($path in $Paths) {
        foreach ($rule in $DocRules.rules) {
            if ($path -match [string]$rule.path_regex) {
                foreach ($doc in @($rule.docs)) {
                    if (-not [string]::IsNullOrWhiteSpace($doc)) {
                        $docs.Add([string]$doc)
                    }
                }
            }
        }
    }

    $uniqueDocs = @($docs | Sort-Object -Unique)
    if ($uniqueDocs.Count -eq 0) {
        return @("No documentation update inferred from path rules.")
    }
    return @($uniqueDocs | ForEach-Object { "$_ - review whether current changes require an update" })
}

function Get-ValidationGuidance {
    param(
        [object[]]$MatchedRules,
        [object]$ValidationRules,
        [string]$HighestRisk
    )

    $items = New-Object System.Collections.Generic.List[string]
    foreach ($rule in $MatchedRules) {
        foreach ($validation in @($rule.validation)) {
            if (-not [string]::IsNullOrWhiteSpace($validation)) {
                $items.Add([string]$validation)
            }
        }
    }

    if ($items.Count -eq 0) {
        foreach ($entry in @($ValidationRules.default_guidance.$HighestRisk)) {
            $items.Add([string]$entry)
        }
    }

    return @($items | Sort-Object -Unique)
}

function Get-ChangeGroups {
    param(
        [string[]]$Paths,
        [object]$GroupRules
    )

    $groups = @{}
    $order = New-Object System.Collections.Generic.List[string]
    $dirtyPathCount = $Paths.Count

    foreach ($path in $Paths) {
        $matchedRule = $null
        foreach ($rule in $GroupRules.rules) {
            if ($path -match [string]$rule.path_regex) {
                $matchedRule = $rule
                break
            }
        }

        if ($null -eq $matchedRule) {
            $matchedRule = [PSCustomObject]@{
                name = "unclassified"
                label = "Unclassified"
                intent = "review-needed"
                reason = "No change-group rule matched this dirty path."
            }
        }

        $name = [string]$matchedRule.name
        if (-not $groups.ContainsKey($name)) {
            $pathsForGroup = New-Object System.Collections.Generic.List[string]
            $groups[$name] = [PSCustomObject]@{
                name = $name
                label = [string]$matchedRule.label
                intent = [string]$matchedRule.intent
                reason = [string]$matchedRule.reason
                paths = $pathsForGroup
            }
            $order.Add($name)
        }

        $groups[$name].paths.Add($path)
    }

    $items = New-Object System.Collections.Generic.List[object]
    foreach ($name in $order) {
        $group = $groups[$name]
        $groupPaths = @($group.paths | Sort-Object -Unique)
        $items.Add([PSCustomObject]@{
            name = [string]$group.name
            label = [string]$group.label
            intent = [string]$group.intent
            reason = [string]$group.reason
            count = $groupPaths.Count
            paths = @($groupPaths)
        })
    }

    $summary = "No dirty paths to group."
    if ($dirtyPathCount -gt 0) {
        $summary = "Grouped $dirtyPathCount dirty path(s) into $($items.Count) change group(s)."
    }

    return [PSCustomObject]@{
        summary = $summary
        items = @($items.ToArray())
    }
}

function Get-AIReviewContract {
    param([string]$RepoRoot)

    $policySources = New-Object System.Collections.Generic.List[string]
    foreach ($relativePath in @("AGENTS.md", "README.md", "docs/AIDevDoctor.md")) {
        if (Test-Path -LiteralPath (Join-Path $RepoRoot $relativePath)) {
            $policySources.Add($relativePath)
        }
    }

    return [PSCustomObject]@{
        summary = "Use this AIDevDoctor package as AI review context, not as proof that the workspace is correct or validated."
        policy_sources = @($policySources.ToArray())
        boundaries = @(
            "Do not modify Editor code under project/src/editor unless the user explicitly overrides that boundary for the task.",
            "Respect the Engine / Editor boundary; Engine-side work should stay in Engine modules and public Engine-facing abstractions.",
            "Do not push backend-specific RHI details directly into Editor-facing code.",
            "Do not revert, overwrite, or attribute unrelated dirty files without explicit user direction."
        )
        required_behaviors = @(
            "Treat validation.plan as recommended work only; run fresh verification before claiming a build, test, backend, or PerfGate result passed.",
            "For Engine, runtime, rendering, RHI, startup/shutdown, asset/scene, configuration, or other shared-path changes, expect Sandbox and Editor coverage on Vulkan and DX12 unless the scope is explicitly narrowed.",
            "Keep generated reports, logs, captures, and temporary diagnostics under the appropriate Intermediate subdirectory.",
            "Update the root README.md in the same task when changing code, assets, config, build, validation, architecture, or workflow behavior."
        )
        review_focus = @(
            "Separate current-task changes from pre-existing dirty workspace state.",
            "Use dirty change groups, change signals, evidence freshness, validation gaps, and the coverage matrix before recommending validation scope.",
            "Call out missing evidence separately from failed evidence."
        )
    }
}

function Format-PowerShellArgument {
    param([string]$Value)

    return "'" + ($Value -replace "'", "''") + "'"
}

function Format-DirtyPathArguments {
    param([string[]]$Paths)

    $items = @($Paths | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 32)
    if ($items.Count -eq 0) {
        return ""
    }
    return (($items | ForEach-Object { Format-PowerShellArgument $_ }) -join " ")
}

function Add-ValidationCommand {
    param(
        [System.Collections.Generic.List[object]]$Commands,
        [hashtable]$Seen,
        [string]$Name,
        [string]$Command,
        [string]$Reason,
        [string]$Scope
    )

    if ($Seen.ContainsKey($Name)) {
        return
    }

    $Seen[$Name] = $true
    $Commands.Add([PSCustomObject]@{
        name = $Name
        command = $Command
        reason = $Reason
        scope = $Scope
    })
}

function Get-ValidationPlan {
    param(
        [string[]]$DirtyPaths,
        [string[]]$StatusLines,
        [object[]]$MatchedRules,
        [string]$HighestRisk,
        [object]$ChangeSignals,
        [object]$CoverageMatrix
    )

    $commands = New-Object System.Collections.Generic.List[object]
    $manualChecks = New-Object System.Collections.Generic.List[string]
    $seenCommands = @{}
    $ruleNames = @($MatchedRules | ForEach-Object { [string]$_.name })
    $trackedDirtyPaths = @($StatusLines |
        Where-Object { $_ -notmatch "^\?\?" } |
        ForEach-Object { Convert-StatusLineToPath $_ } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique)
    $untrackedPaths = @($StatusLines |
        Where-Object { $_ -match "^\?\?" } |
        ForEach-Object { Convert-StatusLineToPath $_ } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique)
    $dirtyPathArguments = Format-DirtyPathArguments -Paths $trackedDirtyPaths

    if ($trackedDirtyPaths.Count -gt 0) {
        if ([string]::IsNullOrWhiteSpace($dirtyPathArguments)) {
            Add-ValidationCommand -Commands $commands -Seen $seenCommands -Name "diff-whitespace-check" -Command "git diff --check" -Reason "Check tracked dirty files for whitespace and patch-format issues." -Scope "workspace"
        }
        else {
            Add-ValidationCommand -Commands $commands -Seen $seenCommands -Name "diff-whitespace-check" -Command "git diff --check -- $dirtyPathArguments" -Reason "Check tracked dirty files for whitespace and patch-format issues." -Scope "workspace"
        }
    }

    if ($untrackedPaths.Count -gt 0) {
        $manualChecks.Add("Review untracked text files directly; git diff --check only covers tracked diffs.")
    }

    if (($ruleNames -contains "build-scripts") -or @($DirtyPaths | Where-Object { $_ -match "^(scripts/AIDevDoctor\.ps1|scripts/TestAIDevDoctor\.ps1|tools/ai-dev/)" }).Count -gt 0) {
        Add-ValidationCommand -Commands $commands -Seen $seenCommands -Name "aid-dev-doctor-self-test" -Command "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\TestAIDevDoctor.ps1" -Reason "Validate the AIDevDoctor report, prompt, context schema, diagnostics, and validation-plan output." -Scope "ai-dev"
    }

    if (@($DirtyPaths | Where-Object { $_ -eq "scripts/RunPerfGate.ps1" -or $_ -eq "scripts/TestRunPerfGate.ps1" }).Count -gt 0) {
        Add-ValidationCommand -Commands $commands -Seen $seenCommands -Name "perf-gate-self-test" -Command "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\TestRunPerfGate.ps1" -Reason "Validate PerfGate tooling behavior after changes to the perf gate scripts." -Scope "perf-gate"
    }

    if ($HighestRisk -eq "critical" -or $HighestRisk -eq "high") {
        Add-ValidationCommand -Commands $commands -Seen $seenCommands -Name "standard-perf-gate" -Command "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RunPerfGate.ps1 -Profile Standard" -Reason "Exercise the standard Sandbox and Editor validation matrix across configured backends and collect structured evidence." -Scope "runtime"
    }

    if ($ruleNames -contains "function-render" -or $ruleNames -contains "graphics-rhi" -or $ruleNames -contains "shader-source") {
        $manualChecks.Add("Inspect generated logs for shader compile, descriptor, barrier, validation, assertion, and resource leak signals.")
        $manualChecks.Add("Confirm render-path changes include meaningful Tracy instrumentation through Base/hprofiler.h where the changed path is a pass, dispatch, or hotspot.")
    }

    if ($ruleNames -contains "engine-config") {
        $manualChecks.Add("Confirm product/config/Engine.ini contains the intended backend, validation, VSync, and RenderDebugView settings before comparing runs.")
    }

    if ($ruleNames -contains "sandbox-scene") {
        $manualChecks.Add("Separate Sandbox.scene.json content changes from renderer or config changes when interpreting visual or PerfGate differences.")
    }

    if ($ruleNames -contains "docs-only" -or @($DirtyPaths | Where-Object { $_ -match "^(README\.md|docs/)" }).Count -gt 0) {
        $manualChecks.Add("Review the README.md and docs diff for stale paths, command names, generated artifact locations, and Engine/Editor boundary language.")
    }

    if (@($ChangeSignals.items).Count -gt 0) {
        $manualChecks.Add("Use the Change Signals section to decide whether the generated command list needs broader backend, log, or documentation coverage.")
    }

    if ($null -ne $CoverageMatrix -and [int]$CoverageMatrix.uncovered_required_count -gt 0) {
        $missingCells = @($CoverageMatrix.missing_required | ForEach-Object { "$($_.target)/$($_.backend)=$($_.coverage_status)" } | Select-Object -First 6)
        $manualChecks.Add("Review the Validation Coverage Matrix; required cells needing attention: $($missingCells -join ', ').")
    }

    $uniqueManualChecks = @($manualChecks | Sort-Object -Unique)
    $summary = "No validation commands were inferred from the current workspace context."
    if ($commands.Count -gt 0 -or $uniqueManualChecks.Count -gt 0) {
        $summary = "Generated $($commands.Count) command(s) and $($uniqueManualChecks.Count) manual check(s) from the current workspace context."
    }

    return [PSCustomObject]@{
        summary = $summary
        commands = @($commands.ToArray())
        manual_checks = @($uniqueManualChecks)
    }
}

function Add-ValidationGap {
    param(
        [System.Collections.Generic.List[object]]$Gaps,
        [hashtable]$Seen,
        [string]$Id,
        [string]$Severity,
        [string]$Reason,
        [string]$NextStep,
        [string[]]$RelatedRules
    )

    if ($Seen.ContainsKey($Id)) {
        return
    }

    $Seen[$Id] = $true
    $Gaps.Add([PSCustomObject]@{
        id = $Id
        severity = $Severity
        reason = $Reason
        next_step = $NextStep
        related_rules = @($RelatedRules)
    })
}

function Get-ValidationGaps {
    param(
        [object[]]$MatchedRules,
        [string]$HighestRisk,
        [object]$EvidenceFreshness,
        [object]$CoverageMatrix
    )

    $gaps = New-Object System.Collections.Generic.List[object]
    $seen = @{}
    $ruleNames = @($MatchedRules | ForEach-Object { [string]$_.name })
    $runtimeRisk = ($HighestRisk -eq "critical" -or $HighestRisk -eq "high")

    if ($runtimeRisk -and $EvidenceFreshness.perf_gate.status -ne "fresh") {
        Add-ValidationGap -Gaps $gaps -Seen $seen -Id "fresh-runtime-validation-missing" -Severity "high" -Reason "High-risk runtime paths are dirty, but no fresh PerfGate evidence is available for the current workspace." -NextStep "Run the standard PerfGate matrix after current changes, then regenerate AIDevDoctor with -IncludePerfGate." -RelatedRules @($ruleNames)
    }

    if ($runtimeRisk -and $null -ne $CoverageMatrix -and [int]$CoverageMatrix.uncovered_required_count -gt 0) {
        $missingCells = @($CoverageMatrix.missing_required | ForEach-Object { "$($_.target)/$($_.backend)=$($_.coverage_status)" } | Select-Object -First 6)
        Add-ValidationGap -Gaps $gaps -Seen $seen -Id "runtime-validation-coverage-incomplete" -Severity "high" -Reason "Required runtime validation coverage is incomplete: $($missingCells -join ', ')." -NextStep "Run the missing Sandbox/Editor backend coverage, then regenerate AIDevDoctor so the matrix can link fresh PASS evidence." -RelatedRules @($ruleNames)
    }

    if (($ruleNames -contains "function-render" -or $ruleNames -contains "graphics-rhi" -or $ruleNames -contains "shader-source") -and $EvidenceFreshness.logs.status -ne "fresh") {
        Add-ValidationGap -Gaps $gaps -Seen $seen -Id "fresh-render-log-evidence-missing" -Severity "medium" -Reason "Render, RHI, or shader paths are dirty, but latest log evidence is not fresh for those changes." -NextStep "Run the affected runtime target(s), collect fresh logs, and include them with -IncludeLogs." -RelatedRules @($ruleNames | Where-Object { $_ -in @("function-render", "graphics-rhi", "shader-source") })
    }

    if (($ruleNames -contains "engine-config") -and $EvidenceFreshness.logs.status -ne "fresh") {
        Add-ValidationGap -Gaps $gaps -Seen $seen -Id "backend-confirmation-log-missing" -Severity "medium" -Reason "Engine.ini is dirty, but no fresh log evidence confirms the runtime backend/config state after the change." -NextStep "Run the intended backend and regenerate AIDevDoctor with -IncludeLogs so the report can compare fresh logs." -RelatedRules @("engine-config")
    }

    if (($ruleNames -contains "build-scripts") -and $EvidenceFreshness.perf_gate.status -eq "stale") {
        Add-ValidationGap -Gaps $gaps -Seen $seen -Id "tooling-changed-after-validation" -Severity "medium" -Reason "Tooling paths changed after the latest PerfGate summary, so existing automation evidence may not cover the current scripts." -NextStep "Run the touched tool self-test and regenerate the report before relying on automation outputs." -RelatedRules @("build-scripts")
    }

    $summary = "No validation evidence gaps inferred from the current workspace context."
    if ($gaps.Count -gt 0) {
        $summary = "Detected $($gaps.Count) validation evidence gap(s)."
    }

    return [PSCustomObject]@{
        summary = $summary
        items = @($gaps.ToArray())
    }
}

function Add-ValidationEvidenceItem {
    param(
        [System.Collections.Generic.List[object]]$Items,
        [string]$Kind,
        [string]$Name,
        [string]$Status,
        [string]$Path,
        [string]$ModifiedUtc,
        [string]$Summary,
        [string]$Target = "",
        [string]$Backend = "",
        [string]$Profile = "",
        [string]$Configuration = ""
    )

    $Items.Add([PSCustomObject]@{
        kind = $Kind
        name = $Name
        status = $Status
        path = $Path
        modified_utc = $ModifiedUtc
        summary = $Summary
        target = $Target
        backend = $Backend
        profile = $Profile
        configuration = $Configuration
    })
}

function Get-ValidationEvidenceIndex {
    param(
        [string]$RepoRoot,
        [string]$CurrentReportRoot
    )

    $items = New-Object System.Collections.Generic.List[object]

    $perfGateRoot = Join-Path $RepoRoot "Intermediate/test-reports/perf-gate"
    if (Test-Path -LiteralPath $perfGateRoot) {
        $summaries = @(Get-ChildItem -LiteralPath $perfGateRoot -Recurse -File -Filter "summary.json" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 5)
        foreach ($summaryFile in $summaries) {
            try {
                $json = Get-Content -LiteralPath $summaryFile.FullName -Raw | ConvertFrom-Json
                $status = if ($json.PSObject.Properties.Name -contains "status") { [string]$json.status } else { "unknown" }
                $profile = if ($json.PSObject.Properties.Name -contains "profile") { [string]$json.profile } else { "" }
                $configuration = if ($json.PSObject.Properties.Name -contains "configuration") { [string]$json.configuration } else { "" }
                Add-ValidationEvidenceItem -Items $items -Kind "perf-gate-summary" -Name $summaryFile.Directory.Name -Status $status -Path $summaryFile.FullName -ModifiedUtc $summaryFile.LastWriteTimeUtc.ToString("o") -Summary "PerfGate summary profile=$profile configuration=$configuration." -Profile $profile -Configuration $configuration

                foreach ($run in @($json.runs)) {
                    $runStatus = if ($run.PSObject.Properties.Name -contains "status") { [string]$run.status } else { "unknown" }
                    $target = if ($run.PSObject.Properties.Name -contains "target") { [string]$run.target } else { "" }
                    $backend = if ($run.PSObject.Properties.Name -contains "backend") { [string]$run.backend } else { "" }
                    $telemetryPath = if ($run.PSObject.Properties.Name -contains "telemetry") { [string]$run.telemetry } else { "" }
                    $frames = if ($run.PSObject.Properties.Name -contains "frames_sampled") { [string]$run.frames_sampled } else { "n/a" }
                    Add-ValidationEvidenceItem -Items $items -Kind "perf-gate-run" -Name "$($summaryFile.Directory.Name)/$target-$backend" -Status $runStatus -Path $telemetryPath -ModifiedUtc $summaryFile.LastWriteTimeUtc.ToString("o") -Summary "PerfGate run frames_sampled=$frames." -Target $target -Backend $backend -Profile $profile -Configuration $configuration
                }
            }
            catch {
                Add-ValidationEvidenceItem -Items $items -Kind "perf-gate-summary" -Name $summaryFile.Directory.Name -Status "parse_error" -Path $summaryFile.FullName -ModifiedUtc $summaryFile.LastWriteTimeUtc.ToString("o") -Summary $_.Exception.Message
            }
        }
    }

    $logRoot = Join-Path $RepoRoot "product/logs"
    if (Test-Path -LiteralPath $logRoot) {
        $logFiles = @(Get-ChildItem -LiteralPath $logRoot -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 6)
        foreach ($logFile in $logFiles) {
            Add-ValidationEvidenceItem -Items $items -Kind "runtime-log" -Name $logFile.Name -Status "available" -Path $logFile.FullName -ModifiedUtc $logFile.LastWriteTimeUtc.ToString("o") -Summary "Runtime log file size=$($logFile.Length) bytes."
        }
    }

    $buildLogRoot = Join-Path $RepoRoot "Intermediate/logs"
    if (Test-Path -LiteralPath $buildLogRoot) {
        $buildLogs = @(Get-ChildItem -LiteralPath $buildLogRoot -Recurse -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 6)
        foreach ($buildLog in $buildLogs) {
            $status = "available"
            if ($buildLog.Name -match "fail|error") {
                $status = "review"
            }
            Add-ValidationEvidenceItem -Items $items -Kind "developer-log" -Name $buildLog.Name -Status $status -Path $buildLog.FullName -ModifiedUtc $buildLog.LastWriteTimeUtc.ToString("o") -Summary "Intermediate log file size=$($buildLog.Length) bytes."
        }
    }

    $aiDevRoot = Join-Path $RepoRoot "Intermediate/test-reports/ai-dev"
    if (Test-Path -LiteralPath $aiDevRoot) {
        $currentRootFull = (Resolve-Path -LiteralPath $CurrentReportRoot -ErrorAction SilentlyContinue)
        $currentRootText = ""
        if ($null -ne $currentRootFull) {
            $currentRootText = $currentRootFull.Path.TrimEnd('\', '/')
        }

        $contexts = @(Get-ChildItem -LiteralPath $aiDevRoot -Recurse -File -Filter "context.json" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 8)
        foreach ($contextFile in $contexts) {
            $contextDirectory = $contextFile.Directory.FullName.TrimEnd('\', '/')
            if (-not [string]::IsNullOrWhiteSpace($currentRootText) -and [string]::Equals($contextDirectory, $currentRootText, [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $mode = ""
            try {
                $contextJson = Get-Content -LiteralPath $contextFile.FullName -Raw | ConvertFrom-Json
                if ($contextJson.PSObject.Properties.Name -contains "mode") {
                    $mode = [string]$contextJson.mode
                }
            }
            catch {
                $mode = "parse_error"
            }
            Add-ValidationEvidenceItem -Items $items -Kind "aid-dev-context" -Name $contextFile.Directory.Name -Status "available" -Path $contextFile.FullName -ModifiedUtc $contextFile.LastWriteTimeUtc.ToString("o") -Summary "AIDevDoctor context mode=$mode."
        }
    }

    $sortedItems = @($items.ToArray() | Sort-Object modified_utc -Descending | Select-Object -First 32)
    $summary = "Indexed $($sortedItems.Count) validation evidence artifact(s)."
    if ($sortedItems.Count -eq 0) {
        $summary = "No validation evidence artifacts found under Intermediate or product/logs."
    }

    return [PSCustomObject]@{
        summary = $summary
        items = @($sortedItems)
    }
}

function Get-CanonicalValidationTarget {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }

    if ($Value -match "^(?i)sandbox$") {
        return "Sandbox"
    }
    if ($Value -match "^(?i)editor$") {
        return "Editor"
    }
    return $Value
}

function Get-CanonicalValidationBackend {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }

    if ($Value -match "^(?i)vulkan$") {
        return "Vulkan"
    }
    if ($Value -match "^(?i)(dx12|d3d12|directx12)$") {
        return "DX12"
    }
    return $Value
}

function Get-ValidationCoverageMatrix {
    param(
        [object]$EvidenceIndex,
        [object[]]$MatchedRules,
        [string]$HighestRisk,
        [object]$EvidenceFreshness
    )

    $expectedCells = @(
        [PSCustomObject]@{ target = "Sandbox"; backend = "Vulkan" },
        [PSCustomObject]@{ target = "Sandbox"; backend = "DX12" },
        [PSCustomObject]@{ target = "Editor"; backend = "Vulkan" },
        [PSCustomObject]@{ target = "Editor"; backend = "DX12" }
    )

    $requiredKeys = @{}
    $runtimeRisk = ($HighestRisk -eq "critical" -or $HighestRisk -eq "high")
    if ($runtimeRisk) {
        foreach ($cell in $expectedCells) {
            $requiredKeys["$($cell.target)/$($cell.backend)"] = $true
        }
    }
    else {
        foreach ($rule in @($MatchedRules)) {
            foreach ($validation in @($rule.validation)) {
                $validationText = [string]$validation
                if ($validationText -match "^(Sandbox|Editor)\s*\+\s*(Vulkan|DX12)$") {
                    $target = Get-CanonicalValidationTarget $matches[1]
                    $backend = Get-CanonicalValidationBackend $matches[2]
                    $requiredKeys["$target/$backend"] = $true
                }
            }
        }
    }

    $dirtyModifiedTime = $null
    $dirtyModifiedUtc = ""
    if (Test-ObjectProperty -Object $EvidenceFreshness -Name "dirty") {
        $dirtyModifiedUtc = Get-ObjectStringProperty -Object $EvidenceFreshness.dirty -Name "latest_modified_utc"
        $dirtyModifiedTime = ConvertTo-DateTimeOffsetOrNull $dirtyModifiedUtc
    }

    $latestRuns = @{}
    foreach ($item in @($EvidenceIndex.items)) {
        if ([string]$item.kind -ne "perf-gate-run") {
            continue
        }

        $target = Get-CanonicalValidationTarget (Get-ObjectStringProperty -Object $item -Name "target")
        $backend = Get-CanonicalValidationBackend (Get-ObjectStringProperty -Object $item -Name "backend")
        if ([string]::IsNullOrWhiteSpace($target) -or [string]::IsNullOrWhiteSpace($backend)) {
            continue
        }

        $key = "$target/$backend"
        $modifiedTime = ConvertTo-DateTimeOffsetOrNull (Get-ObjectStringProperty -Object $item -Name "modified_utc")
        if ($null -eq $modifiedTime) {
            $modifiedTime = [DateTimeOffset]::MinValue
        }

        $record = [PSCustomObject]@{
            item = $item
            target = $target
            backend = $backend
            modified_time = $modifiedTime
        }
        if (-not $latestRuns.ContainsKey($key) -or $modifiedTime -gt $latestRuns[$key].modified_time) {
            $latestRuns[$key] = $record
        }
    }

    $cells = New-Object System.Collections.Generic.List[object]
    $missingRequired = New-Object System.Collections.Generic.List[object]
    $coveredRequiredCount = 0
    foreach ($expected in $expectedCells) {
        $key = "$($expected.target)/$($expected.backend)"
        $required = $requiredKeys.ContainsKey($key)
        $evidenceStatus = "missing"
        $coverageStatus = if ($required) { "missing" } else { "not_required" }
        $fresh = $false
        $evidenceName = ""
        $path = ""
        $modifiedUtc = ""
        $summary = ""
        $profile = ""
        $configuration = ""

        if ($latestRuns.ContainsKey($key)) {
            $run = $latestRuns[$key].item
            $evidenceStatus = Get-ObjectStringProperty -Object $run -Name "status" -Default "unknown"
            $evidenceName = Get-ObjectStringProperty -Object $run -Name "name"
            $path = Get-ObjectStringProperty -Object $run -Name "path"
            $modifiedUtc = Get-ObjectStringProperty -Object $run -Name "modified_utc"
            $summary = Get-ObjectStringProperty -Object $run -Name "summary"
            $profile = Get-ObjectStringProperty -Object $run -Name "profile"
            $configuration = Get-ObjectStringProperty -Object $run -Name "configuration"

            $modifiedTime = $latestRuns[$key].modified_time
            $fresh = ($null -eq $dirtyModifiedTime -or $modifiedTime -ge $dirtyModifiedTime)
            if (-not $fresh) {
                $coverageStatus = "stale"
            }
            elseif ($evidenceStatus -eq "PASS") {
                $coverageStatus = if ($required) { "covered" } else { "available" }
            }
            else {
                $coverageStatus = "review"
            }
        }

        $cell = [PSCustomObject]@{
            target = [string]$expected.target
            backend = [string]$expected.backend
            required = $required
            coverage_status = $coverageStatus
            evidence_status = $evidenceStatus
            fresh = $fresh
            evidence_name = $evidenceName
            path = $path
            modified_utc = $modifiedUtc
            summary = $summary
            profile = $profile
            configuration = $configuration
            compared_to_dirty_modified_utc = $dirtyModifiedUtc
        }
        $cells.Add($cell)

        if ($required -and $coverageStatus -eq "covered") {
            ++$coveredRequiredCount
        }
        elseif ($required) {
            $missingRequired.Add([PSCustomObject]@{
                target = [string]$expected.target
                backend = [string]$expected.backend
                coverage_status = $coverageStatus
                evidence_status = $evidenceStatus
            })
        }
    }

    $requiredCount = $requiredKeys.Count
    $summary = "Validation coverage: no required runtime target/backend cells inferred; indexed latest available evidence for 4 cells."
    if ($requiredCount -gt 0) {
        $summary = "Validation coverage: $coveredRequiredCount/$requiredCount required target/backend cell(s) have fresh PASS evidence."
        if ($missingRequired.Count -gt 0) {
            $summary += " $($missingRequired.Count) required cell(s) need attention."
        }
    }

    return [PSCustomObject]@{
        summary = $summary
        required = ($requiredCount -gt 0)
        required_count = $requiredCount
        covered_required_count = $coveredRequiredCount
        uncovered_required_count = $missingRequired.Count
        missing_required = @($missingRequired.ToArray())
        cells = @($cells.ToArray())
    }
}

function Get-UntrackedTextPaths {
    param(
        [string]$RepoRoot,
        [string[]]$StatusLines
    )

    $paths = New-Object System.Collections.Generic.List[string]
    $allowedExtensions = @(".ps1", ".json", ".md", ".txt", ".ini", ".lua", ".h", ".hpp", ".cpp", ".c", ".hlsl", ".hlsli")

    foreach ($line in $StatusLines) {
        if ($line -notmatch "^\?\?\s+(.+)$") {
            continue
        }

        $relative = (($matches[1] -replace "\\", "/").Trim('"'))
        $fullPath = Join-Path $RepoRoot ($relative -replace "/", "\")
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $extension = [System.IO.Path]::GetExtension($fullPath)
            if ($allowedExtensions -contains $extension) {
                $paths.Add($relative)
            }
            continue
        }

        if (Test-Path -LiteralPath $fullPath -PathType Container) {
            $files = @(Get-ChildItem -LiteralPath $fullPath -Recurse -File -ErrorAction SilentlyContinue)
            foreach ($file in $files) {
                $extension = [System.IO.Path]::GetExtension($file.FullName)
                if ($allowedExtensions -notcontains $extension) {
                    continue
                }
                $rootPath = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd('\', '/')
                $fullName = $file.FullName
                if ($fullName.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $relativeFile = $fullName.Substring($rootPath.Length).TrimStart('\', '/') -replace "\\", "/"
                }
                else {
                    $relativeFile = $file.Name
                }
                $paths.Add($relativeFile)
            }
        }
    }

    return @($paths | Sort-Object -Unique)
}

function Get-ChangeSignalSources {
    param(
        [string]$RepoRoot,
        [string[]]$StatusLines
    )

    $sources = New-Object System.Collections.Generic.List[object]
    $diffLines = @(Invoke-Git -RepoRoot $RepoRoot -Arguments @("diff", "--unified=0", "--no-ext-diff"))
    $cachedDiffLines = @(Invoke-Git -RepoRoot $RepoRoot -Arguments @("diff", "--cached", "--unified=0", "--no-ext-diff"))
    $allDiffLines = @($diffLines + $cachedDiffLines)
    $currentPath = ""

    foreach ($line in $allDiffLines) {
        if ($line -match "^\+\+\+\s+b/(.+)$") {
            $currentPath = $matches[1] -replace "\\", "/"
            continue
        }
        if ($line -match "^---\s+a/(.+)$" -and [string]::IsNullOrWhiteSpace($currentPath)) {
            $currentPath = $matches[1] -replace "\\", "/"
            continue
        }
        if ($line.StartsWith("+++") -or $line.StartsWith("---") -or $line.StartsWith("@@")) {
            continue
        }
        if (($line.StartsWith("+") -or $line.StartsWith("-")) -and -not [string]::IsNullOrWhiteSpace($currentPath)) {
            $sources.Add([PSCustomObject]@{
                path = $currentPath
                text = $line.Substring(1)
            })
        }
    }

    $untracked = @(Get-UntrackedTextPaths -RepoRoot $RepoRoot -StatusLines $StatusLines)
    foreach ($relative in $untracked) {
        $fullPath = Join-Path $RepoRoot ($relative -replace "/", "\")
        try {
            $fileInfo = Get-Item -LiteralPath $fullPath -ErrorAction Stop
            if ($fileInfo.Length -gt 262144) {
                continue
            }
            $lines = @(Get-Content -LiteralPath $fullPath -TotalCount 400 -ErrorAction Stop)
            foreach ($line in $lines) {
                $sources.Add([PSCustomObject]@{
                    path = $relative
                    text = [string]$line
                })
            }
        }
        catch {
            continue
        }
    }

    return @($sources.ToArray())
}

function Get-ChangeSignals {
    param(
        [string]$RepoRoot,
        [string[]]$StatusLines,
        [object]$SignalRules
    )

    $sources = @(Get-ChangeSignalSources -RepoRoot $RepoRoot -StatusLines $StatusLines)
    $items = New-Object System.Collections.Generic.List[object]

    foreach ($rule in $SignalRules.rules) {
        $rulePathRegex = ""
        if ($rule.PSObject.Properties.Name -contains "path_regex") {
            $rulePathRegex = [string]$rule.path_regex
        }
        $matches = @($sources | Where-Object {
            ([string]::IsNullOrWhiteSpace($rulePathRegex) -or $_.path -match $rulePathRegex) -and
            ($_.text -match [string]$rule.regex)
        })
        if ($matches.Count -eq 0) {
            continue
        }

        $examples = @($matches | ForEach-Object {
            $text = [string]$_.text
            if ($text.Length -gt 96) {
                $text = $text.Substring(0, 96) + "..."
            }
            "$($_.path): $text"
        } | Select-Object -First 5)

        $items.Add([PSCustomObject]@{
            name = [string]$rule.name
            category = [string]$rule.category
            severity = [string]$rule.severity
            finding = [string]$rule.finding
            next_step = [string]$rule.next_step
            match_count = $matches.Count
            examples = @($examples)
        })
    }

    $summary = "No change-signal rules matched the current diff or untracked text files."
    if ($items.Count -gt 0) {
        $summary = "Matched $($items.Count) change-signal rule(s) across $($sources.Count) scanned changed text line(s)."
    }

    return [PSCustomObject]@{
        summary = $summary
        items = @($items.ToArray())
    }
}

function Get-EngineConfigSnapshot {
    param([string]$RepoRoot)

    $path = Join-Path $RepoRoot "product/config/Engine.ini"
    if (-not (Test-Path -LiteralPath $path)) {
        return "Engine.ini not found."
    }

    $lines = Get-Content -LiteralPath $path | Select-Object -First 120
    return ($lines -join [Environment]::NewLine)
}

function Get-LatestPerfGateSummary {
    param(
        [string]$RepoRoot,
        [bool]$Enabled
    )

    if (-not $Enabled) {
        return [PSCustomObject]@{
            enabled = $false
            found = $false
            summary = "Not requested. Pass -IncludePerfGate to include latest PerfGate summary."
            path = ""
            modified_utc = ""
        }
    }

    $root = Join-Path $RepoRoot "Intermediate/test-reports/perf-gate"
    if (-not (Test-Path -LiteralPath $root)) {
        return [PSCustomObject]@{
            enabled = $true
            found = $false
            summary = "PerfGate report directory not found."
            path = ""
            modified_utc = ""
        }
    }

    $summary = Get-ChildItem -LiteralPath $root -Recurse -File -Filter "summary.json" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -eq $summary) {
        return [PSCustomObject]@{
            enabled = $true
            found = $false
            summary = "No summary.json found under Intermediate/test-reports/perf-gate."
            path = ""
            modified_utc = ""
        }
    }

    try {
        $json = Get-Content -LiteralPath $summary.FullName -Raw | ConvertFrom-Json
        $status = ""
        if ($json.PSObject.Properties.Name -contains "status") {
            $status = [string]$json.status
        }
        elseif ($json.PSObject.Properties.Name -contains "overall_status") {
            $status = [string]$json.overall_status
        }
        else {
            $status = "status field not found"
        }
        return [PSCustomObject]@{
            enabled = $true
            found = $true
            summary = "Latest PerfGate summary: $status"
            path = $summary.FullName
            modified_utc = $summary.LastWriteTimeUtc.ToString("o")
        }
    }
    catch {
        return [PSCustomObject]@{
            enabled = $true
            found = $true
            summary = "Failed to parse latest PerfGate summary: $($_.Exception.Message)"
            path = $summary.FullName
            modified_utc = $summary.LastWriteTimeUtc.ToString("o")
        }
    }
}

function Get-LatestLogSignals {
    param(
        [string]$RepoRoot,
        [string]$ReportRoot,
        [bool]$Enabled,
        [int]$MaxMatches
    )

    if (-not $Enabled) {
        return [PSCustomObject]@{
            enabled = $false
            found = $false
            summary = "Not requested. Pass -IncludeLogs to include latest log signals."
            snippets = @()
            latest_log_path = ""
            latest_log_modified_utc = ""
            has_logs = $false
        }
    }

    $logRoot = Join-Path $RepoRoot "product/logs"
    if (-not (Test-Path -LiteralPath $logRoot)) {
        return [PSCustomObject]@{
            enabled = $true
            found = $false
            summary = "product/logs not found."
            snippets = @()
            latest_log_path = ""
            latest_log_modified_utc = ""
            has_logs = $false
        }
    }

    $snippetRoot = Join-Path $ReportRoot "snippets"
    New-Item -ItemType Directory -Force -Path $snippetRoot | Out-Null

    $logs = @(Get-ChildItem -LiteralPath $logRoot -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 3)

    $snippetPaths = New-Object System.Collections.Generic.List[string]
    $totalMatches = 0
    $latestLogPath = ""
    $latestLogModifiedUtc = ""
    if ($logs.Count -gt 0) {
        $latestLogPath = [string]$logs[0].FullName
        $latestLogModifiedUtc = $logs[0].LastWriteTimeUtc.ToString("o")
    }

    foreach ($log in $logs) {
        $matches = @(Select-String -LiteralPath $log.FullName -Pattern "error|warning|validation|fail|crash|leak|assert" -CaseSensitive:$false -ErrorAction SilentlyContinue |
            Select-Object -First $MaxMatches)
        if ($matches.Count -eq 0) {
            continue
        }

        $safeName = ($log.Name -replace '[^A-Za-z0-9_.-]', '_') + ".signals.txt"
        $snippetPath = Join-Path $snippetRoot $safeName
        $matches | ForEach-Object { "{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim() } |
            Set-Content -LiteralPath $snippetPath -Encoding UTF8
        $snippetPaths.Add($snippetPath)
        $totalMatches += $matches.Count
    }

    $summary = "Scanned latest $($logs.Count) log file(s); found $totalMatches signal line(s)."
    if ($snippetPaths.Count -eq 0) {
        $summary = "Scanned latest $($logs.Count) log file(s); no error/warning/validation signals found."
    }

    return [PSCustomObject]@{
        enabled = $true
        found = ($snippetPaths.Count -gt 0)
        summary = $summary
        snippets = @($snippetPaths)
        latest_log_path = $latestLogPath
        latest_log_modified_utc = $latestLogModifiedUtc
        has_logs = ($logs.Count -gt 0)
    }
}

function Get-DirtyPathFreshness {
    param(
        [string]$RepoRoot,
        [string[]]$DirtyPaths
    )

    $latestPath = ""
    $latestModifiedUtc = $null
    $scanned = New-Object System.Collections.Generic.List[object]

    foreach ($relativePath in $DirtyPaths) {
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $fullPath = Join-Path $RepoRoot ($relativePath -replace "/", "\")
        $candidate = $null
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $candidate = Get-Item -LiteralPath $fullPath -ErrorAction SilentlyContinue
        }
        elseif (Test-Path -LiteralPath $fullPath -PathType Container) {
            $candidate = Get-ChildItem -LiteralPath $fullPath -Recurse -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
        }

        if ($null -eq $candidate) {
            continue
        }

        $modifiedUtc = $candidate.LastWriteTimeUtc
        $scanned.Add([PSCustomObject]@{
            path = $relativePath
            modified_utc = $modifiedUtc.ToString("o")
        })

        if ($null -eq $latestModifiedUtc -or $modifiedUtc -gt $latestModifiedUtc) {
            $latestModifiedUtc = $modifiedUtc
            $latestPath = $relativePath
        }
    }

    $summary = "No dirty file timestamps were available."
    $latestModifiedText = ""
    if ($null -ne $latestModifiedUtc) {
        $latestModifiedText = $latestModifiedUtc.ToString("o")
        $summary = "Latest dirty path timestamp is $latestModifiedText at $latestPath."
    }
    elseif ($DirtyPaths.Count -eq 0) {
        $summary = "Workspace has no dirty paths."
    }

    return [PSCustomObject]@{
        count = $DirtyPaths.Count
        latest_path = $latestPath
        latest_modified_utc = $latestModifiedText
        summary = $summary
        scanned_paths = @($scanned.ToArray())
    }
}

function Get-EvidenceFreshnessRecord {
    param(
        [string]$Name,
        [bool]$Enabled,
        [bool]$Found,
        [string]$EvidencePath,
        [string]$EvidenceModifiedUtc,
        [object]$DirtyFreshness
    )

    $status = "unknown"
    $reason = "$Name evidence timestamp could not be classified."

    if (-not $Enabled) {
        $status = "not_requested"
        $reason = "$Name evidence was not requested."
    }
    elseif (-not $Found) {
        $status = "unavailable"
        $reason = "$Name evidence was requested but no comparable artifact was found."
    }
    elseif ([string]::IsNullOrWhiteSpace($DirtyFreshness.latest_modified_utc)) {
        $status = "no_dirty_baseline"
        $reason = "$Name evidence exists, but no dirty file timestamp is available for comparison."
    }
    elseif ([string]::IsNullOrWhiteSpace($EvidenceModifiedUtc)) {
        $status = "unknown"
        $reason = "$Name evidence exists, but its modification timestamp is unavailable."
    }
    else {
        $dirtyTime = [DateTimeOffset]::Parse([string]$DirtyFreshness.latest_modified_utc)
        $evidenceTime = [DateTimeOffset]::Parse($EvidenceModifiedUtc)
        if ($evidenceTime -ge $dirtyTime) {
            $status = "fresh"
            $reason = "$Name evidence is newer than or equal to the latest dirty file timestamp."
        }
        else {
            $status = "stale"
            $reason = "$Name evidence is older than the latest dirty file timestamp."
        }
    }

    return [PSCustomObject]@{
        name = $Name
        status = $status
        reason = $reason
        evidence_path = $EvidencePath
        evidence_modified_utc = $EvidenceModifiedUtc
        compared_to_dirty_path = [string]$DirtyFreshness.latest_path
        compared_to_dirty_modified_utc = [string]$DirtyFreshness.latest_modified_utc
    }
}

function Get-EvidenceFreshness {
    param(
        [string]$RepoRoot,
        [string[]]$DirtyPaths,
        [object]$PerfGate,
        [object]$LogSignals
    )

    $dirtyFreshness = Get-DirtyPathFreshness -RepoRoot $RepoRoot -DirtyPaths $DirtyPaths
    $perfGateFreshness = Get-EvidenceFreshnessRecord -Name "PerfGate" -Enabled ([bool]$PerfGate.enabled) -Found ([bool]$PerfGate.found) -EvidencePath ([string]$PerfGate.path) -EvidenceModifiedUtc ([string]$PerfGate.modified_utc) -DirtyFreshness $dirtyFreshness
    $logEvidenceFound = $false
    if ($LogSignals.PSObject.Properties.Name -contains "has_logs") {
        $logEvidenceFound = [bool]$LogSignals.has_logs
    }
    else {
        $logEvidenceFound = [bool]$LogSignals.found
    }
    $logFreshness = Get-EvidenceFreshnessRecord -Name "Logs" -Enabled ([bool]$LogSignals.enabled) -Found $logEvidenceFound -EvidencePath ([string]$LogSignals.latest_log_path) -EvidenceModifiedUtc ([string]$LogSignals.latest_log_modified_utc) -DirtyFreshness $dirtyFreshness

    $staleEvidence = @(@($perfGateFreshness, $logFreshness) | Where-Object { $_.status -eq "stale" })
    $summary = "Evidence freshness: PerfGate=$($perfGateFreshness.status), Logs=$($logFreshness.status)."
    if ($staleEvidence.Count -gt 0) {
        $summary = "Evidence freshness warning: $($staleEvidence.Count) artifact source(s) are older than current dirty files."
    }

    return [PSCustomObject]@{
        summary = $summary
        dirty = $dirtyFreshness
        perf_gate = $perfGateFreshness
        logs = $logFreshness
    }
}

function Get-DiagnosticFindings {
    param(
        [string[]]$DirtyPaths,
        [object[]]$MatchedRules,
        [string]$HighestRisk,
        [object]$ChangeSignals,
        [object]$ChangeGroups,
        [object]$PerfGate,
        [object]$LogSignals,
        [object]$EvidenceFreshness,
        [object]$CoverageMatrix
    )

    $findings = New-Object System.Collections.Generic.List[string]
    $nextSteps = New-Object System.Collections.Generic.List[string]
    $ruleNames = @($MatchedRules | ForEach-Object { [string]$_.name })

    if ($DirtyPaths.Count -eq 0) {
        $findings.Add("Workspace has no dirty paths; AIDevDoctor can still package recent logs and validation reports.")
        $nextSteps.Add("Use -IncludeLogs or -IncludePerfGate when asking AI to review recent runtime failures without source changes.")
    }
    else {
        $findings.Add("Workspace has $($DirtyPaths.Count) dirty path(s); treat unrelated existing changes as separate until reviewed.")
    }

    if ($HighestRisk -eq "critical") {
        $findings.Add("Critical shared engine risk inferred from touched paths.")
        $nextSteps.Add("Run the full shared-path validation matrix before claiming runtime safety.")
    }
    elseif ($HighestRisk -eq "high") {
        $findings.Add("High shared engine risk inferred from touched paths.")
        $nextSteps.Add("Plan Sandbox and Editor smoke coverage on Vulkan and DX12 unless the current change is narrowed to tooling only.")
    }

    if ($ruleNames -contains "function-render") {
        $findings.Add("Function/Render path is dirty; inspect RenderGraph resource declarations, shader bindings, frame stats, debug view behavior, and Tracy coverage.")
        $nextSteps.Add("For render-path changes, verify both backends and check logs for shader, descriptor, barrier, and validation signals.")
    }

    if ($ruleNames -contains "graphics-rhi") {
        $findings.Add("Graphics/RHI path is dirty; backend parity and resource lifetime risk are elevated.")
        $nextSteps.Add("Review Vulkan legality first, then confirm DX12 behavior through the same Function-facing path.")
    }

    if ($ruleNames -contains "shader-source") {
        $findings.Add("Shader source path is dirty; DXC compile, reflection, generated binding, SPIR-V, and DXIL behavior may diverge.")
        $nextSteps.Add("Run at least Sandbox on Vulkan and DX12, then inspect shader debug dumps if compilation or binding fails.")
    }

    if ($ruleNames -contains "engine-config") {
        $findings.Add("Engine.ini is dirty; backend, validation, VSync, or RenderDebugView settings may affect subsequent runs.")
        $nextSteps.Add("Before performance or validation comparison, confirm Engine.ini contains the intended backend and validation state.")
    }

    if ($ruleNames -contains "sandbox-scene") {
        $findings.Add("Sandbox standard scene is dirty; validation results may reflect scene content changes as well as code changes.")
        $nextSteps.Add("When debugging render differences, separate scene asset changes from renderer or config changes.")
    }

    if ($ruleNames -contains "build-scripts") {
        $findings.Add("Tooling or script paths are dirty; validate the affected tool independently from engine runtime smoke tests.")
        $nextSteps.Add("Run the touched tool self-test, such as scripts/TestAIDevDoctor.ps1 for AIDevDoctor changes.")
    }

    if (@($ChangeSignals.items).Count -gt 0) {
        $findings.Add($ChangeSignals.summary)
        $nextSteps.Add("Review the Change Signals section for keyword-based hints before choosing validation scope.")
        foreach ($signal in @($ChangeSignals.items)) {
            if ($signal.severity -eq "high") {
                $findings.Add($signal.finding)
                $nextSteps.Add($signal.next_step)
            }
        }
    }

    $changeGroupItems = @($ChangeGroups.items)
    if ($changeGroupItems.Count -gt 1) {
        $findings.Add($ChangeGroups.summary)
        $nextSteps.Add("Review Dirty Change Groups before attributing runtime risk to the current tooling or documentation work.")
    }

    $hasAiDevTooling = @($changeGroupItems | Where-Object { $_.name -eq "ai-dev-tooling" }).Count -gt 0
    $hasRuntimeGroup = @($changeGroupItems | Where-Object { $_.intent -eq "runtime-validation" }).Count -gt 0
    if ($hasAiDevTooling -and $hasRuntimeGroup) {
        $findings.Add("AIDev tooling changes coexist with runtime/config/asset dirty groups; keep their validation evidence separate.")
        $nextSteps.Add("When asking AI for review, specify whether the task is AIDev tooling, runtime rendering, config, or scene-content work.")
    }

    if ($PerfGate.enabled -and $PerfGate.found) {
        if ($PerfGate.summary -notmatch "PASS") {
            $findings.Add("Latest PerfGate summary is not clean: $($PerfGate.summary).")
            $nextSteps.Add("Open the referenced PerfGate summary and inspect failed or warned target/backend records.")
        }
        else {
            $findings.Add("Latest PerfGate summary reports PASS; confirm it is recent enough for the current dirty paths before relying on it.")
        }
    }
    elseif ($PerfGate.enabled -and -not $PerfGate.found) {
        $findings.Add("PerfGate context was requested but no summary was found.")
        $nextSteps.Add("Run RunPerfGate when runtime/performance validation evidence is required.")
    }

    if ($EvidenceFreshness.perf_gate.status -eq "stale") {
        $findings.Add("Latest PerfGate summary is older than the newest dirty file; do not treat it as validation evidence for the current workspace.")
        $nextSteps.Add("Rerun PerfGate after the current source/config/asset changes before relying on performance or runtime validation evidence.")
    }

    if ($EvidenceFreshness.logs.status -eq "stale") {
        $findings.Add("Latest logs are older than the newest dirty file; log signals may describe a previous workspace state.")
        $nextSteps.Add("Regenerate runtime logs after the current changes when asking AI to diagnose runtime behavior.")
    }

    if ($null -ne $CoverageMatrix -and [int]$CoverageMatrix.uncovered_required_count -gt 0) {
        $findings.Add($CoverageMatrix.summary)
        $nextSteps.Add("Use the Validation Coverage Matrix to target only the missing or stale Sandbox/Editor backend cells before rerunning the full report.")
    }

    if ($LogSignals.enabled -and $LogSignals.found) {
        $findings.Add("Latest logs contain signal lines; review generated snippets before deciding whether a failure is current or historical.")
        $nextSteps.Add("Attach the snippet files from the AIDevDoctor report when asking AI to diagnose runtime issues.")
    }
    elseif ($LogSignals.enabled -and -not $LogSignals.found) {
        $findings.Add("Latest logs were scanned and no error/warning/validation signal lines were found.")
    }

    if ($nextSteps.Count -eq 0) {
        $nextSteps.Add("Review report.md and prompt.md, then run the validation command appropriate to the highest inferred risk.")
    }

    return [PSCustomObject]@{
        findings = @($findings | Sort-Object -Unique)
        next_steps = @($nextSteps | Sort-Object -Unique)
    }
}

$repoRoot = Get-RepoRoot
if ([string]::IsNullOrWhiteSpace($Timestamp)) {
    $Timestamp = [DateTimeOffset]::UtcNow.ToString("yyyyMMdd-HHmmss")
}

$reportRoot = Join-Path $repoRoot "Intermediate/test-reports/ai-dev/$Timestamp"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

$rulesRoot = Join-Path $repoRoot "tools/ai-dev/rules"
$templatesRoot = Join-Path $repoRoot "tools/ai-dev/templates"
$pathRules = Read-JsonFile (Join-Path $rulesRoot "path-risk-rules.json")
$docRules = Read-JsonFile (Join-Path $rulesRoot "doc-rules.json")
$validationRules = Read-JsonFile (Join-Path $rulesRoot "validation-rules.json")
$signalRules = Read-JsonFile (Join-Path $rulesRoot "change-signal-rules.json")
$groupRules = Read-JsonFile (Join-Path $rulesRoot "change-group-rules.json")
$aiReviewContract = Get-AIReviewContract -RepoRoot $repoRoot

$statusLines = @(Invoke-Git -RepoRoot $repoRoot -Arguments @("status", "--short"))
$dirtyPaths = @($statusLines | ForEach-Object { Convert-StatusLineToPath $_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$diffStat = @(Invoke-Git -RepoRoot $repoRoot -Arguments @("diff", "--stat"))
$cachedDiffStat = @(Invoke-Git -RepoRoot $repoRoot -Arguments @("diff", "--cached", "--stat"))
$recentCommits = @(Invoke-Git -RepoRoot $repoRoot -Arguments @("log", "--oneline", "-n", "8"))

$matchedRules = @(Get-MatchedRules -Paths $dirtyPaths -PathRules $pathRules)
$highestRisk = Get-HighestRisk -MatchedRules $matchedRules -ValidationRules $validationRules
$touchedSubsystems = @($matchedRules | ForEach-Object { [string]$_.subsystem } | Sort-Object -Unique)
if ($touchedSubsystems.Count -eq 0) {
    $touchedSubsystems = @("Unclassified")
}
$validationGuidance = @(Get-ValidationGuidance -MatchedRules $matchedRules -ValidationRules $validationRules -HighestRisk $highestRisk)
$docGuidance = @(Get-DocumentationGuidance -Paths $dirtyPaths -DocRules $docRules -MatchedRules $matchedRules)
$engineConfig = Get-EngineConfigSnapshot -RepoRoot $repoRoot
$perfGate = Get-LatestPerfGateSummary -RepoRoot $repoRoot -Enabled ([bool]$IncludePerfGate)
$logSignals = Get-LatestLogSignals -RepoRoot $repoRoot -ReportRoot $reportRoot -Enabled ([bool]$IncludeLogs) -MaxMatches $MaxLogMatches
$changeSignals = Get-ChangeSignals -RepoRoot $repoRoot -StatusLines $statusLines -SignalRules $signalRules
$changeGroups = Get-ChangeGroups -Paths $dirtyPaths -GroupRules $groupRules
$validationEvidenceIndex = Get-ValidationEvidenceIndex -RepoRoot $repoRoot -CurrentReportRoot $reportRoot
$evidenceFreshness = Get-EvidenceFreshness -RepoRoot $repoRoot -DirtyPaths $dirtyPaths -PerfGate $perfGate -LogSignals $logSignals
$validationCoverageMatrix = Get-ValidationCoverageMatrix -EvidenceIndex $validationEvidenceIndex -MatchedRules $matchedRules -HighestRisk $highestRisk -EvidenceFreshness $evidenceFreshness
$validationPlan = Get-ValidationPlan -DirtyPaths $dirtyPaths -StatusLines $statusLines -MatchedRules $matchedRules -HighestRisk $highestRisk -ChangeSignals $changeSignals -CoverageMatrix $validationCoverageMatrix
$validationGaps = Get-ValidationGaps -MatchedRules $matchedRules -HighestRisk $highestRisk -EvidenceFreshness $evidenceFreshness -CoverageMatrix $validationCoverageMatrix
$diagnostics = Get-DiagnosticFindings -DirtyPaths $dirtyPaths -MatchedRules $matchedRules -HighestRisk $highestRisk -ChangeSignals $changeSignals -ChangeGroups $changeGroups -PerfGate $perfGate -LogSignals $logSignals -EvidenceFreshness $evidenceFreshness -CoverageMatrix $validationCoverageMatrix

$contextPath = Join-Path $reportRoot "context.json"
$reportPath = Join-Path $reportRoot "report.md"
$promptPath = Join-Path $reportRoot "prompt.md"
$validationPlanPath = Join-Path $reportRoot "validation-plan.md"

$context = [PSCustomObject]@{
    schema_version = 1
    generated_at = [DateTimeOffset]::Now.ToString("o")
    mode = $Mode
    repository = [PSCustomObject]@{
        root = $repoRoot
        recent_commits = @($recentCommits)
    }
    git = [PSCustomObject]@{
        status_entries = @($statusLines)
        dirty_paths = @($dirtyPaths)
        diff_stat = @($diffStat)
        cached_diff_stat = @($cachedDiffStat)
    }
    risk = [PSCustomObject]@{
        highest = $highestRisk
        touched_subsystems = @($touchedSubsystems)
        matched_rules = @($matchedRules | ForEach-Object {
            [PSCustomObject]@{
                name = [string]$_.name
                subsystem = [string]$_.subsystem
                risk = [string]$_.risk
                reason = [string]$_.reason
            }
        })
    }
    change_groups = $changeGroups
    validation = [PSCustomObject]@{
        guidance = @($validationGuidance)
        plan = $validationPlan
        gaps = $validationGaps
        evidence_index = $validationEvidenceIndex
        coverage_matrix = $validationCoverageMatrix
    }
    documentation = [PSCustomObject]@{
        guidance = @($docGuidance)
    }
    change_signals = $changeSignals
    evidence_freshness = $evidenceFreshness
    diagnostics = $diagnostics
    ai_review_contract = $aiReviewContract
    engine_config = [PSCustomObject]@{
        path = (Join-Path $repoRoot "product/config/Engine.ini")
        snapshot = $engineConfig
    }
    perf_gate = $perfGate
    logs = $logSignals
    outputs = [PSCustomObject]@{
        report_root = $reportRoot
        context_path = $contextPath
        report_path = $reportPath
        prompt_path = $promptPath
        validation_plan_path = $validationPlanPath
    }
}

$context | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $contextPath -Encoding UTF8

$templateValues = @{
    "GENERATED_AT" = $context.generated_at
    "REPO_ROOT" = $repoRoot
    "OUTPUT_DIR" = $reportRoot
    "STATUS_ENTRIES" = (Format-ListBlock @($statusLines))
    "HIGHEST_RISK" = $highestRisk
    "TOUCHED_SUBSYSTEMS" = (Format-ListBlock @($touchedSubsystems))
    "MATCHED_RULES" = (Format-RuleBlock @($matchedRules))
    "AI_REVIEW_CONTRACT" = (Format-AIReviewContractBlock $aiReviewContract)
    "CHANGE_GROUPS" = (Format-ChangeGroupBlock $changeGroups)
    "CHANGE_SIGNAL_SUMMARY" = $changeSignals.summary
    "CHANGE_SIGNALS" = (Format-ChangeSignalBlock @($changeSignals.items))
    "VALIDATION_EVIDENCE_INDEX" = (Format-ValidationEvidenceIndexBlock $validationEvidenceIndex)
    "VALIDATION_COVERAGE_MATRIX" = (Format-ValidationCoverageMatrixBlock $validationCoverageMatrix)
    "EVIDENCE_FRESHNESS" = (Format-EvidenceFreshnessBlock $evidenceFreshness)
    "VALIDATION_GAPS" = (Format-ValidationGapBlock $validationGaps)
    "DIAGNOSTIC_FINDINGS" = (Format-ListBlock @($diagnostics.findings))
    "SUGGESTED_NEXT_STEPS" = (Format-ListBlock @($diagnostics.next_steps))
    "VALIDATION_GUIDANCE" = (Format-ListBlock @($validationGuidance))
    "VALIDATION_PLAN" = (Format-ValidationPlanBlock $validationPlan)
    "VALIDATION_COMMANDS" = (Format-ValidationCommandBlock $validationPlan)
    "VALIDATION_MANUAL_CHECKS" = (Format-ValidationManualCheckBlock $validationPlan)
    "DOC_GUIDANCE" = (Format-ListBlock @($docGuidance))
    "RECENT_COMMITS" = (Format-ListBlock @($recentCommits))
    "ENGINE_CONFIG" = "``````ini" + [Environment]::NewLine + $engineConfig + [Environment]::NewLine + "``````"
    "PERF_GATE_SUMMARY" = (Format-ListBlock @($perfGate.summary, $perfGate.path))
    "LOG_SUMMARY" = (Format-ListBlock @($logSignals.summary, @($logSignals.snippets)))
}

Format-Template -TemplatePath (Join-Path $templatesRoot "report.md.template") -Values $templateValues |
    Set-Content -LiteralPath $reportPath -Encoding UTF8
Format-Template -TemplatePath (Join-Path $templatesRoot "prompt.md.template") -Values $templateValues |
    Set-Content -LiteralPath $promptPath -Encoding UTF8
Format-Template -TemplatePath (Join-Path $templatesRoot "validation-plan.md.template") -Values $templateValues |
    Set-Content -LiteralPath $validationPlanPath -Encoding UTF8

Write-Host "AIDevDoctor report: $reportPath"
Write-Host "AIDevDoctor prompt: $promptPath"
Write-Host "AIDevDoctor validation plan: $validationPlanPath"
