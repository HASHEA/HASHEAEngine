# SDD-2026-07-12-logic-input-consumption: 逻辑线程输入消费与空闲更新收敛

## Status

Done

## Context

`Application` 在 render 线程每帧采集 `InputState`，再把快照发布给可选 logic 线程。当前实现存在三项互相关联的问题：

- logic 循环可按 1ms 间隔多次运行，但 `_consume_logic_input_snapshot()` 在没有新快照时保留旧的 `pressed/released/scroll`，导致同一瞬态输入被重复观察；Sandbox 滚轮调速会因此指数重复放大。
- render 线程用赋值覆盖尚未消费的 pending 快照。logic 线程暂时落后时，较早帧的按键/鼠标边沿与滚轮增量可能丢失。
- `SandboxFreeCameraController` 在没有位移或旋转时仍写回相同 Transform，触发 Scene dirty/change/render-transform version；Sandbox 又把 logic idle sleep 配成 1ms，放大空闲 CPU、锁竞争与 presentation/version 检查开销。

这是 Base 输入状态、Application 跨线程传递与 Sandbox 行为共同构成的跨模块修复，风险级别为 S2。批准前不修改生产代码。

## Goals

- 同一批 `pressed/released/scroll` 瞬态数据至多被一次 `_on_logic_update()` 观察；`down` 与鼠标位置等持续状态在后续 logic tick 保持有效。
- logic 暂时落后时，pending 快照保留最新持续状态，并合并尚未消费的边沿与滚轮增量。
- Sandbox 空闲或仅滚轮调速时不写回未变化的相机 Transform，不推进 Scene transform version。
- Sandbox logic 循环的空闲 sleep 从 1ms 调整为 8ms，将无显式固定步长保证下的理论调度上限从约 1000Hz 收敛到约 125Hz，同时保持真实时间 delta 驱动的移动速度。
- 不改变双后端渲染结果、frame-dump 固定相机与 readiness 契约。

## Non-goals

- 不引入 condition variable、事件队列、固定步长模拟或新的线程模型；这类工作需要独立 S3 + ADR。
- 不保证同一按键在 logic 消费前发生多次往返时的完整事件顺序；现有 `InputState` 只能表达“本批次发生过 pressed/released”。
- 不修改 Graphics、RenderGraph、RHI、场景文件、Engine.ini、golden 或性能基线。
- 不把 Application render frame index 误作 logic 输入 generation；二者没有原子绑定关系。

## Current implementation

- Entry points:
  - `Application::_pump_platform_events()` 每个 render frame 调 `inputState.begin_frame()`、处理窗口事件并发布快照。
  - `Application::_logic_thread_main()` 独立循环消费快照并调用 `_on_logic_update()`。
  - `SandboxStandardScene::update_logic()` 驱动 `SandboxFreeCameraController::update()`。
- Modules: `Base/input`、`Function/Application`、`Sandbox/App`。
- Data flow: window events → render-thread `inputState` → mutex-protected `pendingLogicInputState` → logic-thread `logicInputState` → Sandbox free camera。
- Known constraints:
  - `InputState::begin_frame()` 只清瞬态字段，不清 `down` 或鼠标位置。
  - pending 与 active logic 状态是两份对象；发布与消费在同一 mutex 下串行。
  - Sandbox 的移动积分使用 steady-clock 实际 logic delta，并已把单次 delta clamp 到 0.1s。
  - Scene 的普通 Transform setter 无条件标记 component changed；调用方必须避免语义空写。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Base / Input | 提取公开的瞬态清理操作；增加“最新持续状态 + OR 边沿 + 累加滚轮”的快照合并操作，`begin_frame()` 复用瞬态清理 | `project/src/engine/Base/input/Input.h` |
