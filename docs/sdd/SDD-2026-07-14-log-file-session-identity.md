# Mini SDD: 日志文件会话身份与证据保留

## Status

Approved

## Goal

- 每次 `LogService::init` 都为当前进程会话创建一组唯一、可配对的 Engine/Application 日志文件。
- 消除同一分钟连续启动或同机并行启动时的日志截断覆盖，保留 smoke、validation、PerfGate 与崩溃诊断的完整证据。
- 保持现有 logger 名称、日志宏、消息格式、flush 语义和 `product/logs/*.logfile` 消费约定兼容。

## Non-goals

- 不引入日志轮转、保留期限、压缩或清理策略。
- 不修改 PerfGate、RenderGate、readiness 或 validation 的判定逻辑。
- 不把多个进程写入同一文件，也不改成 append 以混合不同运行会话。
- 不改变 `LogService` 公共接口或新增仅供测试使用的导出符号。

## Files

- `project/src/engine/Base/hlog.cpp`
- `project/src/tests/Base/hlog_tests.cpp`
- `docs/specs/modules/base.md`
- `docs/VERIFY.md`
- `docs/sdd/SDD-2026-07-14-log-file-session-identity.md`
- `docs/plans/2026-07-14-log-file-session-identity.md`

## Approach

1. `LogService::init` 只捕获一次 wall-clock 时间，并生成当前初始化会话的稳定 ID；Engine 与 Application 两个文件共享该 ID，避免分钟边界造成配对错位。
2. 会话 ID 使用可读的本地日期时间、六位微秒、进程 ID 和进程内单调序号：`YYYYMMDD_HHMMSS_ffffff_p<PID>_s<SEQ>`。微秒便于人工排序，PID 隔离并行进程，单调序号保证同一进程内快速 shutdown/init 不碰撞。
3. 保留 spdlog 的 truncate 打开模式。由于每个会话路径唯一，truncate 只初始化新文件，不会把不同运行会话交织到同一文件，也不会覆盖旧会话。
4. 实现保持在 `hlog.cpp` 内部，不扩张 Base 公共 API。测试通过现有 logger sink 观察实际文件路径与内容，连续执行三次初始化，固定“路径全部唯一、Engine/App 后缀成对、前序内容未被后续初始化截断”的外部行为。
5. 长期规格声明日志文件会话身份和保留不变式；验证文档声明自动化应按每次运行产生的唯一日志取证，不再依赖分钟粒度的增量猜测。

## Verification

- RED：构建并运行新增的定向 `LogService` doctest，确认当前分钟粒度实现出现重复路径并截断前序标记。
- GREEN：运行定向 `LogService` doctest。
- `RunTests.bat Debug`
- `RunArchGate.bat`
- `build_editor.bat Debug`
- `build_sandbox.bat Debug`
- `run.bat all Debug --smoke-test-seconds=120`
- smoke 前后审计 `product/logs`：四个进程产生四组唯一且成对的日志，四份 Engine 日志分别保留 readiness/clean-exit 证据，无 validation/device-lost/access-violation/fatal。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `git diff --check`，且暂存集合不含 LFS 可执行文件噪声。

日志命名不触及渲染结果、RHI、内存分配或线程调度，因此不运行 RenderGate/PerfGate。

## Risk / rollback

- 文件数量由“每分钟最多一对”变为“每次初始化一对”，会更快暴露日志保留/清理策略缺口；本包不偷偷增加清理策略，避免误删用户诊断证据。
- 下游若错误依赖旧的分钟文件名会失效；仓库内消费方均以 `*.logfile` 和文件时间扫描，验证阶段会复核相关脚本。
- 回滚可整体恢复 `hlog.cpp`、测试与文档；不涉及配置、资产、渲染基线或持久化格式。
