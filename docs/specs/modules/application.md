---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Application / EntryPoint / UIContext

## 职责与边界

`Function/Application.*` 是引擎运行时壳：负责子系统初始化顺序、主循环（render 线程）与可选 logic 线程、退出与资源回收；`EntryPoint.h` 提供 `main`：工作目录重置、命令行解析并注入 Application。`Function/Gui/UIContext.*` 是 Editor/游戏与引擎之间的 UI 边界门面（封装 ImGui）。本模块不做具体渲染（委托 SceneRenderer/RenderDevice）、不含编辑器业务（Editor 通过继承 `Application` 的虚函数钩子接入）。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `project/src/engine/Function/Application.h/.cpp` | `Application` 类、`EngineInitConfig`、帧循环、frame dump、engine overlay |
| `project/src/engine/EntryPoint.h` | `main`：工作目录重置、命令行契约、`create_application()/destroy_application()`（由应用侧实现） |
| `project/src/engine/Function/Gui/UIContext.h/.cpp` | `UIContext` UI 门面（Pimpl，内部 `ImGuiLayer`） |
| `project/src/engine/Function/Gui/UINodeEditor.h/.cpp` | 节点画布门面（封装 `imgui-node-editor`，由 Engine 持有第三方上下文） |
| `project/src/engine/Function/Gui/UICommon.h`、`ImGuiLayer.*` | UI 类型（`UIVec2/UIWindowFlags/...`）与 ImGui 后端桥接 |

## 公共接口

### 生命周期

- `Application(EngineInitConfig)` → `initialize()` → `start()`（阻塞跑主循环并返回运行成功/失败）→ 析构/`_shutdown_runtime()`；EntryPoint 将失败映射为非零进程退出码。
- `initialize` 顺序：LogService → MemoryService → threading（当前线程注册为 Render 角色）→ 解析 RHI 配置（Engine.ini + `initConfig.backend` 覆盖，路径默认 `product/config/Engine.ini`）并应用本进程 PerfGate validation/vsync override → 发布同一份渲染 feature 配置 → `Window::create` → `GraphicsContext::create`（按需启用 GPU timing）→ `Swapchain::create` → RenderDevice/Renderer → `UIContext`。窗口 extent 在创建 Window 前合并进 `EngineInitConfig`；用于报告的 resolved extent 必须取初始化后的 Swapchain 实际尺寸，而不是 GLFW 请求尺寸。`PerfGateController` 只在上述初始化成功末尾配置，初始化失败不得写出误导性的 0-sample 正常报告。
- 主循环每帧：平台事件泵 → tick → `pump_render_commands` → 渲染 + present → readiness 观察 → 帧计数。`--smoke-test-seconds` 在第一个完整 ready+present-completed 帧提前成功，秒数只作硬失败上限；PerfGate 采样窗口与显式 `--run-for-*` watchdog 仍可请求正常退出。默认渲染阶段固定顺序：begin_frame → `_on_render_debug` → scene presentation submit → `_on_gui` → end_frame。acquire 与 present 结果从 RHI 以同一三态传播：Completed 才继续录制或满足 readiness，Retryable 跳过本帧并等下一帧，Failed 立即终止并返回非零；acquire Retryable 不消费已 arm 的 capture，DXGI OCCLUDED 是成功 present 状态。
- PerfGate GPU 归档使用 `Warmup → Sampling → Draining → Complete` 状态机：仅记录 sampling 窗口内得到精确 submit acknowledgement 的 renderer frame ID，延迟完成 sample 只按自身 frame ID 归属；telemetry 关闭时不进入 drain，开启时 pending 全部完成便提前结束，否则最多 drain 配置时长且不等待 device idle。schema v2 保留 schema v1 的 CPU 顶层字段，并追加 runtime、backend metadata、总 `valid/submitted` coverage 与逐 metric `present/submitted` coverage；backend 判为 invalid 的 sample 不进入 percentile，缺失 metric 不把整帧改判 invalid，也不序列化伪造的 `0` duration。profile 阈值与必需 metric 判定由 `RunPerfGate` 工具层执行。
- 可选 logic 线程：`EngineThreadingConfig.enable_logic_thread` 开启，输入经快照（`_publish/_consume_logic_input_snapshot`）跨线程传递；尚未消费的 render-frame 快照按“最新 down/位置、OR pressed/released、累加 scroll”合并，每批瞬态只允许一次 `_on_logic_update()` 观察，持续状态保留到新快照；logic 线程异常会被捕获并终止主循环。
- 应用侧扩展点：`_on_startup/_on_update/_on_gui/_on_render/_on_logic_*/_on_shutdown` 等虚函数。
- 静态访问器：`Application::get()`、`get_window/get_graphics_context/get_swapchain/get_render_device/get_renderer/get_ui_context/get_input/get_rhi_backend` 等。

