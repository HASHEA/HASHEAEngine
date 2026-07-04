"""PreToolUse 护栏（AGENTS.md SDD rules / High-risk paths 的最薄机械化）。

规则：
- deny: 直接编辑基线文件（render golden / perf baseline），必须走 bless 流程
- ask:  编辑 S2 高风险路径（Graphics RHI / RenderGraph），由用户确认（对应"S2 需批准 SDD"）
其余放行（无输出，exit 0）。
"""

import json
import sys


DENY_PATTERNS = (
    "tools/render/goldens/",
    "tools/perf/perf_gate_baselines.json",
)

ASK_PATTERNS = (
    "project/src/engine/graphics/",
    "/function/render/rendergraph",
)


def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        return
    path = (data.get("tool_input") or {}).get("file_path") or ""
    normalized = path.replace("\\", "/").lower()
    if not normalized:
        return

    decision = None
    reason = ""
    if any(pattern in normalized for pattern in DENY_PATTERNS):
        decision = "deny"
        reason = (
            "基线文件禁止直接编辑：golden 经 RunRenderGate.bat -BlessGolden、"
            "perf 基线经 RunPerfGate.bat -BlessBaseline 更新，且须先经用户确认"
            "（AGENTS.md High-risk paths）。"
        )
    elif any(pattern in normalized for pattern in ASK_PATTERNS):
        decision = "ask"
        reason = (
            "S2 高风险路径（RHI / RenderGraph）：需要已获用户批准的 SDD 才能动代码"
            "（AGENTS.md SDD rules）。确认放行即视为本次改动在已批准范围内。"
        )

    if decision:
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": decision,
                "permissionDecisionReason": reason,
            }
        }))


if __name__ == "__main__":
    main()
