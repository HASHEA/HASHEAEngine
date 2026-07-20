# PerfGate Sample-Time Coverage Implementation Plan

> **Superseded in part (2026-07-15):** 初版使用 `frames_sampled * CPU avg` 推断采样窗口，后续验证证明 renderer-only CPU 时间不能覆盖主循环 stall。最终合同改为 schema v2 wall-clock elapsed span + maximum gap + 30 Hz minimum density，并把受保护 import 绑定到同一 byte snapshot 的 raw telemetry canonical record。权威行为见 `docs/specs/modules/tools.md` 与 `docs/sdd/SDD-2026-07-14-perf-gate-sample-time-coverage.md`；下文保留为 TDD 历史，不再作为现行公式。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make PerfGate reject telemetry and protected report imports whose sampled frames account for less than 90% of the configured sampling window.

**Architecture:** Keep the policy in `RunPerfGate.ps1` as one pure calculation used by both live telemetry validation and report import. The profile's `sample_seconds` is authoritative, so schema-v1 telemetry remains readable while every summary records enough data for import to recompute rather than trust a serialized ratio.

**Tech Stack:** PowerShell 5.1-compatible scripts, JSON schema v2 additive fields, existing PerfGate self-test and batch regression scripts.

---

### Task 1: Add live-telemetry RED cases

**Files:**
- Modify: `scripts/RunPerfGate.ps1:174-235`
- Modify: `scripts/RunPerfGate.ps1:2804-2861`
- Test: `scripts/RunPerfGate.ps1:3183-3197`

- [x] **Step 1: Make the shared telemetry fixture represent a complete 30-second sample**

Keep its existing 2 ms CPU values and change only `frames_sampled` from `120` to `15000`, so `15000 * 2 ms = 30000 ms` without changing comparison-value expectations elsewhere.

- [x] **Step 2: Add boundary and observed-anomaly assertions before production code**

```powershell
$boundaryTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
$boundaryTelemetry.frames_sampled = 13500
$boundaryRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
Test-TelemetryData -Record $boundaryRecord -Telemetry $boundaryTelemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
Assert-SelfTest (
    $boundaryRecord.status -eq "PASS" -and
    [Math]::Abs([double](Get-ProfileProperty $boundaryRecord "sample_time_coverage") - 0.90) -le 0.000001
) "Exactly 90% sample-time coverage must pass and be serialized."

$incompleteTelemetry = New-SelfTestTelemetryV2 -ProfileConfig $vegetationProfile
$incompleteTelemetry.frames_sampled = 704
$incompleteTelemetry.cpu_frame_time_ms.avg = 14.5188190340909
$incompleteRecord = New-RunRecord "Sandbox" "Vulkan" "Sandbox.exe" "telemetry.json" "stdout.log" "stderr.log"
Test-TelemetryData -Record $incompleteRecord -Telemetry $incompleteTelemetry -ProfileConfig $vegetationProfile -Baseline $emptyBaselineForTelemetry -Profile "VegetationFullPipeline" -Configuration "Release" -TelemetryMode "Profile"
Assert-SelfTest (
    $incompleteRecord.status -eq "FAIL" -and
    (@($incompleteRecord.failures) -join " ") -match "sample time coverage"
) "A 30-second report with only about 10.22 seconds of observed frame time must fail closed."
```

- [x] **Step 3: Run the self-test and verify RED**

Run:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -SelfTest
```

Expected: exit 1; the new cases report that `sample_time_coverage` is missing and the `0.341` anomaly was accepted. Existing assertions must not introduce parse errors.

### Task 2: Implement the live sample-time guard

**Files:**
- Modify: `scripts/RunPerfGate.ps1:1-20`
- Modify: `scripts/RunPerfGate.ps1:174-235`
- Modify: `scripts/RunPerfGate.ps1:509-550`
- Modify: `scripts/RunPerfGate.ps1:2400-2450`

- [x] **Step 1: Add the record fields and one two-call-site policy helper**

```powershell
$script:PerfGateMinimumSampleTimeCoverage = 0.90