### 命令行契约（EntryPoint）

| 选项 | 行为 |
| --- | --- |
| `--smoke-test-seconds[=S]` | readiness smoke；等待应用、资源、当前帧全部 scene packet 与非致命 present completion 后成功退出，S 是覆盖初始化、运行与 teardown 的进程级 wall-clock 硬失败上限（裸选项 25 秒）；环境变量 `ASH_ENGINE_SMOKE_TEST_SECONDS` 等效 |
| `--run-for-seconds=S` / `--run-for-frames=N` | 显式固定时长/帧数运行，供 PerfGate watchdog、soak 或调试使用；到达上限是正常退出，不代表 readiness 成功 |
| `--smoke-test[=N]` | deprecated 的 `--run-for-frames=N` 别名（裸选项 N=3）；`ASH_ENGINE_SMOKE_TEST_FRAMES` 同样仅是旧 fixed-run 别名并打印告警 |
| `--rhi=<vulkan\|vk\|directx12\|dx12\|d3d12>` | 后端覆盖，经 `set_backend_override` 在 `initialize()` 之前注入（顺序是硬约束）；非法值直接退出码 1 |
| `--window-width=W --window-height=H` | 本进程窗口 extent 覆盖；两项必须成对出现且各自位于 `1..65535`，缺项、0、负数或越界都在 `create_application()` 前失败；是否启用 PerfGate 不影响该覆盖 |
| `--dump-frame=<png>` | 隐式启用 readiness capture；通常配 `--smoke-test-seconds=S`，未给 S 时默认 120 秒。只有通过 epoch 双重复核的 capture 才原子发布 PNG，超时/失败会删除旧目标并非零退出 |
| `--scene=<json>` | 场景路径覆盖，应用层经 `get_scene_path_override()` 消费 |
| `--engine-self-test` | 只跑 Base 自测后退出 |
| `--rhi-selftest-indirect` | opt-in 的首帧前双后端 indirect draw/dispatch GPU 诊断；失败设置既有 runtime-failure 状态并使 readiness smoke 非零退出 |
| `--rhi-selftest-constant-buffer` | opt-in 的首帧前双后端 constant-buffer 可见性 GPU 诊断；setup、命令录制、校验或读回任一失败均设置既有 runtime-failure 状态并使 readiness smoke 非零退出。与 `--rhi-selftest-indirect` 相互独立；两项同时请求时都会执行，任一失败均在 `_on_startup()` 与首帧前 fail-fast |
| `--bake-ashibl <src.hdr> <out.ashibl> [...]` | IBL 离线烘焙子命令，跑完即退出 |
| PerfGate 系列 | `--perf-gate` 与 `--perf-gate-gpu-timing/validation/vsync=on\|off`、`--perf-gate-{warmup,sample,drain}-seconds=S` 在创建应用前严格解析；识别到但缺少 `=` 值的选项直接失败，validation/vsync 未给时继承运行时配置，时长必须有限且大于 0。合法配置无条件在 initialize 前存为 pending，validation/vsync 显式值不因 `--perf-gate` 缺失而丢失；GPU timing 未显式请求时保持 off，仅在同时给出 `--perf-gate` 与 `--perf-gate-gpu-timing=on` 时向 RHI 请求启用。resolved validation 记录编译后端的实际能力（Release 两后端均为 false），而非仅记录请求值。所有覆盖只作用于当前进程，不写 `Engine.ini` |

