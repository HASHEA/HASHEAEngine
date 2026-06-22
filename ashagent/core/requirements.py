"""AshAgent requirement system core (harness-neutral).

Structured requirements are the source of truth that every code write must bind
to. Each requirement is one JSON file under ashagent/requirements/<ID>.json with
its claim state embedded (smallest conflict surface), plus a rendered <ID>.md
human/agent view derived from the JSON.

This module is pure logic + file I/O against ashagent/requirements/. It never
touches stdin, env, or harness payloads. Adapters and slash commands drive it.

State machine (enforced by `transition`, except entry to `claimed` which goes
through `claim`):

    draft -> reviewed -> claimed -> in-progress -> done                     -> verified
                                                 -> awaiting-visual-approval -> verified

Guards:
    - reviewed:                 requires validate() to pass (checked by caller / new-req flow).
    - claimed:                  only via claim() — reviewed + role match + not already claimed.
    - in-progress / done:       only the owning role may advance.
    - awaiting-visual-approval: only appearance bug orders.
    - verified:                 only role == QA, and verifier != implementer (by_user != owner_user).
"""

from __future__ import annotations

import json
import os
import re
from datetime import datetime, timezone

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))

ORDER_KINDS = ("feature", "bug")
BUG_TYPES = ("crash", "performance", "appearance")
ROLES = ("EngineDev", "EditorDev", "GameDev", "QA")
PERF_GOAL_KINDS = ("no_regression", "improve_by", "meet_cap")

STATES = (
    "draft",
    "reviewed",
    "claimed",
    "in-progress",
    "done",
    "awaiting-visual-approval",
    "verified",
)

# State machine edges. Entry to `claimed` is intentionally absent: it is reached
# only through claim(), which also writes the claim block.
EDGES = {
    "draft": {"reviewed"},
    "reviewed": set(),
    "claimed": {"in-progress"},
    "in-progress": {"done", "awaiting-visual-approval"},
    "done": {"verified"},
    "awaiting-visual-approval": {"verified"},
    "verified": set(),
}

# States in which a requirement authorises code writes (used by check_write).
WRITE_OK_STATES = {"claimed", "in-progress"}

_ID_RE = re.compile(r"^(REQ|BUG)-(\d{4})-(\d{4})$")


def _repo_root() -> str:
    return os.path.normpath(os.path.join(_THIS_DIR, "..", ".."))


def _now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat()


def requirements_dir() -> str:
    return os.path.join(_repo_root(), "ashagent", "requirements")


def req_path(req_id: str) -> str:
    return os.path.join(requirements_dir(), f"{req_id}.json")


def req_md_path(req_id: str) -> str:
    return os.path.join(requirements_dir(), f"{req_id}.md")


def _id_prefix(order_kind: str) -> str:
    return "BUG" if order_kind == "bug" else "REQ"


def load(req_id: str) -> dict | None:
    path = req_path(req_id)
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


def list_all() -> list[dict]:
    d = requirements_dir()
    if not os.path.isdir(d):
        return []
    out = []
    for name in sorted(os.listdir(d)):
        if not name.endswith(".json"):
            continue
        try:
            with open(os.path.join(d, name), "r", encoding="utf-8") as f:
                out.append(json.load(f))
        except (OSError, json.JSONDecodeError):
            continue
    return out


def next_id(order_kind: str, year: int | None = None) -> str:
    year = year or datetime.now().year
    prefix = _id_prefix(order_kind)
    d = requirements_dir()
    max_n = 0
    if os.path.isdir(d):
        for name in os.listdir(d):
            if not name.endswith(".json"):
                continue
            m = _ID_RE.match(name[:-5])
            if not m:
                continue
            if m.group(1) == prefix and int(m.group(2)) == year:
                max_n = max(max_n, int(m.group(3)))
    return f"{prefix}-{year}-{max_n + 1:04d}"


def save(req: dict) -> str:
    """Write the requirement JSON (refreshing updated_at) + its rendered md view.

    Returns the JSON path.
    """
    os.makedirs(requirements_dir(), exist_ok=True)
    req["updated_at"] = _now()
    jpath = req_path(req["id"])
    with open(jpath, "w", encoding="utf-8") as f:
        json.dump(req, f, ensure_ascii=False, indent=2)
        f.write("\n")
    with open(req_md_path(req["id"]), "w", encoding="utf-8") as f:
        f.write(render_md(req))
    return jpath


def _empty_claim() -> dict:
    return {"owner_user": None, "owner_role": None, "session_id": None, "claimed_at": None}


