# AshEngine Agent Rules

This file is the default entry point for any AI coding agent working in this
repository (Claude Code, Codex, or other harnesses). It defines the project's
hard boundaries, code style, and the **AshAgent workflow** every agent must follow.

## Scope

- Applies to the whole repository unless a deeper directory provides a more specific `AGENTS.md` or `AGENTS.override.md`.
- Machine-specific paths are kept in `AGENTS.local.md` (gitignored).

## Hard Boundaries

- Do not modify Editor code under `project/src/editor` unless the user explicitly overrides this rule for a specific task.
- Respect the Engine / Editor boundary.
- Engine-side work should stay in Engine modules and public Engine-facing abstractions.
- Do not push backend-specific or RHI-internal details directly into Editor-facing code.

## Design And Code Style

- Prefer standard, idiomatic C++.
- Favor clear ownership, small coherent abstractions, and maintainable interfaces over ad-hoc patches.
- When multiple solutions are possible, prefer the more elegant and standards-aligned one as long as it fits the existing codebase.

## Profiling Instrumentation

- All new or modified render passes, RenderGraph passes, compute dispatch paths, and known performance hotspots should carry Tracy instrumentation through the existing `Base/hprofiler.h` facade.
- Prefer meaningful pass / scope names and useful counts or object names over many tiny per-resource or per-draw zones.
- Do not include Tracy headers in public headers; keep profiling points in `.cpp` implementation files.

## Reference Baseline

- For engine architecture, rendering infrastructure, threading, ECS, asset systems, and general low-level design, implementations may heavily reference Unreal Engine 5.7.
- The local UE 5.7 source path is recorded per-machine in `AGENTS.local.md` (not committed).

## Local Tool Paths

- Machine-specific tool paths (MSBuild, WinDbg, CDB, UE reference source) live in `AGENTS.local.md`, which is gitignored. Consult that file for concrete paths on this machine; do not hardcode them into committed files.

## Generated Artifact Locations

- Do not place ad-hoc generated reports, debugger logs, build logs, test logs, or temporary validation outputs directly in the repository root.
- Put build logs, CDB logs, validation logs, and similar text diagnostics under `Intermediate/logs`.
- Put test reports under `Intermediate/test-reports`.
- Put temporary test working files under `Intermediate/test-temp`.
- Put RenderDoc captures under `Intermediate/renderdoc_captures`.
- Put Tracy captures or CSV exports under `Intermediate/tracy_captures`.
- If a new generated artifact category is needed, create a clearly named subdirectory under `Intermediate` instead of writing to the root.

## Documentation Maintenance

- Any code, asset, config, build, validation, architecture, or workflow change must update the root `README.md` in the same task so the repository overview stays current.
- Do not leave important project state, path rules, or workflow changes documented only in `docs/` files or conversation history.

## Validation Baseline

- For Engine, runtime, rendering, RHI, startup/shutdown, asset/scene, configuration, or other shared-path changes, validation is not considered complete until both `Sandbox` and `Editor` have been exercised.
- Default validation matrix:
  - `Sandbox` on `Vulkan`
  - `Sandbox` on `DX12`
  - `Editor` on `Vulkan`
  - `Editor` on `DX12`
- Each validation pass should use the normal startup path and graceful shutdown path.
- Only narrow this matrix when the user explicitly asks for a reduced scope, or when the change is clearly private to a single backend or a single executable.

## Error Handling Preference

- Prefer centralized error handling and single-exit style where practical.
- Avoid scattering many early `if (!x) return;` style exits through a function when the code can be expressed more cleanly with the project's existing process-error pattern.
- Prefer the existing error handling helpers such as:
  - `ASH_PROCESS_ERROR`
  - `ASH_LOG_PROCESS_ERROR`
  - `ASH_PROCESS_ERROR_EXIT`
- When touching older code that still uses repeated direct early returns, opportunistically refactor it toward the project's process-error flow if the change remains local, safe, and readable.
- Do not force this mechanically where it would clearly harm readability.

## AshAgent Workflow

This repository is a single code tree shared by multiple roles. Instead of
separate repos, write permission is governed by **role + requirement** social
boundaries, enforced by hooks. The authoritative design is
`docs/AshAgentSystemDesign.md`; the rules below are the operational summary every
agent must follow.

### Session identity

- Every session has an identity: a **mode** (`AshAgent` = AI may write within its
  role; `ReadOnly` = analysis only), a **role**, an **active requirement**, and a
  user. It lives in `Intermediate/ashagent/session-<id>.json` (local, gitignored).
- Start a session with `/ash-start`. Until a session is initialised, code writes
  are denied by the PreToolUse hook.

### Roles and module ownership

Authoritative map: `ashagent/permissions/roles.json`. Summary:

- **EngineDev** — `project/src/engine`, `project/src/shader`, `project/thirdparty`
- **EditorDev** — `project/src/editor`
- **GameDev** — `project/src/sandbox`, `project/src/game`
- **QA** — no source write scope; verifies requirements.

Writing inside your role's modules is allowed. Writing inside another role's
protected module is allowed but **soft-warned and logged** to a cross-module
ledger — the clean path is to file a requirement for the owning role instead.
Writes outside every protected root (docs, config, public area) only require an
initialised AshAgent session with an active requirement.

### Requirements (code writes must bind one)

- Requirements are structured files under `ashagent/requirements/<ID>.json`
  (source of truth) with a rendered `<ID>.md` view. IDs are `REQ-<YYYY>-<NNNN>`
  (feature) or `BUG-<YYYY>-<NNNN>` (bug: crash / performance / appearance).
- **A write to protected module code is only authorised when the active
  requirement exists, is claimed by your role, and is in state `claimed` or
  `in-progress`.** Otherwise the hook denies it.
- State machine: `draft → reviewed → claimed → in-progress → done → verified`
  (appearance bugs may pass through `awaiting-visual-approval`). Only QA can
  move a requirement to `verified`, and the verifier must differ from the
  implementer.

### Slash commands

- `/ash-start` — establish session mode + role, bind a requirement.
- `/ash-new-req` — intake a new requirement from natural language, validate it.
- `/ash-claim` — claim a `reviewed` requirement owned by your role and bind it.
- `/ash-reqs` — list all requirements (id / kind / state / owner).
- `/ash-advance` — advance a requirement's state through the state machine.

### For non-Claude harnesses

The slash commands above are Claude Code's surface. The underlying logic is
harness-neutral in `ashagent/core/` (`requirements.py`, `check_write.py`,
`session_context.py`). Other harnesses should drive that core directly and
honour the same role/requirement rules.