function Get-PerfGateSampleTimeCoverage {
    param(
        [int64]$FramesSampled,
        [double]$CpuFrameTimeAvgMs,
        [double]$SampleSeconds
    )

    $validatedCpuFrameTimeAvgMs = ConvertTo-PerfGateFiniteDouble $CpuFrameTimeAvgMs
    $validatedSampleSeconds = ConvertTo-PerfGateFiniteDouble $SampleSeconds
    if ($FramesSampled -le 0 -or
        $null -eq $validatedCpuFrameTimeAvgMs -or $validatedCpuFrameTimeAvgMs -lt 0.0 -or
        $null -eq $validatedSampleSeconds -or $validatedSampleSeconds -le 0.0) {
        return $null
    }

    $observedFrameTimeMs = [double]$FramesSampled * $validatedCpuFrameTimeAvgMs
    $expectedFrameTimeMs = $validatedSampleSeconds * 1000.0
    $coverage = $observedFrameTimeMs / $expectedFrameTimeMs
    if ($null -eq (ConvertTo-PerfGateFiniteDouble $observedFrameTimeMs) -or
        $null -eq (ConvertTo-PerfGateFiniteDouble $coverage)) {
        return $null
    }

    [PSCustomObject][ordered]@{
        sample_seconds = $validatedSampleSeconds
        observed_frame_time_ms = $observedFrameTimeMs
        coverage = $coverage
    }
}
```

Add `sample_seconds`, `observed_frame_time_ms`, and `sample_time_coverage` to `New-RunRecord`. This helper has two production consumers: live telemetry validation and protected report import.

- [x] **Step 2: Validate live telemetry after CPU avg is parsed**

Use `ProfileConfig.sample_seconds` as the authoritative duration, populate all three record fields, and add this failure when the helper returns null or coverage is below 0.90:

```powershell
Add-Failure $Record ("Sample time coverage {0:N6} was below required {1:N2}: observed {2:N3} ms across {3} frames for a {4:N3} ms window" -f ...)
```

Do not change GPU coverage, performance thresholds, workload identity, or schema version.

- [x] **Step 3: Run self-test and verify first GREEN**

Run the Task 1 command. Expected: exit 0 and `Perf gate self-test passed.`

### Task 3: Make protected report import recompute coverage

**Files:**
- Modify: `scripts/RunPerfGate.ps1:1610-1685`
- Test: `scripts/RunPerfGate.ps1:4403-4554`

- [x] **Step 1: Add an import-bypass RED case**

Make the valid candidate explicit:

```powershell
$candidateVulkan.frames_sampled = 15000
$candidateVulkan.cpu_frame_time_avg_ms = 2.0
$candidateVulkan.sample_seconds = 30.0
$candidateVulkan.observed_frame_time_ms = 30000.0
$candidateVulkan.sample_time_coverage = 1.0
```

Add an unsafe mutation that forges the serialized coverage while retaining the observed anomaly:

```powershell
[PSCustomObject]@{
    name = "forged-sample-time-coverage"
    expected = "sample time coverage"
    mutate = {
        param($value)
        $value.runs[0].frames_sampled = 704
        $value.runs[0].cpu_frame_time_avg_ms = 14.5188190340909
        $value.runs[0].sample_seconds = 30.0
        $value.runs[0].observed_frame_time_ms = 30000.0
        $value.runs[0].sample_time_coverage = 1.0
    }
}
```

Add a second unsafe mutation that removes `sample_seconds`; expect the same `sample time coverage` rejection. These two cases prove both missing evidence and forged evidence fail before baseline cloning.

Run the self-test. Expected RED: import accepts the forged report, so the new unsafe case reports failure while the baseline-object immutability assertion remains testable.

- [x] **Step 2: Recompute and cross-check in import**

For each run, require `record.sample_seconds` to equal `ProfileConfig.sample_seconds`, recompute via `Get-PerfGateSampleTimeCoverage`, require coverage `>= 0.90`, and require serialized `observed_frame_time_ms` / `sample_time_coverage` to match recomputed values within `0.001 ms` / `0.000001`. Throw before cloning the baseline; the error must contain `sample time coverage`.

- [x] **Step 3: Run self-test and verify second GREEN**

Run the self-test. Expected: exit 0; valid import succeeds, forged/missing/inconsistent evidence throws, and every rejected import leaves the baseline object empty.

### Task 4: Update contracts and run full CPU verification

**Files:**
- Modify: `docs/sdd/SDD-2026-07-14-perf-gate-sample-time-coverage.md`
- Modify: `docs/PerfGateUsageGuide.md`
- Modify: `docs/specs/modules/tools.md`
- Modify: `docs/plans/2026-07-14-gpu-driven-foundation.md`
- Test: `scripts/TestRunPerfGate.ps1`

- [x] **Step 1: Document the additive summary fields and fail-closed import rule**

Set the Mini SDD status to `Done`. State that the profile duration is authoritative, schema v2 is unchanged, and report import recomputes the ratio rather than trusting JSON fields. Record the real `704 * 14.518819 / 30000 = 0.340708...` evidence in the active GPU-driven plan.

- [x] **Step 2: Run parser and focused/full tool tests**

```bat
powershell -NoProfile -Command "[void][scriptblock]::Create((Get-Content -Raw scripts/RunPerfGate.ps1))"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestAIDevDoctor.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
RunPerfGate.bat --help
RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release -DryRun
git diff --check
```

Expected: every command exits 0; baseline SHA-256 remains `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214` with zero diff.

- [x] **Step 3: Review the exact diff and commit only the tool contract**

Stage only `scripts/RunPerfGate.ps1`, `scripts/TestRunPerfGate.ps1`, the Mini SDD, usage guide, tools spec, and this implementation plan. Exclude the active foundation plan, plan index, baseline, and Tracy binaries.

```bat
git commit -m "fix(perf): reject incomplete sample windows"
```

### Task 5: Re-establish Gate B evidence on the new SHA

**Files:**
- Protected output: `tools/perf/perf_gate_baselines.json`
- Modify: `docs/plans/2026-07-14-gpu-driven-foundation.md`
- Modify: `docs/plans/README.md`

- [ ] **Step 1: Coordinate a CPU/GPU-exclusive window and collect a wholly new candidate group**

Run `RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release` strictly serially. Discard any run with sample-time coverage below 0.90, WARN/FAIL, incomplete cleanup, identity mismatch, or missing metrics. Do not reuse reports from an older source SHA.

- [ ] **Step 2: Enforce the approved spread contract and import one representative report**

Require all eight backend/core spreads to pass the existing 3%/8%/3%/5% contract. Select the unique minimum eight-metric normalized median-distance report, hash its exact `summary.json`, and use `-BlessBaselineFromReport` with that SHA-256.

- [ ] **Step 3: Run exactly one non-bless COMPARED verification**

Require both backends PASS/COMPARED, 11/11 metrics, total/per-metric GPU coverage at least the profile floor, sample-time coverage at least 0.90, warnings/failures/log rejects zero, configuration restoration, and effective roots zero. Stop on any unexplained WARN/FAIL; never edit the baseline directly.
