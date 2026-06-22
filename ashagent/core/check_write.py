"""AshAgent write-permission core (harness-neutral).

Input contract (a plain dict):
    {
        "tool":  "Edit" | "Write" | ...,     # tool name (informational)
        "path":  "<absolute or repo-relative file path>",
        "mode":  "AshAgent" | "ReadOnly" | None,
        "role":  "EngineDev" | ... | None,
        "active_requirement": "<id>" | None,
    }

Output contract (a plain dict):
    {
        "decision": "allow" | "deny" | "warn",
        "reason":   "<human/model readable explanation>",
        "cross_module": bool,   # True when a write lands outside the role's modules
    }

Decision rules (evaluated top to bottom):
    - Path outside the repo root             -> allow (out of scope; not our business).
    - Session state file (session-*.json)    -> allow (identity self-service bootstrap).
    - Requirement files (ashagent/requirements/**) -> allow (requirement mgmt is meta;
      the first requirement is authored before any claim exists -- chicken-and-egg).
    - ReadOnly mode            -> deny everything (analysis only).
    - No session state at all  -> deny (must initialise via /ash-start).
    - AshAgent write with no active_requirement              -> deny (writes must bind a requirement).
    - AshAgent + write outside every protected root          -> allow (public area; req strong-check NOT applied).
    - AshAgent + write to a protected root: the active requirement must exist, be in a
      write-OK state (claimed/in-progress) and be owned by this role, else -> deny.
    - AshAgent + protected root owned by role                -> allow.
    - AshAgent + protected root NOT owned by role            -> warn (soft) + flag cross_module.

The first two rules run BEFORE the mode/init checks on purpose:
    - Out-of-repo: this system governs the in-repo codebase modules only. Writes to
      plan files, memory, /tmp, the home dir, etc. are none of its concern.
    - Session state file: it is the identity bootstrap, so it must stay writable even
      when the session is uninitialised -- otherwise /ash-start could never write the
      first state file (chicken-and-egg). Trade-off: any session (incl. ReadOnly) can
      rewrite its own state and self-promote. Acceptable: this is a social boundary +
      ledger system, not a hard sandbox; mode is a self-declared identity.

This module performs no I/O beyond reading roles.json; it never reads stdin,
env, or harness-specific payloads. Adapters translate harness events into the
input contract and render the output back into harness-specific responses.
"""

from __future__ import annotations

import json
import os
from fnmatch import fnmatch

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_ROLES_PATH = os.path.join(_THIS_DIR, "..", "permissions", "roles.json")

# Repo-relative glob for the per-session identity state files. These must stay
# writable even before a session is initialised (bootstrap). See module docstring.
SESSION_STATE_GLOB = "Intermediate/ashagent/session-*.json"

# Repo-relative root for requirement files. Requirement management is a meta
# process: the very first requirement is authored before any claim can exist, so
# this tree must stay writable regardless of the active requirement.
REQUIREMENTS_DIR_REL = "ashagent/requirements"


def _repo_root() -> str:
    # ashagent/core/ -> repo root is two levels up.
    return os.path.normpath(os.path.join(_THIS_DIR, "..", ".."))


def _inside_repo(path: str) -> bool:
    """True when an absolute/relative path resolves under the repo root.

    Relative paths are treated as repo-relative (inside). Absolute paths are
    compared case-insensitively against the repo root prefix.
    """
    if not path:
        return False
    p = path.replace("\\", "/")
    # A relative path (no drive letter, no leading slash) is repo-relative.
    if not (len(p) >= 2 and p[1] == ":") and not p.startswith("/"):
        return True
    root = _repo_root().replace("\\", "/").rstrip("/")
    return p.lower() == root.lower() or p.lower().startswith(root.lower() + "/")


def load_roles(roles_path: str | None = None) -> dict:
    path = roles_path or _ROLES_PATH
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def normalize_path(path: str) -> str:
    """Return a repo-relative, forward-slash path when possible.

    Accepts absolute paths inside the repo or already-relative paths.
    """
    if not path:
        return ""
    p = path.replace("\\", "/")
    root = _repo_root().replace("\\", "/")
    # Strip a trailing slash on root for clean prefix comparison.
    root_stripped = root.rstrip("/")
    # Case-insensitive drive letters on Windows: compare lowercased prefixes.
    if p.lower().startswith(root_stripped.lower() + "/"):
        p = p[len(root_stripped) + 1:]
    return p.lstrip("/")