| Function / Application | 发布时合并到 pending；消费后清 pending 瞬态；每次 `_on_logic_update()` 返回后清 active logic 瞬态 | `project/src/engine/Function/Application.cpp` |
| Sandbox | idle sleep 改为 8ms；移除未使用且容易误导的 `frame_index` 参数；相机只在位置或旋转实际变化时写 Transform | `project/src/sandbox/Sandbox.cpp`、`project/src/sandbox/App/SandboxApplication.cpp`、`SandboxStandardScene.*`、`SandboxFreeCameraController.cpp` |
| Tests | 覆盖快照合并/一次性消费语义、滚轮一次生效、空闲不推进 transform version；Tests 工程仅加入所需 Sandbox controller 源与 include | `project/src/tests/Base/input_state_tests.cpp`、`project/src/tests/Sandbox/sandbox_free_camera_tests.cpp`、`project/src/tests/premake5.lua` |
| Specs | 完成时回写稳定契约与验证结论 | `docs/specs/modules/base.md`、`application.md`、`sandbox.md` |

### API / contract changes

`InputState` 增加两个后端无关的值语义操作（最终命名在实现前以测试表达为准）：

1. `clear_transient_state()`：清 `keyPressed/keyReleased`、鼠标 pressed/released 与 scroll，保留 key/mouse down 和鼠标位置；`begin_frame()` 委托它。
2. `merge_frame_snapshot(const InputState& newer)`：
   - key/mouse down 与鼠标位置取 `newer` 的最新值；
   - pressed/released 对尚未消费批次做逐项 OR；
   - scroll 对尚未消费批次求和。

Application mailbox 契约：

- publish 不再覆盖 pending 瞬态，而是在 mutex 内合并；
- consume 在 mutex 内复制 pending 到 active 后，清 pending 瞬态并清 dirty 标记；
- logic update 后只清 active 瞬态，因此 held key 可继续驱动后续 tick，而边沿/滚轮不能重复触发；并发到达的新 publish 只进入 pending，不受 active 清理影响。

若一次消费前同一键既按下又释放，消费者可同时看到 pressed 与 released，down 表示最终状态；这是当前布尔快照模型能无损表达的上限。

### Backend impact

无 Vulkan/DX12 分支或接口改动。由于 Application/Sandbox 是双后端共同上层，必须以 Editor/Sandbox × Vulkan/DX12 readiness smoke 和 RenderGate 证明没有后端相关回归。

### Performance

