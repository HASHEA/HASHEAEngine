---
description: Claim a reviewed requirement owned by this session's role, then bind it as the active requirement so code writes are authorised.
allowed-tools: Bash(python:*), Bash(git config user.name), AskUserQuestion, Read
---

You are running the **AshAgent requirement claim**. Claiming moves a requirement
`reviewed -> claimed` and binds it to this session so the PreToolUse hook will
authorise code writes against it.

## 1. Resolve session_id, user, role

- **session_id**: find `session_id=<id>` in the SessionStart-injected context
  already in this conversation. If you cannot find it, STOP and tell the user to
  restart the session and re-run /ash-claim. Never guess the id.
- **user**: `git config user.name`.
- **role**: read it from `Intermediate/ashagent/session-<session_id>.json`. If the
  session is ReadOnly or has no role, STOP and tell the user to run /ash-start in
  AshAgent mode first — claiming requires a role.

## 2. List claimable requirements

Run the snippet to list `reviewed` requirements matching this session's role:

```bash
python - <<'PY'
import sys; sys.path.insert(0, '.')
from ashagent.core import requirements as R
ROLE = "REPLACE_ROLE"
avail = [r for r in R.list_all() if r.get("state")=="reviewed" and r.get("role")==ROLE]
if not avail:
    print("(none available for role", ROLE, "— create one with /ash-new-req)")
for r in avail:
    print(r["id"], "|", r.get("order_kind"), r.get("bug_type") or "", "|", r.get("title"))
PY
```

Show the list and let the user pick one (AskUserQuestion if more than one).

## 3. Claim it + bind to the session

Run the snippet (substitute the chosen id, role, user, session_id). It claims the
requirement AND rewrites the session state file's `active_requirement` in one go.
Both files are exempt from the write hook, so this always succeeds.

```bash
python - <<'PY'
import sys, json, os; sys.path.insert(0, '.')
from ashagent.core import requirements as R
from ashagent.core import session_context as S
from datetime import datetime, timezone

REQ_ID  = "REPLACE_ID"
ROLE    = "REPLACE_ROLE"
USER    = "REPLACE_USER"
SID     = "REPLACE_SESSION_ID"

res = R.claim(REQ_ID, role=ROLE, user=USER, session_id=SID)
print("CLAIM", res["ok"], res["reason"])
if res["ok"]:
    sp = S.state_path(SID)
    st = json.load(open(sp, encoding="utf-8"))
    st["active_requirement"] = REQ_ID
    st["updated_at"] = datetime.now(timezone.utc).astimezone().isoformat()
    json.dump(st, open(sp, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    print("BOUND active_requirement =", REQ_ID)
PY
```

## 4. Report

If claimed, tell the user: requirement `<ID>` is now `claimed` and bound as the
active requirement; code writes in their role's modules are authorised
immediately (the hook re-reads the state file each write — no restart). If the
claim failed, relay the reason and suggest `/ash-reqs` to inspect state.