def new_requirement(
    *,
    order_kind: str,
    title: str,
    description: str,
    role: str,
    acceptance_criteria: list[str],
    created_by: str,
    bug_type: str | None = None,
    repro: dict | None = None,
    perf: dict | None = None,
    capture: dict | None = None,
    year: int | None = None,
) -> dict:
    """Construct a draft requirement dict (not yet saved).

    Only the type-specific block matching order_kind/bug_type is attached.
    """
    rid = next_id(order_kind, year)
    now = _now()
    req: dict = {
        "schema_version": 1,
        "id": rid,
        "order_kind": order_kind,
        "bug_type": bug_type if order_kind == "bug" else None,
        "title": title,
        "description": description,
        "role": role,
        "acceptance_criteria": list(acceptance_criteria or []),
        "state": "draft",
        "claim": _empty_claim(),
        "created_by": created_by,
        "created_at": now,
        "updated_at": now,
        "history": [
            {"state": "draft", "by_user": created_by, "by_role": role, "at": now, "note": "created"}
        ],
    }
    if order_kind == "bug" and bug_type == "crash":
        req["repro"] = repro or {
            "repro_class": None, "target": None, "backend": None, "config": None,
            "scene": None, "engine_ini": None, "app_args": None, "manual_steps": None,
        }
    elif order_kind == "bug" and bug_type == "performance":
        req["perf"] = perf or {
            "goal_kind": None, "target_metric": None, "improve_pct": None,
            "cap": None, "scene": None,
        }
    elif order_kind == "bug" and bug_type == "appearance":
        req["capture"] = capture or {
            "expectation": None, "scene": None, "camera": None,
            "frame": None, "backends": ["DX12", "Vulkan"],
        }
    return req


def validate(req: dict) -> tuple[bool, list[str]]:
    """Compliance review: required fields present + type-specific block complete."""
    issues: list[str] = []

    if not req.get("id"):
        issues.append("missing id")
    order_kind = req.get("order_kind")
    if order_kind not in ORDER_KINDS:
        issues.append(f"order_kind must be one of {ORDER_KINDS}")
    if not (req.get("title") or "").strip():
        issues.append("title is empty")
    if not (req.get("description") or "").strip():
        issues.append("description is empty")
    if req.get("role") not in ROLES:
        issues.append(f"role must be one of {ROLES}")
    ac = req.get("acceptance_criteria")
    if not isinstance(ac, list) or not [x for x in ac if (x or "").strip()]:
        issues.append("acceptance_criteria must be a non-empty list")

    if order_kind == "feature":
        if req.get("bug_type") is not None:
            issues.append("feature order must have bug_type = null")
    elif order_kind == "bug":
        bt = req.get("bug_type")
        if bt not in BUG_TYPES:
            issues.append(f"bug order bug_type must be one of {BUG_TYPES}")
        elif bt == "crash":
            r = req.get("repro") or {}
            if r.get("repro_class") not in ("A", "B", "C"):
                issues.append("crash repro.repro_class must be A, B, or C")
            if not (r.get("target") or "").strip():
                issues.append("crash repro.target is empty")
            if not (r.get("backend") or "").strip():
                issues.append("crash repro.backend is empty")
        elif bt == "performance":
            p = req.get("perf") or {}
            gk = p.get("goal_kind")
            if gk not in PERF_GOAL_KINDS:
                issues.append(f"perf.goal_kind must be one of {PERF_GOAL_KINDS}")
            if not (p.get("target_metric") or "").strip():
                issues.append("perf.target_metric is empty")
            if gk == "improve_by" and p.get("improve_pct") in (None, ""):
                issues.append("perf.improve_pct required when goal_kind = improve_by")
            if gk == "meet_cap" and p.get("cap") in (None, ""):
                issues.append("perf.cap required when goal_kind = meet_cap")
        elif bt == "appearance":
            c = req.get("capture") or {}
            if not (c.get("expectation") or "").strip():
                issues.append("appearance capture.expectation is empty")
            if not (c.get("scene") or "").strip():
                issues.append("appearance capture.scene is empty")
            if not (isinstance(c.get("backends"), list) and c.get("backends")):
                issues.append("appearance capture.backends must be a non-empty list")

    return (len(issues) == 0, issues)