- 合并一次快照仍为固定大小 O(512 keys + 8 mouse buttons)，与原完整赋值同阶；工作在短 mutex 临界区内，不分配内存。
- Sandbox idle sleep 1ms → 8ms，使理论最大 logic tick 数降低 87.5%；实际频率受 Windows 调度精度和 tick 工作量影响，不承诺精确 125Hz。
- 空闲相机不再产生 Transform change event/version，可避免无效 scene snapshot/presentation 工作。
- 移动仍按真实 delta 积分，预期单位时间速度不变；输入响应额外上界约 8ms。PerfGate 不更新或 bless 基线，FAIL 必须修复，WARN 必须解释。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 定向 doctest | merge、瞬态清理、held state、滚轮一次生效、空闲 transform version | `Tests.exe --test-case="*InputState*"` 与 `Tests.exe --test-case="*Sandbox free camera*"` |
| 全量单测 | 新旧 doctest + legacy self-tests | `RunTests.bat` |
| 架构门禁 | Base ← Function ← Sandbox 依赖方向 | `RunArchGate.bat` |
| 全新生成/构建 | tests premake、Engine、Editor、Sandbox | 删除隔离树 sln 后 `generate_vs2022.bat`，再 `build_editor.bat Debug`、`build_sandbox.bat Debug` |
| Application 生命周期 | Editor/Sandbox × Vulkan/DX12 readiness | `run.bat all Debug --smoke-test-seconds=120` |
| 渲染确定性 | Sandbox/particles 双后端 golden 与跨后端 diff | `RunRenderGate.bat` |
| 性能 | logic cadence 与空写收敛不引入回归 | `RunPerfGate.bat -Profile Standard` |
| 计划校验 | dirty paths 与验证矩阵一致 | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan` |
| 日志 | runtime/validation/leak | 检查上述运行日志无 Error、validation/debug-layer、VUID 或 shutdown leak |

验证全部在本隔离 worktree 执行，不读取、恢复、暂存或覆盖共享 main 中用户修改的 `product/assets/scenes/Sandbox.scene.json` 与 `product/config/editor/imgui.ini`。禁止更新 perf baseline 或 render golden。

## Task breakdown

1. RED：新增 InputState 合并/清理测试，证明旧实现缺 API 且无法满足累计语义。
2. GREEN：实现 InputState 值语义操作，仅跑定向与全量单测。
3. RED：新增 Sandbox free camera 的滚轮一次生效与空闲 transform-version 测试。
4. GREEN：接入 Application mailbox 一次性消费；Sandbox 去空写、移除伪 frame 参数并调整 idle sleep。
5. 回写 base/application/sandbox 长期 spec；执行 fresh generate/build、全矩阵 smoke、RenderGate、PerfGate、ArchGate 与 AIDevDoctor。
6. 独立复审 cached diff、验证输出与剩余 dirty 边界；仅在全部门禁通过后提交。

## Risks

| Risk | Mitigation |
| --- | --- |
| 合并规则使同批次 pressed/released 同时为 true | 明确契约并补测试；down 始终表示最终状态，不虚构事件顺序 |
| active 清理时机过早导致首次 update 丢边沿 | 仅在 `_on_logic_update()` 返回后清理；startup 不视为输入消费 |
| pending 清理误删并发新事件 | publish/consume 只在同一 mutex 下改 pending；active 清理不接触 pending |
| 8ms sleep 改变相机手感 | 移动按真实 delta；双后端运行检查，PerfGate 与实际相机操作验证；如有证据再调整，不引入新配置 |
| 去空写掩盖应提交的旋转 | 以明确的 `transform_changed` 标志覆盖非零 mouse delta 与移动；滚轮只改变速度，本就不应写 Transform |
| RenderGate 画面变化 | frame dump 无输入且固定相机，预期完全不变；任何 FAIL 按 bug 处理，不 bless |

## Open questions

- 无。建议按上述 8ms Sandbox cadence 与批次合并语义实施；后续若需要有序输入事件或固定步长模拟，另立 S3。

## Verification results

- TDD RED：InputState 新 API 缺失产生预期 C2039；旧自由相机空闲 update 把 transform version 从 6 推进到 8；旧 Application mailbox 丢失首帧 pressed/scroll，且重复两个 logic tick 暴露同一 pressed；startup 期间第二次 publish 的旧路径稳定丢失首批 pressed 与 1.0 scroll。
- 定向 GREEN：InputState 2/2、23/23；Application mailbox/logic 3/3、17/17；Sandbox free camera 2/2、13/13。
- `RunTests.bat`：46/46 cases、522/522 assertions。
- fresh 删除/生成 solution 后，Editor Debug 与 Sandbox Debug 构建通过；审查修正后两目标再次构建通过。
- `RunArchGate.bat`：PASS，35 条既有 legacy warning，无新增越界。
- `run.bat all Debug --smoke-test-seconds=120`：Editor/Sandbox × Vulkan/DX12 全部 0 退出；审查修正后完整重跑通过。
- `RunRenderGate.bat`：PASS；报告 `20260713-102818-928-4884-240b08a6`。Sandbox Vulkan 0.996278、DX12 0.996177、cross 0.999747；Particles 三项均 1.0。
- `RunPerfGate.bat -Profile Standard`：PASS，无 warning/failure；报告 `20260713-102918`。四组合 baseline 均为 MISSING，因此仅证明绝对门槛，不声称相对基线改善。
- `AIDevDoctor.ps1 -Mode ValidatePlan`：PASS；最终计划报告 `20260713-022711`。
- 最终门禁日志未命中 VUID、validation error、device removed 或真实 leak；Vulkan 均报告无 live VMA allocation。
- 独立代码审查首轮发现的 startup 批次丢失 Important 与纯滚轮测试 Minor 均经 RED→GREEN 修正；复核结论 Ready。
