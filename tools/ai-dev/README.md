# AIDevDoctor

AIDevDoctor is a read-only development helper for AshEngine. It collects the current workspace context and generates an AI-friendly report under `Intermediate/test-reports/ai-dev/<timestamp>/`.

It is not an Engine runtime feature and does not modify project files, build outputs, `Engine.ini`, or source code.

## Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode Report
```

Optional log and PerfGate context:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode Report -IncludeLogs -IncludePerfGate
```

Focused validation-plan mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

Self-test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestAIDevDoctor.ps1
```

## Outputs

- `context.json`: structured workspace, risk, validation, docs, log, and PerfGate context.
- `report.md`: human-readable report.
- `prompt.md`: context formatted for AI review.
- `validation-plan.md`: focused validation command and manual-check plan.

The report includes rule-based diagnostic findings and suggested next steps. These are conservative hints for a developer or AI reviewer; they do not replace build, smoke, backend, or PerfGate validation evidence.

The report includes an AI review contract that carries key repository handoff rules: Engine / Editor boundaries, unrelated dirty-file handling, validation expectations, generated artifact locations, and README maintenance.

The report includes dirty change groups from `rules/change-group-rules.json`. These path-based groups help separate AIDev tooling, runtime config, scene assets, render/RHI/shader code, documentation, and general developer tooling before assigning validation scope.

The report also includes change signals from `rules/change-signal-rules.json`. These rules scan changed text for render, shader, config, profiling, error-flow, documentation, and tooling hints.

The generated context also includes `validation.plan`, a read-only list of suggested commands and manual checks inferred from dirty paths, path-risk rules, highest risk, change signals, and validation coverage. AIDevDoctor writes the plan only; it does not execute builds, smoke tests, PerfGate, or config changes.

The generated context also includes `validation.evidence_index`, which lists recent PerfGate summaries and runs, runtime logs, developer logs, and previous AIDevDoctor contexts. This is a read-only evidence directory; it does not prove that a result is fresh for the current dirty workspace.

The generated context also includes `validation.coverage_matrix`, which maps recent PerfGate run evidence onto the standard Sandbox / Editor and Vulkan / DX12 matrix. Cells distinguish fresh `PASS` evidence from stale, missing, optional, or review-needed evidence.

The generated context also includes `evidence_freshness`, which compares the newest dirty file timestamp with requested PerfGate and log evidence. This helps avoid treating old reports or logs as proof for newer workspace changes.

The generated context also includes `validation.gaps`, which turns path risk and evidence freshness into explicit missing-evidence items such as stale runtime validation, missing fresh render logs, or missing backend-confirmation logs.

## Scope

The tool performs rule-based workspace analysis only. It does not call an LLM, does not run builds, and does not execute validation commands.
