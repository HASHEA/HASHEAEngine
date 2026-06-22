"""Claude Code PreToolUse adapter.

Translates the Claude Code hook payload (stdin JSON) into the harness-neutral
input contract, calls ashagent.core.check_write, and renders the result back as
a Claude Code PreToolUse JSON response.

Registered in settings(.local).json:
    PreToolUse matcher "Edit|Write|MultiEdit|NotebookEdit" ->
        command: python ashagent/adapters/claude_code/pretooluse.py
"""

from __future__ import annotations

import json
import os
import sys

# Make the repo root importable so `import ashagent...` works regardless of cwd.
_THIS = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.normpath(os.path.join(_THIS, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from ashagent.core import check_write, ledger, session_context  # noqa: E402


def _extract_path(tool_input: dict) -> str:
    # Edit/Write/NotebookEdit use file_path; NotebookEdit uses notebook_path.
    return tool_input.get("file_path") or tool_input.get("notebook_path") or ""


def main() -> int:
    raw = sys.stdin.read()
    try:
        payload = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError:
        payload = {}

    session_id = payload.get("session_id", "")
    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}
    path = _extract_path(tool_input)

    state = session_context.load_state(session_id) or {}

    neutral = {
        "tool": tool_name,
        "path": path,
        "mode": state.get("mode"),
        "role": state.get("role"),
        "active_requirement": state.get("active_requirement"),
    }

    result = check_write.evaluate(neutral)
    decision = result["decision"]
    reason = result["reason"]

    if decision == "warn" and result.get("cross_module"):
        ledger.record({
            "tool": tool_name,
            "path": check_write.normalize_path(path),
            "role": state.get("role"),
            "user": state.get("user"),
            "active_requirement": state.get("active_requirement"),
            "reason": reason,
        })

    if decision == "deny":
        out = {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": reason,
            }
        }
    elif decision == "warn":
        out = {
            "continue": True,
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
                "additionalContext": reason,
            },
        }
    else:  # allow
        out = {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
            }
        }

    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
