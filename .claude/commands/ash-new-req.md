---
description: Intake a new requirement from natural language — generate structured JSON, validate, and (if valid) mark it reviewed and offer to claim it.
allowed-tools: Bash(python:*), AskUserQuestion, Read
argument-hint: [free-text description of the feature or bug]
---

You are running the **AshAgent requirement intake**. Turn a natural-language ask
into a structured, validated requirement file. Source of truth is
`ashagent/requirements/<ID>.json`; a rendered `<ID>.md` view is written alongside.

## 1. Gather the requirement

Use `$ARGUMENTS` as the initial description if present; otherwise ask the user
what they want. Then determine these fields, asking via AskUserQuestion only for
what you cannot confidently infer:

- **order_kind**: `feature` or `bug`.
- **bug_type** (bug only): `crash` | `performance` | `appearance`.
- **role** (owning role, decides who can claim + write scope): `EngineDev` |
  `EditorDev` | `GameDev` | `QA`. (Authoritative module map: `ashagent/permissions/roles.json`.)
- **title**, **description**.
- **acceptance_criteria**: a non-empty list of testable checks.
- Type-specific block:
  - crash → **repro**: `repro_class` (A|B|C), `target`, `backend`, `config`,
    `scene`, `engine_ini`, `app_args`, `manual_steps` (align to `run.bat` params).
  - performance → **perf**: `goal_kind` (no_regression|improve_by|meet_cap),
    `target_metric` (see `tools/perf/perf_gate_baselines.json`), `improve_pct`
    (if improve_by), `cap` (if meet_cap), `scene`.
  - appearance → **capture**: `expectation`, `scene`, `camera`, `frame`,
    `backends` (default `["DX12","Vulkan"]`).

## 2. Create + validate

Resolve the user via `git config user.name`. Build a Python dict literal for the
fields you gathered and run the snippet (substitute the real values). It
constructs the requirement, validates it, and on success saves it as `reviewed`.

```bash
python - <<'PY'
import sys, json; sys.path.insert(0, '.')
from ashagent.core import requirements as R

created_by = "REPLACE_USER"
req = R.new_requirement(
    order_kind="REPLACE",            # feature | bug
    bug_type=None,                    # crash|performance|appearance, or None for feature
    title="REPLACE",
    description="REPLACE",
    role="REPLACE",                   # EngineDev|EditorDev|GameDev|QA
    acceptance_criteria=["REPLACE"],
    created_by=created_by,
    repro=None,                       # dict for crash, else None
    perf=None,                        # dict for performance, else None
    capture=None,                     # dict for appearance, else None
)
ok, issues = R.validate(req)
if not ok:
    print("VALIDATION_FAILED")
    print(json.dumps(issues, ensure_ascii=False, indent=2))
else:
    R.save(req)
    r = R.transition(req["id"], "reviewed", by_role=req["role"], by_user=created_by, note="intake validated")
    print("CREATED", req["id"], "->", "reviewed" if r["ok"] else r["reason"])
PY
```

- If it prints `VALIDATION_FAILED`, tell the user exactly which fields are
  missing/invalid, gather them, and re-run. Do NOT save an invalid requirement.
- If it prints `CREATED <ID>`, report the new id and that it is now `reviewed`.

## 3. Offer to claim

Ask the user (AskUserQuestion) whether to claim it now. If yes, tell them to run
`/ash-claim` (or run the claim flow directly per `ash-claim.md`). Claiming binds
the requirement to this session so code writes are authorised.