def _matches_any(rel_path: str, patterns: list[str]) -> bool:
    return any(fnmatch(rel_path, pat) for pat in patterns)


def _under_protected_root(rel_path: str, protected_roots: list[str]) -> bool:
    return any(
        rel_path == root or rel_path.startswith(root + "/")
        for root in protected_roots
    )


def evaluate(inp: dict, roles_data: dict | None = None, req_loader=None) -> dict:
    roles_data = roles_data or load_roles()
    protected_roots = roles_data.get("protected_roots", [])
    roles = roles_data.get("roles", {})

    mode = inp.get("mode")
    role = inp.get("role")
    active_req = inp.get("active_requirement")
    raw_path = inp.get("path", "")
    rel = normalize_path(raw_path)

    # 0a. Outside the repo root: out of scope, not our business.
    if not _inside_repo(raw_path):
        return {
            "decision": "allow",
            "reason": "Path is outside the repo root; AshAgent governs in-repo modules only.",
            "cross_module": False,
        }

    # 0b. Session state file: identity bootstrap, always writable (chicken-and-egg).
    if fnmatch(rel, SESSION_STATE_GLOB):
        return {
            "decision": "allow",
            "reason": "Session state file (identity self-service bootstrap); always writable.",
            "cross_module": False,
        }

    # 0c. Requirement files: requirement management is a meta process; the first
    # requirement is authored before any claim exists, so always writable.
    if rel == REQUIREMENTS_DIR_REL or rel.startswith(REQUIREMENTS_DIR_REL + "/"):
        return {
            "decision": "allow",
            "reason": "Requirement file (requirement management is meta); always writable.",
            "cross_module": False,
        }

    # 1. No session initialised.
    if not mode:
        return {
            "decision": "deny",
            "reason": "AshAgent session not initialised. Run /ash-start to choose a mode and role before writing.",
            "cross_module": False,
        }

    # 2. ReadOnly: analysis only.
    if mode == "ReadOnly":
        return {
            "decision": "deny",
            "reason": "ReadOnly mode: AI has no write permission. Make edits manually in the IDE, or restart in AshAgent mode.",
            "cross_module": False,
        }

    # From here mode == AshAgent (treat any non-ReadOnly as AshAgent).
    # 3. Writes must bind an active requirement.
    if not active_req:
        return {
            "decision": "deny",
            "reason": "AshAgent mode: every write must bind an active requirement. Claim or create a requirement first.",
            "cross_module": False,
        }

    role_def = roles.get(role or "", {})
    writable = role_def.get("writable", [])

    in_protected = _under_protected_root(rel, protected_roots)

    # 4. Public area (outside every protected root): allow.
    # Requirement strong-check is intentionally NOT applied here -- only AshAgent
    # mode + a non-empty active_requirement is required (M2 behaviour).
    if not in_protected:
        return {
            "decision": "allow",
            "reason": "Public area write (outside protected module roots).",
            "cross_module": False,
        }

    # 5. Protected root: the active requirement must be real, claimed by this
    # role, and in a write-OK state. This is the "code write must bind a real
    # requirement" gate. req_loader is injectable for tests (default: load from
    # ashagent/requirements/).
    from ashagent.core import requirements as _req

    loader = req_loader if req_loader is not None else _req.load
    req_obj = loader(active_req)
    if not _req.is_valid_for_write(req_obj, role):
        return {
            "decision": "deny",
            "reason": (
                f"Requirement '{active_req}' does not authorise this write: it must exist, "
                f"be claimed by {role}, and be in state claimed/in-progress. "
                "Run /ash-claim to bind a real requirement, or /ash-advance it to in-progress."
            ),
            "cross_module": False,
        }

    # 6. Inside a protected root owned by this role: allow.
    if _matches_any(rel, writable):
        return {
            "decision": "allow",
            "reason": f"Within {role} writable scope.",
            "cross_module": False,
        }

    # 7. Inside a protected root NOT owned by this role: soft warning + flag.
    return {
        "decision": "warn",
        "reason": (
            f"SOFT WARNING: '{rel}' is outside your module ({role}). "
            "Allowed but logged as a cross-module edit. The clean path is to file a "
            "requirement for the owning role instead."
        ),
        "cross_module": True,
    }
