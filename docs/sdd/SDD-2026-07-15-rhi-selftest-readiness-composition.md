# Mini SDD: RHI self-test 与 readiness 生命周期组合修复

## Status

Done

## Goal

修复 `--rhi-selftest-indirect` 与 `--smoke-test-seconds` 同时启用时，RenderGraph indirect self-test 成功后过早请求退出、导致仍处于 `Running` 的 readiness automation 被误判失败的问题。组合模式必须先完成 raw RHI、constant-buffer（若请求）和 RenderGraph indirect self-test，再继续普通帧直到 readiness 成功；任一 self-test 失败仍立即 fail-closed 并返回非零。

## Non-goals

- 不修改 Vulkan/DX12 self-test、RenderGraph、RHI 或 shader 实现。
- 不改变单独运行 `--rhi-selftest-indirect --run-for-frames=1` 的 one-shot 成功退出语义。
- 不降低 readiness 条件、超时或 CI 覆盖，也不修改性能/渲染基线。

## Files

- `project/src/engine/Function/Application.cpp`
- `project/src/tests/Function/application_automation_tests.cpp`（仅在现有 seam 能直接表达组合策略时；否则以既有 CI 命令作为持久集成回归）
- `.github/workflows/ci.yml`（只补充契约注释；现有组合命令保持为回归入口）
- `docs/specs/modules/application.md`
- `docs/sdd/SDD-2026-07-15-rhi-selftest-readiness-composition.md`

## Approach

1. 以 CI 的既有 Release DX12 命令复现 RED：三个 GPU self-test 均 PASS，但 readiness 尚未得出最终结果便退出并返回 1。
2. RenderGraph self-test 失败时维持当前行为：设置 runtime failure 并立即请求退出。
3. RenderGraph self-test 成功时：若未启用 readiness automation，保持 one-shot 请求退出；若启用了 readiness automation，不立即退出，从下一帧进入现有普通 render/present/readiness 路径，最终退出权仍由 automation controller 持有。
4. 不为单一调用点新增公共抽象；优先用现有 `automationEnabled` 状态表达策略。CI 保留组合参数，从而持续覆盖该生命周期合同。

## Verification

- RED/GREEN：`run.bat sandbox dx12 Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer`。
- 对称后端：同命令改为 `vulkan`，两后端都要求 raw indirect、constant buffer、RenderGraph indirect 与 readiness PASS，进程 exit 0、Sandbox `clean_exit=yes`，fresh 日志无 error/validation/device-lost/fatal。
- `RunTests.bat Debug --test-case=*indirect self-test*`（若新增 focused doctest）。
- `RunTests.bat Debug`、`RunArchGate.bat`、`scripts/AIDevDoctor.ps1 -Mode ValidatePlan`、`git diff --check`。
- 作为 Application 生命周期/渲染启动修复，按 `docs/VERIFY.md` 重跑 `RunRenderGate.bat`；不 bless。

完成证据（2026-07-15）：

- RED：原始 Release DX12 组合命令中 raw indirect、constant buffer、RenderGraph indirect 均 PASS，但进程因 `readiness automation terminated before reaching a final outcome` 返回 1，Sandbox `clean_exit=no`。
- GREEN：相同 DX12 组合命令三项 self-test 均 PASS，readiness 在后续普通帧成功，exit 0、`clean_exit=yes`；Vulkan 对称组合同样 exit 0、`clean_exit=yes`。
- one-shot：Release DX12 `--rhi-selftest-indirect --rhi-selftest-constant-buffer --run-for-frames=1` exit 0，无需 readiness 成功。
- CPU/架构：focused 与 full `RunTests.bat Debug` 均 176/176 cases、2510/2510 assertions；`RunArchGate.bat` PASS（35 条既有 legacy WARN）。
- readiness/渲染：`run.bat all Debug --smoke-test-seconds=120` 四组合 exit 0；`RunRenderGate.bat` PASS，报告 `20260715-182519-783-24392-0b404b76`，未 bless。
- 工具/边界：AIDevDoctor ValidatePlan、`git diff --check`、配置哈希恢复与有效进程根审计均 PASS。

## Risk / rollback

风险是 self-test 成功后继续运行普通场景时遗留瞬态帧状态。现有 self-test 已完整执行 begin/end/present/capture 并释放自有资源；GREEN 必须通过双后端组合命令与 RenderGate 才能提交。回滚时恢复 `Application.cpp` 的 one-shot 无条件退出，并把 CI 拆成独立 self-test/readiness 两个进程，禁止仅删除 readiness 覆盖。
