---
description: Initialise this AshAgent session — choose entry mode (AshAgent / ReadOnly), role, and bind a requirement. Writes the per-session state file the permission hooks read.
allowed-tools: Bash(git config user.name), Write, AskUserQuestion, Read
---

You are running the **AshAgent session bootstrap**. Your job is to interactively
establish this session's runtime identity and write its state file, which the
PreToolUse / SessionStart hooks read to enforce write permissions.

Follow these steps in order. Do not skip the confirmation step.

## 1. Resolve the session_id

Find this session's id from the SessionStart-injected context already in your
conversation. Look for the line containing `session_id=<id>` (injected by
`ashagent/adapters/claude_code/sessionstart.py`).

- If you find it, use that exact id.
- If you CANNOT find a `session_id=` anywhere in context, STOP and tell the user:
  "找不到 session_id —— 请重启会话以加载 SessionStart 注入，然后重新运行 /ash-start。"
  Do NOT guess, invent, or infer the id from file names. A wrong id writes a state
  file the hooks will never read.

## 2. Resolve the user

Run `git config user.name` to get the accountability subject. If empty, fall back
to asking the user for their name.

## 3. Choose the entry mode

Use AskUserQuestion to ask which entry mode to start in:

- **AshAgent** — AI may write, limited to its role's modules; every code write must
  bind an active requirement. Cross-module writes are allowed with a soft warning + ledger.
- **ReadOnly** — AI has zero write permission (analysis only); the human edits in the IDE.

## 4. Branch on the chosen mode

**If ReadOnly:** set `role = null` and `active_requirement = null`. Skip to step 5.

**If AshAgent:**

a. Use AskUserQuestion to choose the **role**:
   - `EngineDev` — engine runtime / RHI / rendering / shaders / thirdparty
     (`project/src/engine`, `project/src/shader`, `project/thirdparty`)
   - `EditorDev` — editor executable & tooling (`project/src/editor`)
   - `GameDev` — sandbox & game (`project/src/sandbox`, `project/src/game`)
   - `QA` — verification role, no source write scope
   (Authoritative mapping lives in `ashagent/permissions/roles.json` — read it if unsure.)

b. Bind a **real** active_requirement (no more placeholders — that was M2). The
   active requirement must be one that is `claimed` by this role, so code writes
   are authorised. Use AskUserQuestion to offer:
   - **Claim an existing reviewed requirement** — run the `/ash-claim` flow
     (`.claude/commands/ash-claim.md`): list `reviewed` requirements owned by the
     chosen role, claim one, and it writes `active_requirement` into the state
     file for you. If you take this path, `/ash-claim` already wrote the state
     file — skip step 5 and go to step 6.
   - **Create one now** — run the `/ash-new-req` flow to author + validate a new
     requirement (lands in `reviewed`), then claim it via `/ash-claim`. Again the
     claim writes the state file; skip step 5.
   - **Defer (leave unbound)** — set `active_requirement = null` and write the
     state file in step 5. NOTE: with no claimed requirement, writes to protected
     module code will be denied until you `/ash-claim` one; only public-area
     writes (docs/config) work.

## 5. Write the state file

Use the **Write** tool to write `Intermediate/ashagent/session-<session_id>.json`
(substitute the real id). This write is exempt from the permission hook, so it
succeeds even on an uninitialised session.

Content (match `ashagent/SESSION_STATE_SCHEMA.md` exactly):

```json
{
  "schema_version": 1,
  "mode": "<AshAgent|ReadOnly>",
  "role": "<role or null>",
  "active_requirement": "<id or null>",
  "user": "<git user.name>",
  "updated_at": "<current ISO-8601 local timestamp>"
}
```

Use a real current timestamp (e.g. ask Bash for `date` if needed). `role` and
`active_requirement` must be JSON `null` (not the string "null") in ReadOnly mode.

## 6. Report

Print a one-line identity summary: mode, role, active_requirement, user, session_id.
Tell the user that permissions take effect immediately — the PreToolUse hook re-reads
the state file on every write, so no restart is needed. If they later want to switch
mode/role/requirement, they can re-run /ash-start.
