---
owner: huyizhou
last_reviewed: 2026-07-04
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
| `project/src/engine/Function/Gui/UICommon.h`、`ImGuiLayer.*` | UI 类型（`UIVec2/UIWindowFlags/...`）与 ImGui 后端桥接 |

## 公共接口

### 生命周期

- `Application(EngineInitConfig)` → `initialize()` → `start()`（阻塞跑主循环）→ 析构/`_shutdown_runtime()`。
- `initialize` 顺序：LogService → MemoryService → threading（当前线程注册为 Render 角色）→ 解析 RHI 配置（Engine.ini + `initConfig.backend` 覆盖，路径默认 `product/config/Engine.ini`）→ 渲染 feature/debug view/环境光配置 → `Window::create` → `GraphicsContext::create` → `Swapchain::create` → RenderDevice/Renderer → `UIContext`。
- 主循环每帧：平台事件泵 → tick → `pump_render_commands` → 渲染 + present → 帧计数；`--smoke-test`/`--smoke-test-seconds` 达上限即 `request_exit()`；PerfGate 采样窗口结束也会请求退出。默认渲染阶段固定顺序：begin_frame → `_on_render_debug` → scene presentation submit → `_on_gui` → end_frame。
- 可选 logic 线程：`EngineThreadingConfig.enable_logic_thread` 开启，输入经快照（`_publish/_consume_logic_input_snapshot`）跨线程传递；logic 线程异常会被捕获并终止主循环。
- 应用侧扩展点：`_on_startup/_on_update/_on_gui/_on_render/_on_logic_*/_on_shutdown` 等虚函数。
- 静态访问器：`Application::get()`、`get_window/get_graphics_context/get_swapchain/get_render_device/get_renderer/get_ui_context/get_input/get_rhi_backend` 等。

### 命令行契约（EntryPoint）

| 选项 | 行为 |
| --- | --- |
| `--smoke-test[=N]` | 跑 N 帧后退出，缺省 N=3；环境变量 `ASH_ENGINE_SMOKE_TEST_FRAMES` 等效 |
| `--smoke-test-seconds[=S]` | 跑 S 秒后退出，缺省 25；环境变量 `ASH_ENGINE_SMOKE_TEST_SECONDS` 等效 |
| `--rhi=<vulkan\|vk\|directx12\|dx12\|d3d12>` | 后端覆盖，经 `set_backend_override` 在 `initialize()` 之前注入（顺序是硬约束）；非法值直接退出码 1 |
| `--dump-frame=<png>` | backbuffer 抓帧落 PNG（RenderGate 用）；必须与 `--smoke-test=N` 同用，否则告警并跳过。抓帧时机由渲染资产流送 quiesce 信号驱动（连续 32 帧无 pending 即抓帧并退出）；N 仅为超时保底，超时抓帧会告警 |
| `--scene=<json>` | 场景路径覆盖，应用层经 `get_scene_path_override()` 消费 |
| `--engine-self-test` | 只跑 Base 自测后退出 |
| `--bake-ashibl <src.hdr> <out.ashibl> [...]` | IBL 离线烘焙子命令，跑完即退出 |
| PerfGate 系列 | `parse_perf_gate_config` 解析后 `configure_perf_gate` |

- 工作目录：`main` 先 `init_dir()`——从当前目录向上（≤16 层）找同时含 `AshEngine.sln`、`project/`、`product/` 的仓库根并 `fs::current_path` 切过去；全部相对路径以仓库根为基准。

### Frame dump 与 engine overlay

- 抓帧时机（SDD-2026-07-07-render-gate-streaming-signal）：dump 模式下每个渲染帧查询
  `RenderAssetManager::has_requested_render_assets() && !has_pending_render_assets()`
  （已有请求且 static mesh 全部到 GpuReady/Failed 终态、无 pending 纹理解码），连续满足
  32 帧（quiesce 余量，覆盖级联请求与 proxy 重建）即抓帧，写盘成功后 `request_exit()`；
  smoke 帧数上限退化为超时保底，超时抓帧告警"may capture an incomplete scene"。
- 抓帧经 `RenderDevice::request_back_buffer_capture / fetch_back_buffer_capture` 回读，stb 写 PNG（`_write_pending_frame_dump`）。
- `draw_engine_overlay()` 绘制帧统计浮层（`EngineFrameStatsOverlay` 窗口）；frame dump 模式（`frameDumpPath` 非空）下整体隐藏，保证 dump 确定性。

### UIContext（Editor 与 Engine 的 UI 边界）

- 生命周期钩子（`init/shutdown/begin_frame/render/handle_window_event`）由引擎持有；Editor 只使用绘制/查询 API（窗口、dock、控件、draw list、剪贴板、按键修饰、`register_render_target/register_texture_view` + `image/draw_surface_fill_available` 等）。
- 主题/字体/多视口能力由 `EngineInitConfig` 的 `ui*` 字段透传。
- 准入标准：只收后端无关、立即模式、编辑器之外也可复用的能力；编辑器工作区编排、默认 dock 布局、面板注册/持久化、Inspector 语义一律放上层编辑器门面。底层 dock/viewport 原语（`dock_builder_*` 等）可进，编辑器具体布局策略不可进。
- 纹理/surface 约定：通用预览传 `RenderTarget`；场景视口传 `ScenePresentationSubsystem` 的 `UISurfaceHandle`；Window 输出无有效 surface（交换链不可被 UI 采样）。`UITextureHandle` 是瞬态后端数据，禁止跨帧缓存；描述符注册由引擎持有。
- CPU 像素上传：`create_ui_texture_rgba8(pixels, w, h, debug_name)` → `shared_ptr<UITexture>`（RGBA8 sRGB、单 mip、SRV-only 的不透明所有权载体，同时持有 texture+view——`TextureView` 对父纹理仅 weak 引用），配套 `register_ui_texture/unregister_ui_texture`。Editor 图标等像素数据一律走此门面创建 GPU 纹理（SDD-2026-07-08-editor-icon-ui-texture-facade），禁止直连 `GraphicsContext`。
- Vulkan 动态渲染路径下 ImGui 初始化必须显式填 `UseDynamicRendering` 与颜色附件格式，保持与引擎渲染路径一致。

## 约束与不变式

- `set_backend_override` 必须在 `initialize()` 之前调用，之后无效。
- Editor 及其他上层 UI 代码禁止绕过 `UIContext` 直接调用 ImGui 或 Graphics；需要新能力时给 `UIContext` 加封装接口。
- `Application` 是全局单例（`Application::app`）；主循环线程即 render 线程，渲染命令只能在该线程 pump。
- `--dump-frame` 路径仅在 smoke 帧数限定下生效；抓帧确定性约定（固定相机、隐藏 overlay、禁 TAA jitter）见 `docs/VERIFY.md` RenderGate 节。

## 验证

对齐 `docs/VERIFY.md`「Scene / Asset / Application 生命周期」行：

- 构建 + `run.bat all Debug --smoke-test-seconds=5`（全矩阵 smoke），Editor 打开默认场景操作一遍。
- 改动命令行契约或 frame dump 路径时追加 `RunRenderGate.bat`（其依赖 `--rhi/--smoke-test/--dump-frame`）；改 PerfGate 注入时跑 `RunPerfGate.bat -Profile Standard`。
- UIContext/UI 改动：`run.bat editor` 手动过一遍改动路径。

## 历史

- [SDD-2026-07-07-render-gate 渲染验证安全网（RenderGate）](../../sdd/SDD-2026-07-07-render-gate.md)：新增 `--rhi/--dump-frame/--scene` 命令行与抓帧模式 overlay 隐藏。