def render_md(req: dict) -> str:
    lines: list[str] = []
    lines.append(f"# {req.get('id', '(no id)')} — {req.get('title', '')}")
    lines.append("")
    lines.append(f"- **order_kind**: {req.get('order_kind')}")
    if req.get("order_kind") == "bug":
        lines.append(f"- **bug_type**: {req.get('bug_type')}")
    lines.append(f"- **role**: {req.get('role')}")
    lines.append(f"- **state**: {req.get('state')}")
    claim = req.get("claim") or {}
    owner = claim.get("owner_user")
    if owner:
        lines.append(
            f"- **claimed by**: {owner} ({claim.get('owner_role')}) at {claim.get('claimed_at')}"
        )
    else:
        lines.append("- **claimed by**: (unclaimed)")
    lines.append(f"- **created_by**: {req.get('created_by')} at {req.get('created_at')}")
    lines.append(f"- **updated_at**: {req.get('updated_at')}")
    lines.append("")
    lines.append("## Description")
    lines.append("")
    lines.append(req.get("description", "") or "")
    lines.append("")
    lines.append("## Acceptance criteria")
    lines.append("")
    for c in req.get("acceptance_criteria", []) or []:
        lines.append(f"- {c}")
    lines.append("")

    if "repro" in req:
        lines.append("## Repro (crash)")
        lines.append("")
        for k, v in (req.get("repro") or {}).items():
            lines.append(f"- **{k}**: {v}")
        lines.append("")
    if "perf" in req:
        lines.append("## Performance goal")
        lines.append("")
        for k, v in (req.get("perf") or {}).items():
            lines.append(f"- **{k}**: {v}")
        lines.append("")
    if "capture" in req:
        lines.append("## Capture (appearance)")
        lines.append("")
        for k, v in (req.get("capture") or {}).items():
            lines.append(f"- **{k}**: {v}")
        lines.append("")

    lines.append("## History")
    lines.append("")
    for h in req.get("history", []) or []:
        lines.append(
            f"- `{h.get('at')}` {h.get('state')} — {h.get('by_user')} "
            f"({h.get('by_role')}){': ' + h['note'] if h.get('note') else ''}"
        )
    lines.append("")
    lines.append("> Derived view. Source of truth is the sibling .json. Do not hand-edit.")
    lines.append("")
    return "\n".join(lines)


def _fail(reason: str) -> dict:
    return {"ok": False, "reason": reason, "requirement": None}


def _ok(req: dict, reason: str = "") -> dict:
    return {"ok": True, "reason": reason, "requirement": req}


def claim(req_id: str, role: str, user: str, session_id: str) -> dict:
    """reviewed -> claimed. Requires role match and not already claimed."""
    req = load(req_id)
    if req is None:
        return _fail(f"{req_id} does not exist.")
    if req.get("state") != "reviewed":
        return _fail(
            f"{req_id} is in state '{req.get('state')}', only 'reviewed' requirements can be claimed."
        )
    if req.get("role") != role:
        return _fail(
            f"{req_id} belongs to role {req.get('role')}, not {role}; you cannot claim it."
        )
    if (req.get("claim") or {}).get("owner_user"):
        return _fail(f"{req_id} is already claimed by {req['claim']['owner_user']}.")

    now = _now()
    req["claim"] = {
        "owner_user": user,
        "owner_role": role,
        "session_id": session_id,
        "claimed_at": now,
    }
    req["state"] = "claimed"
    req.setdefault("history", []).append(
        {"state": "claimed", "by_user": user, "by_role": role, "at": now, "note": "claimed"}
    )
    save(req)
    return _ok(req, f"{req_id} claimed by {user} ({role}).")


def transition(req_id: str, new_state: str, by_role: str, by_user: str, note: str = "") -> dict:
    """Enforce one state-machine edge with role/verifier guards."""
    req = load(req_id)
    if req is None:
        return _fail(f"{req_id} does not exist.")
    if new_state not in STATES:
        return _fail(f"Unknown target state '{new_state}'.")
    if new_state == "claimed":
        return _fail("Use /ash-claim to claim a requirement (it also records the claim).")

    cur = req.get("state")
    if new_state not in EDGES.get(cur, set()):
        return _fail(f"Illegal transition {cur} -> {new_state}.")

    claim_blk = req.get("claim") or {}

    if new_state == "awaiting-visual-approval":
        if req.get("bug_type") != "appearance":
            return _fail("Only appearance bug orders can enter awaiting-visual-approval.")

    if new_state == "verified":
        if by_role != "QA":
            return _fail("Only the QA role can verify a requirement.")
        if by_user and by_user == claim_blk.get("owner_user"):
            return _fail("Verifier must differ from implementer (by_user == owner_user).")
    else:
        # in-progress / done / reviewed advances are driven by the owning role.
        if by_role != req.get("role"):
            return _fail(
                f"Only the owning role ({req.get('role')}) can advance this requirement to '{new_state}'."
            )

    now = _now()
    req["state"] = new_state
    req.setdefault("history", []).append(
        {"state": new_state, "by_user": by_user, "by_role": by_role, "at": now, "note": note}
    )
    save(req)
    return _ok(req, f"{req_id}: {cur} -> {new_state}.")


def is_valid_for_write(req: dict | None, role: str) -> bool:
    """True when this requirement authorises a code write by `role`.

    Used by check_write: the requirement must exist, be in a write-OK state,
    and be claimed by the writing role.
    """
    if not req:
        return False
    if req.get("state") not in WRITE_OK_STATES:
        return False
    return (req.get("claim") or {}).get("owner_role") == role
