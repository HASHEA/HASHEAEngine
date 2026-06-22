---
description: List all AshAgent requirements (id, kind, state, owner). Read-only overview of ashagent/requirements/.
allowed-tools: Bash(python:*)
---

You are listing the AshAgent requirements. Run the snippet below and present the
table verbatim to the user. Do not invent rows.

```bash
python - <<'PY'
import sys; sys.path.insert(0, '.')
from ashagent.core import requirements as R
rows = R.list_all()
if not rows:
    print("(no requirements yet — run /ash-new-req to create one)")
else:
    print(f"{'ID':<16} {'KIND':<14} {'STATE':<24} OWNER")
    for r in rows:
        kind = r.get('order_kind','?')
        if r.get('bug_type'): kind += f"/{r['bug_type']}"
        owner = (r.get('claim') or {}).get('owner_user') or '-'
        print(f"{r.get('id',''):<16} {kind:<14} {r.get('state',''):<24} {owner}")
PY
```

After printing, if the user wants details on one requirement, read its
`ashagent/requirements/<ID>.md` view.
