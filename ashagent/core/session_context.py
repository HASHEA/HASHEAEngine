"""Session-state reader (harness-neutral).

Reads the runtime session-state file for a given session id and produces:
    - the parsed state dict (or None if absent)
    - a human/model-readable context string for SessionStart injection

The state file lives at Intermediate/ashagent/session-<id>.json (local, not
committed). See ashagent/SESSION_STATE_SCHEMA.md.
"""

from __future__ import annotations

import json
import os

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))


def _repo_root() -> str:
    return os.path.normpath(os.path.join(_THIS_DIR, "..", ".."))


def state_path(session_id: str) -> str:
    safe = "".join(c for c in (session_id or "unknown") if c.isalnum() or c in "-_")
    return os.path.join(_repo_root(), "Intermediate", "ashagent", f"session-{safe}.json")


def load_state(session_id: str) -> dict | None:
    path = state_path(session_id)
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


def context_text(session_id: str) -> str:
    sid = session_id or "(unknown)"
    st = load_state(session_id)
    if st is None:
        return (
            f"[AshAgent] No session state found (session_id={sid}). This session is "
            "uninitialised: run /ash-start to choose a mode (AshAgent / ReadOnly) and a "
            "role (EngineDev / EditorDev / GameDev / QA) before making changes."
        )
    mode = st.get("mode", "(unset)")
    role = st.get("role", "(unset)")
    req = st.get("active_requirement") or "(none)"
    user = st.get("user", "(unset)")
    return (
        f"[AshAgent] Session identity loaded — mode={mode}, role={role}, "
        f"active_requirement={req}, user={user}, session_id={sid}. "
        "Write permissions are enforced by the PreToolUse hook per your role's "
        "module ownership; cross-module writes are allowed with a soft warning and logged."
    )
