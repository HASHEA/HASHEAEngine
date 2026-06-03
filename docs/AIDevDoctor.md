# AIDevDoctor

`scripts/AIDevDoctor.ps1` is a read-only helper for preparing AshEngine development context for AI review.

The tool collects:

- `git status --short`
- `git diff --stat`
- recently touched paths
- recent commits
- `product/config/Engine.ini`
- optional latest `product/logs` signals
- optional latest PerfGate summary
- validation evidence index from recent PerfGate summaries, runtime logs, Intermediate logs, and AIDevDoctor contexts
- validation coverage matrix for Sandbox / Editor across Vulkan / DX12 based on recent PerfGate run evidence
- AI review contract summarizing the key handoff boundaries and required behaviors from repository policy
- dirty change groups inferred from path rules
- a read-only validation plan with suggested commands and manual checks
- evidence freshness for comparing latest logs or PerfGate summaries against dirty file timestamps
- validation gaps inferred from path risk and evidence freshness

It writes reports to:

```text
Intermediate/test-reports/ai-dev/<timestamp>/
```

Generated files:

- `context.json`
- `report.md`
- `prompt.md`
- `validation-plan.md`

Use the tool when asking AI to review current engine work, debug a validation failure, or decide which validation matrix and docs are required.

`context.json` includes a `diagnostics` section with rule-based findings and suggested next steps. These findings are intentionally conservative: they summarize path-based risk, latest log signals, and latest PerfGate status, but they do not prove a change is correct.

`context.json` includes an `ai_review_contract` section. This section packages the repo policy sources, hard boundaries, required behaviors, and review focus that should travel with any AI handoff. It is intentionally duplicated into the generated markdown so an external reviewer sees the same constraints as the local agent.

`context.json` includes `change_groups`, which groups dirty paths through `tools/ai-dev/rules/change-group-rules.json`. Groups separate AIDev tooling, runtime config, scene assets, render/RHI/shader code, documentation, and general developer tooling. They are path-based review hints and do not imply who authored a change.

`context.json` also includes `change_signals`, which scans changed diff lines and small untracked text files against `tools/ai-dev/rules/change-signal-rules.json`. This catches hints such as render pass changes, shader/binding text, runtime config edits, profiling instrumentation, and automation-tool changes.

`context.json` also includes `validation.plan`. The plan is generated from the current dirty paths, path-risk rules, highest inferred risk, change signals, and validation coverage matrix. It can suggest commands such as AIDevDoctor self-test, PerfGate self-test, `git diff --check`, or the standard PerfGate run, plus manual checks for Engine.ini, logs, scene-content separation, missing or stale target/backend coverage, documentation, and Tracy coverage. AIDevDoctor only writes this plan; it does not execute those commands.

`context.json` also includes `validation.evidence_index`. This indexes recent validation-related artifacts such as PerfGate summaries and runs, runtime logs under `product/logs`, developer logs under `Intermediate/logs`, and earlier AIDevDoctor contexts. The index is a directory of available evidence; freshness and applicability are still evaluated separately by `evidence_freshness` and `validation.gaps`.

`context.json` also includes `validation.coverage_matrix`. This condenses recent PerfGate run evidence into the standard runtime matrix: Sandbox / Editor on Vulkan / DX12. Each cell records whether the latest evidence is required for the current path risk, whether it is fresh relative to dirty files, and whether the latest run status is a fresh `PASS`, stale, missing, or needs review.

`context.json` also includes `evidence_freshness`. It compares the newest dirty file timestamp with the latest PerfGate summary and latest log file when those evidence sources are requested. The result is a conservative freshness hint (`fresh`, `stale`, `not_requested`, `unavailable`, or related status), not proof that validation passed.

`context.json` also includes `validation.gaps`. These gaps are inferred from path-risk rules and evidence freshness. For example, high-risk render changes without fresh PerfGate evidence produce a runtime validation gap; Engine.ini changes without fresh logs produce a backend-confirmation gap. Gaps describe missing evidence, not failed validation.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode Report
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode Report -IncludeLogs -IncludePerfGate
```

Generate the same context plus a dedicated validation plan artifact:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestAIDevDoctor.ps1
```

## Boundaries

AIDevDoctor is not part of the Engine runtime. It does not call model providers, does not modify source files, does not alter `Engine.ini`, and does not replace the existing validation scripts.