- 工作目录：`main` 先 `init_dir()`——从当前目录向上（≤16 层）找同时含 `AshEngine.sln`、`project/`、`product/` 的仓库根并 `fs::current_path` 切过去；全部相对路径以仓库根为基准。

### Frame dump 与 engine overlay

- readiness 由 `ApplicationAutomationController` 统一判定：派生应用 Ready、资源无 pending/failed、命令队列为空、当前 Application frame 的全部预期 scene packet 成功、scene 提交 epoch 等于最新资源 epoch，且 acquire/begin/end 成功、present Completed。Retryable acquire/present 保持 Pending；Sandbox 与 Editor 分别提供启动/场景及 bootstrap/UI/资产库 readiness hook。
- dump 使用两阶段握手：第一个 ready frame 清空可能混有 fallback 资源画面的 AO/TAA/体积光 temporal history，并 arm 资源 epoch；下一帧开始前 epoch 未变才请求 capture，present 后再验证同一 epoch 和全部 scene packet。变化时读回并丢弃、重新等待；超时绝不写不完整图片。粒子等动态内容还能提供 capture-ready 语义信号，当前按 `spawn_rate × (lifetime + variance)` 推导稳定窗口，不使用固定预热帧。
- dump 模式的可见帧在提交前深拷贝，并使用渲染侧连续 frame index 与固定 `delta_seconds=1/60`；它与固定相机、隐藏 overlay、禁 TAA jitter 一起构成确定性契约。
- 普通模式使用当前 Application render frame index，并以每个新 frame 实际进入的 scene render 调用作为模拟时钟。空 packet、输出/材质准备失败而未进入 render、以及重复同帧 submit 不推进时钟；一旦进入 render，即使该调用随后失败也推进一次，避免非事务式渲染失败后下一帧重复积分。
- 抓帧经 `RenderDevice::request_back_buffer_capture / fetch_back_buffer_capture` 回读；fetch 只把 wall-clock deadline 的剩余时间传给双后端 current-frame completion wait，不调用无上限的 device idle。通过 post-present readiness 后先用 stb 写临时 PNG，再 rename 到最终路径。deadline 覆盖 GPU readback、编码和发布，越界输出会删除并失败退出。EntryPoint 同时持有只针对 readiness/dump 的进程 watchdog；若 GPU/driver hang 使 graceful teardown 也无法返回，watchdog 到 deadline 后以失败码直接终止进程，禁止继续做无界 GPU 析构。
- `draw_engine_overlay()` 绘制帧统计浮层（`EngineFrameStatsOverlay` 窗口）；frame dump 模式（`frameDumpPath` 非空）下整体隐藏，保证 dump 确定性。

### UIContext（Editor 与 Engine 的 UI 边界）

