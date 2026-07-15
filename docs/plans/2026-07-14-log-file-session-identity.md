# 日志文件会话身份实施计划

> 对应 Mini SDD：`docs/sdd/SDD-2026-07-14-log-file-session-identity.md`

## 目标

以最小 Base 层改动消除快速连续启动造成的日志覆盖，并让每次运行的 Engine/Application 日志能够可靠配对和归因。

## Task 1：锁定回归行为

- 新增 `project/src/tests/Base/hlog_tests.cpp`。
- 从 `LogService` 现有 file sink 读取实际路径，不新增测试专用生产接口。
- 连续三次写入标记、shutdown、init；断言每组路径唯一、Engine/App session 后缀一致、已关闭会话的标记仍存在。
- 运行定向用例并保存 RED 失败证据。

## Task 2：实现唯一 session ID

- 在 `hlog.cpp` 内增加进程内原子序号。
- 单次捕获时间，组合微秒、PID 与序号生成 session ID。
- 用同一 session ID 构造 Engine/Application 文件路径，保持现有 sink 模式、pattern、logger 名称和 flush 行为。
- 运行定向用例至 GREEN。

## Task 3：规格回写与 CPU 验证

- 更新 `docs/specs/modules/base.md` 和 `docs/VERIFY.md`。
- 运行全量 `RunTests.bat Debug`、`RunArchGate.bat`、Editor/Sandbox Debug build、AIDevDoctor 与 `git diff --check`。
- 审查生产 diff、测试强度、异常路径和文件边界。

## Task 4：运行证据验证

- 与其他会话协调 GPU 独占窗口。
- 记录 smoke 前日志集合与 `Engine.ini` 哈希。
- 运行 `run.bat all Debug --smoke-test-seconds=120`。
- 确认四次进程运行各自产生唯一 Engine/Application 日志对，四组证据均保留，fresh 日志无致命诊断，配置哈希精确恢复，相关进程全部退出。

## Task 5：交付

- 把 SDD 状态改为 Done。
- 仅暂存 SDD 允许路径，排除 LFS 噪声。
- 提交、推送 `codex/remediation-log-session-identity` 并创建 ready PR。
