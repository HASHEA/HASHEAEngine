# AshAgent 会话状态文件 Schema

会话状态文件记录「某个具体 AI 会话当下的运行时身份」:入口模式、激活角色、当前绑定的需求。它是**本地、不入库的运行时状态**(不是共享资产)。

## 位置

```
Intermediate/ashagent/session-<session_id>.json
```

- `<session_id>` 由 harness 提供(Claude Code 的 hook payload 带 `session_id`)。
- `Intermediate/` 已被 `.gitignore` 忽略,天然本地化。
- 一会话一文件,互不干扰;并发会话各读各的。

## 字段

| 字段 | 类型 | 含义 |
|---|---|---|
| `schema_version` | int | 当前为 `1` |
| `mode` | string | 入口模式:`AshAgent`(可写,须绑需求)/ `ReadOnly`(零写权) |
| `role` | string | 激活角色:`EngineDev` / `EditorDev` / `GameDev` / `QA`;ReadOnly 下可为 `null` |
| `active_requirement` | string \| null | 当前 claim 的需求 ID;AshAgent 模式下写代码必须非空 |
| `user` | string | 真人 / git 账号标识(追责主体) |
| `updated_at` | string | ISO 时间戳,最后一次状态变更 |

## 示例

```json
{
  "schema_version": 1,
  "mode": "AshAgent",
  "role": "EngineDev",
  "active_requirement": "REQ-2026-0001",
  "user": "huyizhou",
  "updated_at": "2026-06-22T17:00:00+08:00"
}
```

## 状态来源

- 由入口 skill(`/ash-start`,后续里程碑实现)交互式生成。
- M1 阶段先手工写入用于验证。
- SessionStart hook 读它注入上下文;PreToolUse hook 读它做权限判定。