- 生命周期钩子（`init/shutdown/begin_frame/render/handle_window_event`）由引擎持有；Editor 只使用 `UIContext` 绘制/查询 API，包括支持完整 uint32 范围的 `input_uint`、窗口、dock、控件、draw list、剪贴板、按键修饰、`register_render_target/register_texture_view` + `image/draw_surface_fill_available` 等。
- 节点画布通过 `UINodeEditor` 独立门面暴露；其实现与第三方 `imgui-node-editor` 均编译在 `Engine.dll` 内，Editor 只提交节点、pin、link 与交互查询，不 include `imgui_node_editor.h` 或调用 `ax::NodeEditor`。
- 主题/字体/多视口能力由 `EngineInitConfig` 的 `ui*` 字段透传。
- 准入标准：只收后端无关、立即模式、编辑器之外也可复用的能力；编辑器工作区编排、默认 dock 布局、面板注册/持久化、Inspector 语义一律放上层编辑器门面。底层 dock/viewport 原语（`dock_builder_*` 等）可进，编辑器具体布局策略不可进。
- 纹理/surface 约定：通用预览传 `RenderTarget`；场景视口传 `ScenePresentationSubsystem` 的 `UISurfaceHandle`；Window 输出无有效 surface（交换链不可被 UI 采样）。`UITextureHandle` 是瞬态后端数据，禁止跨帧缓存；描述符注册由引擎持有。
- CPU 像素上传：`create_ui_texture_rgba8(pixels, w, h, debug_name)` → `shared_ptr<UITexture>`（RGBA8 sRGB、单 mip、SRV-only 的不透明所有权载体，同时持有 texture+view——`TextureView` 对父纹理仅 weak 引用），配套 `register_ui_texture/unregister_ui_texture`。Editor 图标等像素数据一律走此门面创建 GPU 纹理（SDD-2026-07-08-editor-icon-ui-texture-facade），禁止直连 `GraphicsContext`。
- Vulkan 动态渲染路径下 ImGui 初始化必须显式填 `UseDynamicRendering` 与颜色附件格式，保持与引擎渲染路径一致。

## 约束与不变式

- backend、window extent、PerfGate、scene 与两个 RHI self-test 请求必须在 `initialize()` 之前注入；readiness/run watchdog 与 frame dump 控制在成功初始化之后注入。EntryPoint 必须在 `create_application()` 前拒绝非法 automation、extent、PerfGate 或 RHI 参数。
- Editor 及其他上层 UI 代码禁止绕过 `UIContext` 直接调用 ImGui 或 Graphics；需要新能力时给 `UIContext` 加封装接口。
- `Application` 是全局单例（`Application::app`）；主循环线程即 render 线程，渲染命令只能在该线程 pump。
- readiness automation 未成功前的窗口关闭、logic/render/scene/资产/capture 失败或超时均返回非零；固定 `--run-for-*` 不得冒充 smoke 成功。
- 抓帧确定性约定（资源 epoch、当前帧全 packet、动态 capture-ready、固定渲染步长、固定相机、隐藏 overlay、禁 TAA jitter）见 `docs/VERIFY.md`。

## 验证

对齐 `docs/VERIFY.md`「Scene / Asset / Application 生命周期」行：

- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke，ready 后提前退出），Editor 打开默认场景操作一遍。
- 改动命令行契约或 frame dump 路径时追加 `RunRenderGate.bat`（其依赖 `--rhi/--smoke-test-seconds/--dump-frame`）；改 PerfGate 注入时跑 `scripts/TestRunPerfGate.ps1` 与 `RunPerfGate.bat -Profile Standard`。
- UIContext/UI 改动：`run.bat editor` 手动过一遍改动路径。

## 历史

- [SDD-2026-07-07-render-gate 渲染验证安全网（RenderGate）](../../sdd/SDD-2026-07-07-render-gate.md)：新增 `--rhi/--dump-frame/--scene` 命令行与抓帧模式 overlay 隐藏。
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：以 readiness + asset epoch + 当前帧提交快照替代固定帧成功条件。
- [SDD-2026-07-12-logic-input-consumption](../../sdd/SDD-2026-07-12-logic-input-consumption.md)：修正 logic mailbox 的瞬态重复/覆盖丢失，并定义消费批次边界。
- [SDD-2026-07-13-gpu-performance-observability](../../sdd/SDD-2026-07-13-gpu-performance-observability.md)：新增成对 extent 与 PerfGate tri-state 启动覆盖，并把所有影响 Window/RHI 的配置前移到 initialize 前。
