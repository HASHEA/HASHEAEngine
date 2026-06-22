"""Cross-module edit ledger (harness-neutral).

When an AshAgent write lands outside the active role's modules, the PreToolUse
adapter does NOT block it (soft warning), but records it here for traceability.
This is the "记账" half of the soft-warning rule: edits are allowed but leave a
trail, which QA / review can inspect as evidence of bypassing the requirement
system.

In a later milestone this should fold into the requirement system. For M1 it
appends JSONL to a local, gitignored ledger under Intermediate/.
"""

from __future__ import annotations

import json
import os
from datetime import datetime, timezone

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))


def _repo_root() -> str:
    return os.path.normpath(os.path.join(_THIS_DIR, "..", ".."))


def ledger_path() -> str:
    return os.path.join(_repo_root(), "Intermediate", "ashagent", "cross-module-edits.jsonl")


def record(entry: dict) -> str:
    """Append a cross-module edit record. Returns the ledger path.

    entry is expected to carry: tool, path, role, user, active_requirement, reason.
    """
    path = ledger_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    record = dict(entry)
    record["logged_at"] = datetime.now(timezone.utc).astimezone().isoformat()
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")
    return path
