---
description: Advance a requirement's state — in-progress / done / awaiting-visual-approval (owner), or verified (QA only, verifier != implementer).
allowed-tools: Bash(python:*), Bash(git config user.name), AskUserQuestion, Read
argument-hint: [requirement id] [target state]
---

You are running the **AshAgent state advance**. It enforces the requirement state
machine and role guards via `requirements.transition`.

Valid edges:

```
claimed -> in-progress -> done                     -> verified
                       -> awaiting-visual-approval  -> verified   (appearance only)
```

- `in-progress` / `done` / `awaiting-visual-approval`: only the **owning role** may advance.
- `awaiting-visual-approval`: only appearance bug orders.
- `verified`: only role **QA**, and the verifier must differ from the implementer
  (`by_user != claim.owner_user`).

## 1. Resolve actor

- **user**: `git config user.name`.
- **role**: read from `Intermediate/ashagent/session-<session_id>.json` (resolve
  session_id from the SessionStart-injected context; never guess it).

## 2. Pick requirement + target

Use `$ARGUMENTS` if it names an id and/or target state. Otherwise run `/ash-reqs`
(or `R.list_all()`) to show candidates and ask the user which requirement and
which target state via AskUserQuestion. Only offer states reachable from the
requirement's current state.

## 3. Transition

Run the snippet (substitute id, target, role, user):

```bash
python - <<'PY'
import sys; sys.path.insert(0, '.')
from ashagent.core import requirements as R
REQ_ID = "REPLACE_ID"
TARGET = "REPLACE_STATE"      # in-progress | done | awaiting-visual-approval | verified
ROLE   = "REPLACE_ROLE"
USER   = "REPLACE_USER"
res = R.transition(REQ_ID, TARGET, by_role=ROLE, by_user=USER, note="via /ash-advance")
print("ADVANCE", res["ok"], res["reason"])
PY
```

If it fails, relay the exact reason (illegal edge, wrong role, verifier ==
implementer, non-appearance order, etc.) — do not retry blindly. If it succeeds,
report the new state.
