"""Claude Code SessionStart adapter.

Reads the Claude Code SessionStart hook payload (stdin JSON), resolves the
runtime session identity via ashagent.core.session_context, and injects a
human/model-readable context string back into the session.

Registered in settings(.local).json:
    SessionStart -> command: python ashagent/adapters/claude_code/sessionstart.py
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

from ashagent.core import session_context  # noqa: E402


def main() -> int:
    raw = sys.stdin.read()
    try:
        payload = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError:
        payload = {}

    session_id = payload.get("session_id", "")
    context = session_context.context_text(session_id)

    out = {
        "hookSpecificOutput": {
            "hookEventName": "SessionStart",
            "additionalContext": context,
        }
    }
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
