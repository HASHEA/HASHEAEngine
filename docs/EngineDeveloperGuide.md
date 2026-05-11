# AshEngine Engine 开发指南

> 面向 Engine / Runtime / RHI / 渲染基础设施开发者。本文档描述当前主干的真实架构、边界、配置、调试方式与维护约定。

---

## 0. 文档定位与维护约定

### 本文档的定位

`EngineDeveloperGuide.md` 是 **Engine 侧长期维护主文档**。今后只要发生以下类型的改动，都应同步更新本文档：

- Engine 模块分层与依赖边界变化
- Application 生命周期变化
- DynamicRHI / backend 选择逻辑变化
- Renderer / RenderDevice / 资源模型 / 渲染流程变化
- Shader 编译、缓存、反射、绑定规则变化
- 运行目录、配置项、日志路径、缓存路径变化
- Engine 暴露给 Editor / Game / Client 的公共能力变化

### 与其他文档的关系

- Editor 侧长期维护主文档：`docs/EditorDeveloperGuide.md`
- Engine UI 专题文档：`docs/EngineUIContext.md`
- Scene presentation 专题：`docs/ScenePresentationSubsystemGuide.md`

### 维护规则

- Engine 相关开发完成后：
  - 更新本文档
  - 若改动属于某个专题，再同步更新对应专题文档
- Editor 相关开发完成后：
  - 更新 `EditorDeveloperGuide.md`
  - 若改动涉及 Engine 暴露给 Editor 的边界，同时更新本文档

---

## 1. 仓库结构总览

```text
AshEngine/HASHEAEngine/
├── premake5.lua                     # 顶层 Premake workspace
├── docs/                            # 长期维护文档
├── product/                         # 运行期配置、日志、缓存、最终可执行目录、运行期资产
├── _BUILD/                          # VS/MSBuild 中间产物与 target 输出
└── project/
    ├── src/
    │   ├── engine/
    │   │   ├── Base/                # 日志、内存、断言、窗口、输入、序列化、时间等基础设施
    │   │   ├── Function/            # Engine-facing 高层能力
    │   │   │   ├── Application.*    # 应用生命周期与主循环
    │   │   │   ├── Render/          # RenderDevice / Renderer / 高层资源封装
    │   │   │   ├── Gui/             # ImGuiLayer + UIContext
    │   │   │   ├── Asset/           # AssetDatabase
    │   │   │   └── Scene/           # Scene / Entity / 组件描述
    │   │   └── Graphics/            # RHI 抽象与 Vulkan / DX12 后端
    │   └── editor/                  # Editor 可执行项目
    │   └── sandbox/                 # Engine 自维护测试/验证可执行项目
    └── thirdparty/                  # 第三方库
```

---

## 2. 分层与依赖边界

### 2.1 Base 层

职责：

- 日志、断言、内存服务
- 窗口与输入
- 时间、字符串、文件、序列化、服务管理

原则：

- 不承载渲染后端语义
- 尽量被 Function / Graphics 复用

### 2.2 Graphics 层

职责：

- RHI 抽象
- Vulkan / DX12 后端实现
- Shader 编译、反射、缓存、管线对象、命令缓冲、资源状态转换

原则：

- 这是 Engine 内部底层，不应直接暴露给 Editor / Game / Client
- 新功能如果会影响公共上层，应先看是否需要由 Function 层重新封装
- 若改动共享抽象，默认需要同时验证 Vulkan 与 DX12

### 2.3 Function 层

职责：

- 把 Graphics 层能力整理成上层可用的 Engine-facing API
- 隔离具体后端对象与 RHI 细节
- 维护 `Renderer`、`RenderDevice`、`UIContext`、`Scene`、`AssetDatabase` 等高层能力

原则：

- Engine 对外的长期公共边界优先放在这里
- 不把 Vulkan / DX12 具体类型泄露给上层

### 2.4 Editor / Game / Client 与 Engine 的关系

- Editor 不应直接依赖 `Graphics/`
- Game / Client 以后也应优先依赖 Function 层暴露的能力
- 如果某个接口明显只描述 Editor 工作区语义，不应直接塞进 Engine 通用 facade

目前 UI 已按这个规则收口：

- `ImGuiLayer` 保持 Engine 内部实现
- `UIContext` 仅保留通用 DevUI 能力
- Editor-specific workspace / panel / property-grid 语义应放在更高一层

### 2.5 Engine 侧错误处理风格

当前 Engine 代码统一倾向于使用项目内已有的 process-error 风格来收口多失败路径函数，优先使用：

- `ASH_PROCESS_ERROR`
- `ASH_LOG_PROCESS_ERROR`
- `ASH_PROCESS_ERROR_EXIT`
- `ASH_PROCESS_GUARD_*`

适用范围：

- 资源创建
- 初始化 / shutdown / begin / end
- 绑定、提交、加载、状态转换
- 其他存在多处失败出口的流程型函数

不建议机械改写的范围：

- 纯 getter
- 轻量 value 转换
- 一行包装器
- 简单 UI widget 透传

原则：

- 优先集中失败处理与单一返回出口
- 但不要为了“统一风格”牺牲可读性
- 在循环体内要谨慎使用 `ASH_PROCESS_ERROR`
- 如果失败需要终止整个函数，而不是只终止循环，应使用显式状态变量或把检查提升到循环外

---

## 3. 构建系统与输出目录

### 3.1 构建系统

项目使用 **Premake5 + Visual Studio/MSBuild**，不是 CMake。

关键 Premake 文件：

- 顶层 workspace：`premake5.lua`
- Engine 项目：`project/src/engine/premake5.lua`
- Editor 项目：`project/src/editor/premake5.lua`
- Sandbox 项目：`project/src/sandbox/premake5.lua`

### 3.2 当前主要输出目录

- 进程启动时，`EntryPoint.h` 会先把当前工作目录切回仓库根目录
- 中间文件 / target 输出：`_BUILD/<Config>-windows-x86_64/...`
- 运行目录：`product/bin64/<Config>-windows-x86_64/`
- 运行配置：`product/config/Engine.ini`
- 日志目录：`product/logs/`
- Sandbox 生成报告：`Intermediate/test-reports/sandbox/`
- Engine self-test 临时资产：`Intermediate/test-temp/engine/`
- Shader debug dump：`Intermediate/logs/shader-debug/`
- Sandbox 测试资产：`product/assets/models/gltfs/`
- Shader Cache：`product/caches/ShaderCaches/`
- Pipeline Cache：`product/caches/PipelineCaches/`

### 3.3 当前编译能力

Windows Debug / Release 下，Engine 同时编入：

- `ASH_HAS_VULKAN`
- `ASH_HAS_DX12`
- `ASH_HAS_DXC`

非 Windows 构建会在 Premake 中移除：

- `Graphics/DirectX12/**`
- `Graphics/DXC/**`
- `imgui_impl_dx12.*`

这意味着 **DynamicRHI 是运行时切换**，但前提仍然是目标平台已编入对应后端。

---

## 4. Application 生命周期

### 4.1 入口与初始化

`Application` 是 Runtime 主入口，核心配置在 `EngineInitConfig`：

- 初始窗口大小
- swapchain buffer 数
- 标题
- vsync
- 期望 backend
- backend 配置文件路径

当前初始化顺序：

1. 初始化日志与内存服务
2. 读取 `Engine.ini`，解析 Runtime RHI 配置
3. 解析最终 backend
4. 创建窗口
5. 创建 `GraphicsContext`
6. 创建 `Swapchain`
7. 创建 `RenderDevice`
8. 创建 `Renderer`
9. 初始化 `SceneRenderer`
10. 初始化 `ScenePresentationSubsystem`
11. 创建并初始化 `UIContext`

补充说明：

- `EntryPoint.h` 会先搜索仓库根目录，并把进程工作目录切到该根目录
- 因此默认的 `product/...` 相对路径会稳定落在仓库内的 `product` 树下
- `Application` 构造函数只保存 `EngineInitConfig` 并设置全局访问指针，不创建窗口、RHI 或渲染资源；初始化逻辑集中在 `Application::initialize()` / `Application::initialize(config)`
- `EntryPoint.h` 在 `create_application()` 返回对象后显式调用 `initialize()`，再检查 `is_initialized()`；派生类构造函数不能依赖窗口、RHI、Renderer 或 `UIContext` 已存在
- 需要依赖 Engine runtime 的派生类 bootstrap 应放在 `_on_startup()` 或之后，而不是放在派生类构造函数中
- `create_application()` 返回空指针或 `Application::is_initialized()==false` 时，`EntryPoint.h` 会记录 fatal 到 stderr，销毁半初始化对象并返回非 0；不要在 `Application::start()` 内继续容忍半初始化运行
- `EntryPoint.h` 用局部 `Application*` 持有 create/destroy ownership，`Application::app` 只作为运行期全局访问指针；不要把静态指针当成对象所有权来源
- `Application::initialize()` 必须检查 `GraphicsContext::init()`、`Swapchain::init()`、`SceneRenderer::initialize()`、`ScenePresentationSubsystem::initialize()` 等关键返回值；新增关键初始化步骤时，失败路径也要保持 `is_initialized()==false`
- `Application` 析构统一走 `_shutdown_runtime()` 幂等清理 partial runtime state；不要在局部失败路径里手写一套资源释放顺序
- `EntryPoint.h` 还支持 smoke-run 自动退出：
  - `ASH_ENGINE_SMOKE_TEST_FRAMES`
  - `ASH_ENGINE_SMOKE_TEST_SECONDS`
  - `--smoke-test=<frames>`
  - `--smoke-test-seconds=<seconds>`
- `EntryPoint.h` 支持 `--engine-self-test`，用于运行不创建窗口/RHI 的 Engine Base 层自测

### 4.2 主循环

`Application::start()` 的核心流程为：

1. `_pump_platform_events()`
2. `_tick_frame()`
3. `pump_render_commands()`
4. `_render_frame()`
5. `_present_frame()`
6. `pump_render_commands()`

补充说明：

- `UIContext::begin_frame()` 在事件泵阶段启动
- 未启用 logic thread 时，`ScenePresentationSubsystem` 的 update phase 会在 `_on_update()` 之后执行
- 启用 logic thread 时，`ScenePresentationSubsystem` 的 update phase 会跟在 `_on_logic_startup()` / `_on_logic_update()` 后执行
- update phase 必须保持 CPU-only：不能在这里创建 RHI texture / buffer / sampler / program；GPU 资产 finalization 与材质 proxy 准备归 render thread 的 submit phase
- 默认 `Application::_on_render()` 会按 `begin_frame() -> _on_render_debug() -> scene presentation submit -> _on_gui() -> end_frame()` 的固定顺序运行
- UI 绘制最终在 `Renderer::end_frame()` 内作为末尾 overlay 提交
- 窗口最小化时会跳过实际渲染
- 渲染线程即主线程，负责：
  - 平台事件泵
  - render command queue 执行
  - `Renderer` 帧录制 / 提交 / present

### 4.3 第一阶段线程模型

当前主干已经接入一套 **UE 风格的第一阶段多线程骨架**，目标是先把“逻辑线程 + 渲染线程 + worker 线程池”建立起来，并给未来的 RHI 线程预留扩展点。

当前线程角色定义在：

- `project/src/engine/Base/hthreading.h`

当前可用角色：

- `Render`
- `Logic`
- `Worker`
- `RHI`（仅保留角色位，尚未真正启用独立 RHI 线程）

当前配置入口在 `EngineInitConfig.threading`：

- `enable_logic_thread`
- `worker_thread_count`
- `logic_thread_idle_sleep_ms`

当前行为约定：

- `Application::initialize()` 会初始化 engine threading 基础设施与 worker 线程池；生命周期约束以 `initialize()` / `_shutdown_runtime()` 为准
- 如果 `enable_logic_thread=false`，行为保持单线程兼容：`_on_update()` 仍在主线程执行
- 如果 `enable_logic_thread=true`：
  - 主线程继续作为渲染线程
  - `Application` 会拉起独立逻辑线程
  - 默认 `Application::_on_logic_update()` 会转调 `_on_update()`
  - 因此已有 app 可以通过“开启 logic thread”把 update 迁移到逻辑线程；若需要更细分职责，应显式覆写新的 logic hook

### 4.4 Render Command Queue

当前逻辑线程到渲染线程的通信方式，采用 UE 风格的 enqueue 模型：

- `enqueue_render_command()`
- `ASH_ENQUEUE_RENDER_COMMAND(Name, Lambda)`

行为规则：

- 如果当前已经在渲染线程内，render command 会立即执行
- 如果当前在逻辑线程或 worker 线程内，command 会进入 render queue，等待主线程在每帧 pump 时执行
- 线程系统 shutdown 期间，新的 render/worker command 会被拒绝，而不会错误地在非目标线程内联执行

这条路径当前用于：

- 逻辑线程通知渲染线程执行 render-side follow-up
- 将来承接 scene proxy 创建、GPU 资源上传后的 render-side 接入等工作

### 4.5 Logic Thread Hooks

除了已有钩子外，`Application` 现在新增了逻辑线程生命周期钩子：

- `_on_logic_startup()`
- `_on_logic_update()`
- `_on_logic_shutdown()`

当前建议：

- render-thread 专属工作继续放在：
  - `_on_startup()`
  - `_on_render()`
  - `_on_gui()`
  - `_present()`
- scene/world/update/asset orchestration 逐步迁到：
  - `_on_logic_startup()`
  - `_on_logic_update()`
  - `_on_logic_shutdown()`
- scene presentation 的声明更新属于 logic/update 侧；scene-driven draw submit 继续走默认 `_on_render()` 内的固定提交阶段

### 4.6 输入快照

为避免逻辑线程直接读写主线程正在更新的输入状态，`Application` 当前做了第一阶段输入快照隔离：

- 主线程维护真实 `InputState`
- 每帧事件泵结束后，把输入状态复制到逻辑线程待消费快照
- 逻辑线程在 update 前消费该快照
- `Application::get_input()` 会按当前线程角色返回对应的输入状态引用

这意味着：

- 逻辑线程读到的是“上一轮事件泵发布的稳定快照”
- 这套机制当前只解决 `InputState` 访问竞争，不代表其他高层系统已经自动线程安全

### 4.7 可重写钩子

当前主要虚函数：

- `_on_startup()`
- `_on_shutdown()`
- `_on_update()`
- `_on_logic_startup()`
- `_on_logic_update()`
- `_on_logic_shutdown()`
- `_on_gui()`
- `_on_render_debug()`
- `_on_render()`
- `_present()`

Engine 侧改动这些钩子的调用时机、职责或约束时，必须回写本文档与 Editor 文档。

---

## 5. DynamicRHI 与运行时后端切换

### 5.1 目标

当前引擎不再依赖 Premake 宏硬切换单一运行时后端，而是：

- 编译期决定“某后端是否被编入”
- 运行期通过配置决定“本次启动使用哪个后端”

### 5.2 关键入口

相关实现位于：

- `project/src/engine/Graphics/DynamicRHI.h`
- `project/src/engine/Graphics/DynamicRHI.cpp`

核心能力：

- `is_backend_compiled()`
- `get_default_backend()`
- `resolve_runtime_backend()`
- `load_runtime_rhi_config()`
- `GraphicsContext::create()`
- `Swapchain::create()`

### 5.3 当前默认策略

- Windows：优先 DX12，其次 Vulkan
- 非 Windows：优先 Vulkan，其次 DX12

如果配置请求了一个当前构建未编入的 backend，会：

- 输出 warning
- 回退到可用默认 backend

### 5.4 当前配置文件

当前运行配置文件为：

```ini
[RHI]
Backend=Vulkan

[VulkanValidation]
Enabled=true
GpuAssisted=false
SynchronizationValidation=true
BreakOnValidationError=false

[DX12Validation]
Enabled=true
GpuValidation=true
```

当前有效配置项：

- `RHI.Backend`
- `VulkanValidation.Enabled`
- `VulkanValidation.GpuAssisted`
- `VulkanValidation.SynchronizationValidation`
- `VulkanValidation.BreakOnValidationError`
- `DX12Validation.Enabled`
- `DX12Validation.GpuValidation`

Validation 开关只在 Debug 配置下生效。Release 构建即使 `Engine.ini` 中设置 `VulkanValidation.Enabled=true` 或 `DX12Validation.Enabled=true`，也必须保持 validation 关闭；其中 Vulkan 通过不编入 `VULKAN_DEBUG_REPORT` 生效路径实现，DX12 在 `DynamicRHI` 配置解析和 `DX12Context` 初始化层都会强制关闭 debug layer / GPU-based validation。

实现注意事项：

- `DynamicRHI.cpp` 的 INI 解析已兼容 UTF-8 BOM；不要假设配置文件一定是无 BOM 文本。
- 自动化验证脚本在切换 backend 时会重写 `Engine.ini`，并且会回读运行日志确认“请求 backend”和“实际启动 backend”一致。
- Debug 下当 `DX12Validation.Enabled=true` 时，D3D12 debug layer 的 warning / error / corruption 会通过 `DX12Context` 直接写入引擎日志；DXGI 层消息则在 swapchain 创建 / resize / present 等节点被 drain 后写入同一套日志。
- DX12 validation 默认不对 error / corruption 调用 `SetBreakOnSeverity(TRUE)`；错误必须进入日志，由 smoke / validation 脚本判定失败，避免自动化运行被调试中断直接杀死。
- 为避免 DX12 debug-layer 在逐帧场景下刷爆日志，相同 warning / error 会按“首条立即打印，重复次数在 shutdown 时汇总”的方式输出。

以后新增或删除配置项，必须同步更新本文档。

---

## 6. 渲染栈总览

当前渲染栈分为四层：

1. `GraphicsContext`
2. `Swapchain`
3. `RenderDevice`
4. `Renderer`

### 6.1 GraphicsContext / Swapchain

职责：

- 设备、队列、命令池、交换链等底层后端对象生命周期
- Validation 层初始化
- 后端级资源创建入口

这是 RHI 层，不应直接暴露给 Editor / Game / Client。

### 6.2 RenderDevice

`RenderDevice` 是 Function 层里最靠近 RHI 的一层封装，职责包括：

- 创建高层资源包装对象
- 管理 pass begin / end
- 绑定 graphics / compute program
- 绑定 vertex / index buffer
- 设置 viewport / scissor
- 提交 draw / draw indexed / dispatch
- 维护 back buffer 与 transient render target 池
- 管理高层资源到 RHI 资源/视图的映射
- 在合适时机执行资源状态转换

高层 `RenderTextureFormat` 到 RHI `AshFormat` 的映射统一维护在 `Function/Render/RenderFormatUtils.*`。`RenderDevice`、`TextureAsset`、`ImGuiLayer` 或其他 Function 层代码不应再各自复制一份 `to_rhi_format()` / `from_rhi_format()`；新增格式时必须同时更新 `RenderFormatUtils`、Vulkan/DX12 RHI 格式表和 self-test。

当前窗口输出规则：

- `get_back_buffer()` 返回当前帧窗口输出目标；主窗口路径默认直接返回 swapchain target
- `end_frame()` 如果检测到本帧已经写入 swapchain target，会直接进入 `present()`，不再执行 offscreen -> swapchain copy
- Engine 仍保留内部 offscreen back buffer 作为兼容 fallback；只有明确写入该 offscreen target 的路径才会在 present 阶段 copy 到 swapchain

这意味着：

- Editor / Game / Client 上层不应依赖 `get_back_buffer()` 具备 shader-resource 能力；需要采样的视口输出仍应显式创建 offscreen `RenderTarget`
- 引擎内部 final present 路径不依赖额外的“fullscreen shader present pass”；直接写 swapchain 时由 pass end barrier 转到 `Present`，fallback offscreen 路径才走 backend copy + present state transition
- DX12 不存在 Vulkan `MAILBOX` 的一一对应语义；当前将 `MAILBOX` / `IMMEDIATE` 作为低延迟 uncapped present 请求处理，在系统支持 tearing 时使用 `Present(0, DXGI_PRESENT_ALLOW_TEARING)`，否则退化为 `Present(0, 0)`，并在 swapchain 创建日志中输出最终 sync interval / flags / tearing support。

### 6.3 Renderer

`Renderer` 是更高一层的帧 orchestration facade，职责包括：

- `begin_frame() / end_frame() / present()`
- 高层资源创建便捷入口
- `begin_pass()` + `GraphicsPassContext`
- 收集 draw call 并在 pass 结束时统一提交
- 统计 frame stats
- 在 `end_frame()` 时渲染 UI

### 6.4 当前 pass / dispatch 规则

当前规则非常重要：

- `dispatch()` 必须发生在 **没有 active graphics pass** 的时候
- `begin_pass()` 后的 draw 会先记录到 `GraphicsPassContext`
- 真正的资源 transition 会在 pass 提交前统一完成
- 然后才真正调用底层 `begin_pass()`
- `PassDesc::allow_reorder_draws` 是显式 opt-in；只有顺序不敏感的 pass 才能开启。当前 `SceneRenderer` 的 opaque static mesh pass 会按 program / index buffer / vertex buffer 做稳定排序，以减少 pipeline 和 descriptor 重绑
- pass 提交前会对同一个 `GraphicsProgram`、vertex buffer、index buffer 的 transition 做一次性处理；提交 RHI 前还会按 resource/range/state 合并重复的只读 barrier，UAV/RTV/DSV 等写状态 barrier 保持原顺序
- RHI graphics program 的 raster/depth/topology 状态在 variant 创建时写入 `GraphicProgramCreateDesc::pipeline`；`RenderDevice::bind_graphics_program()` 不能逐 draw 调用 `apply_render_state()`，因为 DX12 会把它标记为 PSO rebuild，Vulkan 会直接重建 `VkPipeline`
- `RenderDevice::bind_graphics_program()` 按绑定版本缓存 `CommitProgramBindings`；DX12 因 GPU descriptor heap 帧内线性分配仍按 pass 重新 commit，Vulkan graphics program 的 descriptor set 可跨 pass/frame 复用，直到高层 binding version 变化
- `RenderDevice::begin_pass()` 会复用 RHI `RenderPass` 与 `Framebuffer`：RenderPass key 由 attachment format / load action / final state / multiview 等语义构成，Framebuffer key 由 render pass、attachment texture、extent 与 layer 构成；clear value 不属于 cache key，每次 begin pass 都会重新写入当前 clear 值
- Framebuffer cache 会在连续数帧未使用后清理；`clear_transient_render_targets()` 也会清掉 framebuffer cache，避免 transient / resize 路径长期压住旧 attachment
- Vulkan dynamic rendering 下，`cmd_end_render_pass()` 结束后 attachment 仍停留在 renderable layout；resource tracker 必须先保持 `RTV` / `DSVWrite` 这类真实附件状态，再由 pass 外部显式 barrier 转到 `SRVGraphics` / `Present` 等 final state
- DX12 texture transition 由 `DX12ResourceTracker` 维护 per-subresource 状态；当整资源 transition 遇到 mip/slice 混合状态时，必须展开成子资源 barrier，不能用某个旧整资源状态覆盖全部 subresource
- RHI `CommandBuffer` 会记录首个命令录制错误；`RenderDevice` 在 `begin_record()`、barrier/copy/upload、`end_pass()` 和 `end_record()` 后必须检查 `has_error()`，有错时跳过 submit，不能把已知非法命令提交给 Vulkan/DX12 queue

这样做的原因是：

- Vulkan 不允许把某些 `vkCmdPipelineBarrier` 调用塞进 render pass / dynamic rendering 活跃区间
- Vulkan final present 也不应依赖额外 graphics pass 去做纯搬运/翻转
- 因此共享高层渲染路径必须保证 barrier 在 pass 外部完成

如果以后改动这条规则，必须同时验证 Vulkan 与 DX12。

### 6.5 GPU Upload Command Path

当前 GPU 资源的 CPU->GPU 初始数据上传，统一不再走“创建/更新时立即提交并等待”的路径，而是改为：

- 每帧维护一条专用 upload command buffer / command list
- 当帧内发生 GPU-only buffer 的 `initial_data` / `update()`，或 texture 的 `initial_data` 上传时，把 copy/upload 命令记录到这条 upload cmd
- 帧结束提交时，upload cmd 会先于主 render command buffer 提交
- 如果上传请求发生在 frame 之外，则先把数据复制进 pending upload 队列，并在下一次 `begin_frame()` 时灌入 upload cmd

当前这条路径覆盖：

- GPU-only buffer 的创建初始数据上传
- GPU-only buffer 的后续 `update()` 上传
- texture 创建时的 `initial_data` 上传

这样做的约束是：

- 这些上传默认都是“随下一次帧提交生效”，不再是即时阻塞提交
- 共享高层渲染路径应假定 upload cmd 与 render cmd 处于同一帧提交流水中
- texture `initial_data` 当前按“整张纹理紧密排列的完整初始内容”解释；当 `TextureUploadDesc::mip_level_count > 1` 时，初始数据必须按 mip0、mip1... 顺序 tight-packed，backend 内部会展开成各 subresource copy
- 多采样纹理、深度/模板纹理、稀疏纹理的 `initial_data` 上传当前不走这条通路；如果以后要补，优先继续复用同一套 per-frame upload cmd 设计

---

## 7. Function 层高层渲染资源模型

### 7.1 当前公开包装对象

当前高层资源包括：

- `RenderTarget`
- `UniformBuffer`
- `VertexBuffer`
- `IndexBuffer`
- `StorageBuffer`
- `GraphicsProgram`
- `ComputeProgram`

这些类型：

- 对上隐藏具体后端对象
- 内部通过 `Impl` 持有 RHI 资源
- 由 `Renderer` / `RenderDevice` 创建

`UniformBuffer` 的底层 allocation 会按 256 字节对齐。高层创建时如果传入 `initial_data`，`RenderDevice::create_uniform_buffer()` 必须把逻辑数据拷贝到同等大小的 zero-padded 临时块后再交给 RHI，避免 Vulkan `vkCmdUpdateBuffer` 或 DX12 upload path 按分配大小读取时越界。

### 7.2 RenderTarget

当前支持的典型用途：

- Engine offscreen back buffer 包装
- color render target
- depth-stencil target
- shader resource
- unordered access target

当前 `RenderTargetDesc` 还支持声明“optimized clear value”：

- `use_optimized_clear_value`
- `optimized_clear_color`
- `optimized_clear_depth_stencil`

用途：

- 给 DX12 render target / depth-stencil 资源传递匹配的 optimized clear value
- 避免 `ClearRenderTargetView` / `ClearDepthStencilView` 的 debug-layer “missing/mismatching clear value” warning

约束：

- 如果某张目标会长期通过 `RenderLoadAction::Clear` 以固定颜色/深度清屏，创建时应把 optimized clear value 设成与实际 clear 一致的值
- 如果实际 clear 值和创建值不一致，DX12 仍然会给出 warning
- Engine 内部 offscreen back buffer 只作为 fallback/copy 路径保留；主窗口默认直接写 swapchain target。若某个 offscreen 目标会长期通过 `RenderLoadAction::Clear` 以固定颜色/深度清屏，创建时应提供匹配的 optimized clear value
- DX12/Vulkan swapchain 常见格式是 `B8G8R8A8`，`RenderTextureFormat` 必须保留 `BGRA8_UNORM` / `BGRA8_SRGB` 到 RHI 格式的双向映射；UI 或其他高层直接读取 swapchain target format 时不能退化成 `Unknown`。
- DX12 flip-model swapchain surface 必须使用 `*_UNORM`，但 `DX12Swapchain::get_format()` 应返回应用请求的 render-target 格式；当请求 `*_SRGB` 时，back buffer RTV、RenderPass 和 PSO 都必须继续使用 `*_SRGB`，由硬件执行 linear -> sRGB 编码，不能因为 swapchain surface 是 UNORM 而把高层格式降级成 UNORM。

### 7.3 GraphicsProgram / ComputeProgram

当前 `GraphicsProgram` / `ComputeProgram` 同时承担：

- pipeline state 载体
- shader entry / macro / state 描述
- 资源绑定入口
- root constants / constant block 设置
- 按名称绑定 buffer / texture / sampler

这符合当前引擎把 “Program 作为高层 pipeline + binding facade” 的设计。

### 7.4 资源绑定接口

当前支持的典型绑定能力：

- constant data block
- static int / uint / float
- uniform buffer
- storage buffer / rw storage buffer
- texture / rw texture
- 各类数组绑定
- sampler / sampler array

高层通过字符串名绑定，底层结合 shader reflection 消费这些绑定。

---

## 8. Shader 编译、反射与缓存

### 8.1 当前着色器主路径

当前主路径是：

- 上层使用 HLSL
- 通过 DXC 编译
- Vulkan 路径消费 SPIR-V / SPIR-V 反射
- DX12 路径消费 DXIL + DXC / D3D12 reflection

其中当前共享 scene/view/projection 路径的约定是：

- 共享数学路径使用 DX 风格的 left-handed + depth `[0, 1]`
- vertex / tess-eval / geometry / mesh 等会输出图元位置的 Vulkan shader stage，编译时带 `-fvk-invert-y`
- `-fvk-invert-y` 会改变 Vulkan 侧 framebuffer-space winding，因此 Vulkan pipeline 创建时需要补偿性翻转 front-face
- DX12 不应额外翻转 front-face；它应直接消费共享的 `AshFrontFace` 定义

因此当前引擎对上层的约定是：

- HLSL 仍按统一的 DX 风格坐标/手性语义编写
- Vulkan/DX12 坐标差异由 backend 编译与 pipeline 层吸收，而不是靠某个特定 pass 做临时翻转补偿

### 8.2 Shader Pool Key 与失效

`RHI::get_shader_hash(ShaderCreation)` 是 Vulkan/DX12 shader pool 的共享 key 入口。它必须同时包含：

- base shader / user shader / generated bindings 的路径
- 这些文件的当前 size 与 last-write-time
- `ShaderCreation::source_hash`
- entry point、stage、宏字符串

材质 V2 生成路径会把 `MaterialResource::combined_source_hash` 传入 `GraphicsProgramDesc::source_hash`，再进入 `ShaderCreation::source_hash`。因此材质编译 hash、generated bindings 文本、host shader path 或用户 shader 内容变化时，都不应复用旧 shader 对象。

对非材质路径，如果调用方已经有更权威的源码版本号或生成文本 hash，也应显式填入 `GraphicsProgramDesc::source_hash` 或 `ComputeProgramDesc::source_hash`。

### 8.3 反射驱动的内容

当前反射会参与：

- descriptor / resource binding 信息提取
- Vulkan descriptor set layout 生成与缓存
- DX12 root signature / descriptor range 生成
- thread group size / parameter block 元信息提取
- graphics / compute program 的 reflected sampler 名称提取

关于 graphics vertex input，当前主干规则已经调整为：

- shader reflection 仍会保留 vertex stage 的 active inputs 元信息
- 但 **共享 graphics program 不再依赖 Vulkan SPIR-V reflection 去“补全”静态 mesh 这类预定义顶点布局**
- 对于上层已经明确知道顶点结构的路径，应由 C++ 显式提供 `GraphicsProgramDesc.vertex_input`
- 显式 layout 是 Vulkan 与 DX12 的共同权威输入
- 只有当上层没有提供显式 layout 时，backend 才允许退回使用反射得到的 vertex input

这条规则的原因是：

- Vulkan / SPIR-V reflection 只能稳定看到 shader 当前真正活跃的输入
- 如果某些顶点字段暂时未在 shader 中使用，它们可能不会出现在反射结果里
- 对 `MeshVertex` 这类固定 CPU/GPU 顶点结构，依赖“活跃输入反射结果”来重建完整 input layout 是不可靠的

当前推荐做法：

- `Graphics/VertexInputLayout.h` 只负责低层通用 layout assembly 与合法性校验
- Engine 侧应再维护一层稳定的 `VertexDecl` 抽象，作为预定义 vertex layout 的共享入口
- 具体 vertex type 的 preset 应定义在 Engine 侧、并靠近拥有该 vertex type 的模块
- 例如静态场景 mesh 当前使用 `Function/Render/VertexLayoutPresets.h` 中基于 `MeshVertex` 的 `VertexDecl`
- `GraphicsProgramDesc` 可以直接携带 `VertexDecl`；若同时给了裸 `vertex_input`，则两者必须一致
- 显式 layout 应优先使用 `sizeof(...)` / `offsetof(...)`，不要在 RHI 层硬编码 stride / offset
- Vulkan 与 DX12 backend 都应优先消费这份显式 `VertexDecl`/`vertex_input`，只有上层未提供时才退回 shader reflection
- 当上层显式提供 `VertexDecl`/`vertex_input` 时，Engine 会再用 shader reflection 做“active vertex inputs 是否被这份显式 layout 覆盖”的校验；对于 Vulkan，这份 active-input reflection 还会用于把最终 pipeline vertex input 收窄到 shader 实际消费的 attribute 子集，以避免 validation warning，但它不会再回写或篡改共享的静态顶点布局定义

### 8.4 ShaderParameterBlockLayout

当前引擎已经把 `ShaderParameterBlockLayout` 作为一条共享设计路径：

- DX12 后端消费它
- Vulkan 后端也应保持同一消费语义
- 高层 `GraphicsProgram` / `ComputeProgram` 会把 root constants / const block 绑定到这条路径上

如果以后改动 parameter block 规则，需要同时看两套后端是否仍一致。

补充说明：

- DX12 路径当前允许把约定名为 `AshRootConstants` / `RootConstants` 的 HLSL constant block 视为 root constants
- Vulkan 路径不能直接把同一份普通 `cbuffer` 反射成 push constants；当前主干做法是：
  - 上层 HLSL 继续写 `cbuffer AshRootConstants` / `cbuffer RootConstants`
  - Vulkan 运行时编译前，会把这类 block 重写为 `struct + [[vk::push_constant]]` 变量，并通过宏把原成员名映射回去
  - Vulkan rewrite 时还会去掉 DXC 预处理后 `cbuffer` 成员自带的 `const`，否则改写后的 struct 会编译失败
- Vulkan shader reflection 现在不只会产出 push constants；对于普通 uniform-buffer constant block（例如材质侧的 `AshMaterialParameters`），也会从最终 SPIR-V 中补齐 `ShaderParameterBlockLayout`
- 因此高层如果通过 `GraphicsProgram::get_parameter_block_layout(...)` 查询非 root constant block，Vulkan 与 DX12 都应该能返回同名 layout；不要再假设 Vulkan 只会在这条路径上暴露 `AshRootConstants`
- 同一个逻辑 constant block 在 Vulkan / DX12 上的 `byte_size` 允许不同：DX12 reflection 可能保留 cbuffer 尾部 padding，而 Vulkan reflection 更接近声明结构体本体大小；高层打包参数时必须以反射出来的 member `offset/size` 为准，不要硬编码“两个后端 byte_size 必须相等”
- 因此如果以后调整 root constants 命名、shader preprocess、cache key 或 parameter-block 规则，必须同时验证：
  - DX12 root constants / root signature 路径
  - Vulkan push constants / SPIR-V reflection 路径

### 8.5 当前缓存目录

当前缓存目录为：

- Shader Cache：
  - `product/caches/ShaderCaches/dx12`
  - `product/caches/ShaderCaches/vulkan`
- Vulkan Pipeline Cache：
  - `product/caches/PipelineCaches/AshVulkanPipelineCache.pipelineCacheVK`

当前 `ShaderCache` 相关声明在：

- `project/src/engine/Graphics/ShaderCache.h`

### 8.6 Shader 规则变更时的文档要求

如果发生以下任一变化，必须更新本文档：

- shader 源格式变化
- compiler 参数变化
- reflection 规则变化
- binding 验证规则变化
- cache key / cache 目录变化
- vertex input 生成规则变化

---

## 9. Vulkan / DX12 双后端开发规则

### 9.1 默认规则

以后所有新功能、渲染 bug 修复、底层抽象改动，默认按以下规则执行：

- 如果只改纯 Vulkan 私有实现，可只验证 Vulkan
- 只要改到了共享抽象、Function 层、Shader 绑定、资源状态、Renderer、RenderDevice、UI、配置或公共 API，就应视为 **双后端改动**
- 双后端改动默认需要验证 Vulkan + DX12

### 9.2 典型高风险改动类型

- `Renderer` / `RenderDevice`
- `Graphics/RenderProgram.*`
- `Graphics/Shader.*`
- `Graphics/DescriptorSet*`
- `Graphics/GraphicsContext.*`
- `Function/Gui/*`
- DynamicRHI 与配置项

### 9.3 非 Windows 兼容约束

做公共改动时要注意：

- 不要把 DX12 / DXC 头文件泄露到未做平台保护的公共路径
- 非 Windows 构建仍需要能通过 premake 的文件裁剪正常编译

---

## 10. UI 系统

### 10.1 当前分层

当前 UI 分层为：

- `ImGuiLayer`：Engine 内部后端桥接层
- `UIContext`：Engine 对上的通用 DevUI facade

### 10.2 当前边界

`UIContext` 当前只适合承载：

- 通用 immediate-mode widget
- 通用 docking / viewport 原语
- 窗口 / 子窗口 / 菜单 / tabs / tables / popup
- wrapped text / 多分量数值编辑 / 颜色编辑 / 稳定 id tree / richer table column 配置
- RenderTarget / `UISurfaceHandle` 显示
- 输入捕获查询

它 **不再承载**：

- workspace / 默认 dockspace 布局 policy
- panel 系统语义
- property-grid / inspector 语义

相关专题说明见：

- `docs/EngineUIContext.md`
- `docs/EditorDeveloperGuide.md`

### 10.3 后续原则

- Engine 继续维护 `ImGuiLayer` 与 `UIContext`
- `UIContext` 可以继续补充“原语级”的 docking / viewport / widget 能力，但不要在这里固化 editor 的默认布局、面板编排或 property-grid 规则
- Editor 如需更高层工作区接口，应在其上方再包 editor-specific facade

---

## 11. Asset 与 Scene 基础模块

### 11.1 AssetData：CPU 侧资源基础数据模型

当前 Engine 在 `project/src/engine/Function/Asset/AssetData.*` 中维护一套**纯 CPU 侧**资源数据结构：

- `MeshVertex`
- `MeshSection`
- `MaterialSlot`
- `Mesh`
- `ModelNode`
- `Model`
- `AshAssetNode`
- `AshAsset`

它们的职责是：

- 保存从外部资源文件导入出的原始几何、层级、材质槽与组件快照
- 作为 Engine / Editor / 以后 Game 侧都可消费的中间数据模型
- 明确与 GPU 资源生命周期解耦

这层**不直接创建 GPU 资源**，也不暴露 Vulkan / DX12 / 底层 RHI 类型。

### 11.2 当前导入与序列化能力

当前公开 API 为：

- `load_mesh_from_file()`
- `load_model_from_file()`
- `load_ashasset_from_file()`
- `save_ashasset_to_file()`
- `make_ashasset_from_model()`

当前支持的模型源格式：

- `.obj`
- `.fbx`
- `.gltf`
- `.glb`

当前行为约定：

- `load_model_from_file()` 返回带层级的 `Model`
- `load_mesh_from_file()` 走“先导入 `Model`，再按节点 world transform 合并成单 `Mesh`”的 CPU 路径
- `OBJ` 通过 `tinyobjloader` 导入
- `glTF/glb` 通过 `tinygltf` 导入
- `FBX` 通过 `openFBX` 在 triangulate 模式下导入
- `glTF/glb` indexed primitive 必须保留顶点复用：导入时按 primitive 内的 source vertex index 建立 remap，只为实际引用过的 glTF 顶点创建一次 `MeshVertex`，`Mesh::indices` 保存 remap 后的索引
- 导入出的材质信息当前仍保留为 `MaterialSlot` 与纹理路径这层 CPU 元数据
- 正式材质系统已经切到 V2-only 的 `MaterialInterface / Material / MaterialInstance`
- V1 `.material/.mat` 运行时资产与兼容路径已移除
- imported `MaterialSlot` 现在会在 render asset 同步阶段按 `material_slot` 稳定映射到默认 `MaterialInterface`
- imported `MaterialSlot` 仍不是最终 GPU 材质对象；真正的 draw-time 绑定由 `MaterialRenderProxy + MaterialSystem` 在 render thread 上解析

当前 `ashasset` 是 **prefab-style JSON 资源格式**，它保存：

- 节点名
- 父子层级
- `TransformComponent`
- 可选 `CameraComponent`
- 可选 `LightComponent`
- 可选 `MeshComponent`

`make_ashasset_from_model()` 可把导入出的 `Model` 直接转成一个可实例化的 prefab 骨架。

### 11.3 AssetDatabase：资源目录、类型识别与缓存

`AssetDatabase` 现在不再只提供 text / binary 访问，而是同时承担 Engine 侧资源目录索引与轻量缓存职责。

当前提供：

- root path 管理
- 目录扫描 / `refresh()`
- `AssetInfo` 列表
- 按 id / path 查询
- text / binary 加载
- `Mesh / Model / AshAsset` 加载
- `Material` 加载
- `Mesh / Model / AshAsset` 异步加载
- `Material` 异步加载
- per-asset load state / last error
- `Mesh / Model / AshAsset / Material` 内存缓存
- worker-thread backed CPU 导入任务分发

当前资源类型识别规则新增：

- `.obj/.fbx/.gltf/.glb` -> `AssetType::Model`
- `.ashasset` -> `AssetType::Prefab`
- `.ashmat/.ashmatins` -> `AssetType::Material`

当前缓存接口：

- `load_mesh_by_id()` / `load_mesh_by_path()`
- `load_model_by_id()` / `load_model_by_path()`
- `load_material_by_id()` / `load_material_by_path()`
- `load_ashasset_by_id()` / `load_ashasset_by_path()`
- `load_mesh_by_id_async()` / `load_mesh_by_path_async()`
- `load_model_by_id_async()` / `load_model_by_path_async()`
- `load_material_by_id_async()` / `load_material_by_path_async()`
- `load_ashasset_by_id_async()` / `load_ashasset_by_path_async()`

当前 `AssetLoadState` 新增：

- `Loading`

当前异步加载路径约定：

- 由逻辑线程或其他高层线程发起 async request
- 真正的磁盘 I/O / 模型解析在 engine worker 线程池中执行
- 返回值是 `std::shared_future<std::shared_ptr<const ...>>`
- 同一个 asset 的重复 async request 会复用进行中的 in-flight `shared_future`，避免同一资源在 worker 线程池中被重复 decode/import
- 成功后资源会进入 `AssetDatabase` cache，并把 load state 更新为 `Loaded`
- 失败时会回写 per-asset last error 与 `Failed`
- 同一个 asset 在 `Failed` 后、下一次 `refresh()` 前会命中失败缓存，后续同步请求会直接失败，异步请求会直接返回 ready-null future，不再注册新的 worker job 或重复进入磁盘 IO / decode / import 热路径

当前线程安全边界：

- `Mesh / Model / AshAsset / Material` 的同步/异步加载、load state、last error、内存 cache 与 in-flight future 表已做互斥保护
- `refresh()` / `set_root_path()` 仍应视为**逻辑线程持有的管理操作**
- 当前不建议让 `refresh()` 与一批正在进行中的 async load 并发交错；以后如引入更完整的 streaming/cooking 系统，再补 generation/fence 语义

这一层的定位是：

- 给 Editor 资源浏览和轻量实例化提供统一入口
- 给 Engine 内部场景/运行时提供基础资源查询入口
- 不替代后续更完整的 asset cooking / streaming / async loading 系统

当前材质资产补充约定：

- `AssetDatabase` 现在会把 `.AshMat` / `.AshMatIns` 加载为 `std::shared_ptr<const MaterialInterface>`
- `MaterialInstance` 加载时会递归解析父材质链，并在内存里建立只读运行时对象关系
- Engine 默认 surface PBR 材质现在以真实资产形式落在 `product/assets/materials/v2/`：
  - `materials/v2/M_SurfacePBR.AshMat`
  - `materials/v2/MI_DefaultSurface.AshMatIns`
- 代码中仍保留同路径的合成 builtin fallback；`AssetDatabase` 的同步和异步材质加载都会优先读取磁盘材质，只有磁盘资产缺失时才使用 fallback
- 默认材质不依赖 Sandbox 路径，属于 Engine 公共运行时资产
- 当前正式落地的 V2 编译框架先服务于 `Surface.StaticMesh`
- `UI` 与 `PostProcess` 当前明确不走这套材质系统，而是各自维护自己的 shader / 参数组织路径

当前 `Material.*` 对象模型位于 `project/src/engine/Function/Render/Material.*`：

- `MaterialInterface`
  - 上层统一持有接口
  - 暴露 domain / blend mode / shading model / material shader / parameter / resource / sampler / base material 解析 / change version
- `Material`
  - 描述 V2 基材质定义
  - 负责声明 `materialShader`、`renderState`、`requiredCapabilities`、`staticSwitches`
  - 参数当前只支持 `float / float4`
  - 贴图类输入必须声明在 `resources`，并通过 sampler 名称引用 `samplers`
- `MaterialInstance`
  - 持有父 `MaterialInterface`
  - 只允许覆写动态参数值与资源绑定，不允许覆写 domain / blend mode / shading model / two_sided 等静态渲染属性
  - 可在本地补充或覆写 sampler definitions；最终视图按“父 + 子覆盖”合并
- 只有 `MaterialInstance(.AshMatIns)` 可以直接赋给 mesh section、`MeshComponent.material_overrides` 或 `Model.default_materials`
- 如果显式绑定解析成 `.AshMat` 基材质，运行时会报错并回退到 generated/default instance

当前 imported model 的默认材质映射补充约定：

- `Model` 现在保留 `default_materials`
- `default_materials` 以 `material_slot -> material_path` 的稳定映射保存显式默认材质
- 如果 `default_materials` 未提供某个 slot，则 `RenderAssetManager` 会在运行时为该 `MaterialSlot` 生成一个临时 `MaterialInstance`
- 生成材质的父对象固定为 `materials/v2/M_SurfacePBR.AshMat`
- 生成材质的虚拟路径固定使用 `__generated__/materials/... .AshMatIns`
- 生成材质的参数值来自导入 `MaterialSlot` 的颜色、金属度、粗糙度、法线/底色/发光纹理路径
- 所有 section 的最终解析顺序为：
  - `MeshComponent.material_overrides[material_slot]`（要求 `.AshMatIns`）
  - `Model.default_materials[material_slot]`（要求 `.AshMatIns`）
  - imported `MaterialSlot` 自动生成材质实例
  - `materials/v2/MI_DefaultSurface.AshMatIns`

当前 V2 材质文件格式约定：

- 仍使用 JSON 文本，与 `.scene` / `.ashasset` 的风格保持一致
- `version = 2`
- `.AshMat` 必须搭配 `class = "Material"`
- `.AshMatIns` 必须搭配 `class = "MaterialInstance"`
- 基材质当前用于声明：
  - `materialShader`
  - `renderState`
  - `requiredCapabilities`
  - `staticSwitches`
  - `parameters`
  - `resources`
  - `samplers`
- 实例材质当前用于声明：
  - `parent`
  - 可选本地 `samplers`
  - `overrides.parameters`
  - `overrides.resources`
- `overrides.parameters` 当前只接受 scalar / float4
- 贴图类输入统一保存为 `MaterialTextureBinding { texture_path, sampler_name }`
- sampler 必须通过名字引用；资源绑定不再接受旧式内联 sampler 对象

当前贴图运行时补充约定：

- `TextureAsset` 位于 `project/src/engine/Function/Render/TextureAsset.*`
- 这是一层给材质系统和 render asset 管理器使用的最小运行时贴图对象，不属于 Editor-facing RHI 接口
- `Renderer` / `RenderDevice` 现在显式提供 `create_texture_2d(const TextureUploadDesc&)`
- 该入口用于 sampled 2D texture 上传，不复用 `create_render_target()` 的 pass attachment 语义
- 当前上传路径支持 uncompressed 与 block-compressed 2D 纹理的 row/block pitch 校验；非压缩单 mip 可做必要的 tight repack，block-compressed / 多 mip 初始数据必须按 mip0、mip1... tight-packed
- 普通 8-bit RGBA 贴图解码后会在 CPU 侧生成完整 mip chain，并以 tight-packed mip 初始数据一次上传；HDR 贴图当前仍只上传 mip0
- runtime cooked texture 支持 2D 非数组 `.dds` 与 raw `.ktx2` 的 BC1 / BC2 / BC3 / BC4 / BC5 / BC6H / BC7 载入；BC1 / BC2 / BC3 / BC7 的 sRGB payload 会映射为真实 GPU sRGB 格式，而不是只保存 metadata；supercompressed KTX2 不在 runtime 解码，必须由 importer/offline pipeline 先转成可直接上传的 payload
- DDS/KTX2 cooked payload 解析集中在 `Function/Render/TextureCookedDecoder.*`，`TextureAsset.cpp` 只保留公共贴图入口、stb 普通图片解码和 CPU mip 生成
- `RenderAssetManager::request_texture_asset()` 在 cache miss 时不再在 render-thread 请求路径同步 decode 文件；它会先返回一个持有 fallback GPU 资源的 `TextureAsset(Loading)` 占位对象，并把 CPU decode 派发到 worker 线程
- worker decode 完成后，render-thread `finalize_pending_assets()` 或下一次 texture request 会创建真实 GPU texture，并原地更新同一个 `TextureAsset` 的 resource / metadata / `change_version`
- `MaterialRenderProxy` 会记录绑定时看到的 `TextureAsset::change_version`；如果占位贴图从 `Loading` 变为 `Ready`，或真实 resource 更新，下一帧 material proxy 会重新打包并重新绑定材质资源

当前贴图解码范围：

- 支持：`.png`、`.jpg`、`.jpeg`、`.tga`、`.bmp`、`.hdr`、`.dds`、`.ktx2`
- 普通图片解码库：`stb_image`
- `.hdr` 当前按 `RGBA32_SFLOAT` + Linear 路径处理
- `.dds` 当前支持 legacy FourCC 和 DX10 header 的 BCn cooked payload；legacy DXT1/DXT3/DXT5 会根据请求的 `TextureColorSpace` 选择 UNORM 或 sRGB GPU 格式
- `.ktx2` 当前支持 `supercompressionScheme == 0` 且 `vkFormat` 为 BCn 的 raw payload；KTX2 中的 sRGB `vkFormat` 会保留为对应 sRGB GPU 格式

当前 `RenderAssetManager` 贴图入口约定：

- `request_texture_asset(asset_path, color_space, fallback_kind)`
- 失败或不支持的贴图会告警一次、记录失败 key，并回退到 Engine 内置 fallback 纹理；后续同 key 请求会直接走 fallback，避免反复磁盘 decode
- 成功 decode 后写入 texture cache 前会再次检查同 key 是否已被其他请求填充，避免并发请求重复创建 GPU texture
- `request_sampler(desc)` 会命中一个进程级全局 sampler 池；key 是 `RenderSamplerDesc`
- sampler 池当前由 `RenderAssetManager` 持有，生命周期默认持续到引擎退出
- 未显式定义 sampler 的材质纹理绑定会统一走默认 `Repeat` sampler
- 当前内置 fallback 纹理包括：
  - 白贴图
  - 默认法线贴图
  - 黑贴图
- 贴图的磁盘解码和 GPU 上传当前仍由 `RenderAssetManager` 负责，尚未上提为 `AssetDatabase` 的正式 texture runtime 资产缓存体系

当前 render-side 材质代理补充约定：

- `RenderAssetManager` 负责桥接：
  - mesh CPU/ GPU 资源
  - `MaterialInterface`
  - `TextureAsset`
  - `MaterialSystem`
  - `MaterialRenderProxy`
- `StaticMeshRenderAsset` 只缓存共享几何和“默认 section 材质”，不缓存组件实例级 override 结果
- `MeshComponent.material_overrides` 的最终解析发生在 `RenderScene::rebuild_from_scene(...)`
- per-primitive / per-visible-section 会携带最终 `MaterialInterface` 和 CPU-only `MaterialRenderProxy` cache；GPU program、UBO、sampler 与 texture binding 只允许在 render thread 的 submit phase 准备或刷新
- `RenderScene::rebuild_from_scene(...)` 可能运行在 logic thread，因此 section 解析阶段必须保持 CPU-only，不能创建材质 UBO、纹理、sampler 或 graphics program
- submit 阶段在 proxy 缺失或材质 version / compile hash / shader 文件签名 / texture asset version 变化时，通过 `RenderAssetManager` 的 cache 准备/刷新 `MaterialRenderProxy`；shader 文件签名检查按 `MaterialRenderProxy` 节流，不能在每个可见 section、每帧都进行 filesystem probe
- `MaterialRenderProxy` 在 render thread 上缓存：
  - `Surface.StaticMesh.BasePass` 与 `DepthOnly` 两套 `MaterialResource`
  - 每材质独立的 `GraphicsProgram`
  - 由参数 block 反射驱动的材质 `UniformBuffer`
  - 解析后的资源贴图与 sampler 绑定快照
- `MaterialRenderProxy` 现在还负责：
  - 向 `MaterialSystem` 申请/缓存按 `MaterialUsageDesc` 编译出的 V2 resource template
  - 根据 shader 反射得到的 parameter block layout 打包材质参数
  - 按 `resources + samplers` 解析最终贴图/采样器绑定
  - 通过 `prepare_surface_staticmesh(...)` 收口 static mesh surface 的 binding 更新和 graphics program 准备
- `MaterialSystem` 现在会把 engine shader family host、material shader 和生成的 `Bindings.generated.hlsli` 组合成最终编译单元
- `MaterialShaderMap` 创建 resource template 时只调用 shader compile/reflection 路径收集 binding layout 和 material parameter block layout，不再为了拿 reflection 创建临时 `GraphicsProgram`
- builtin fallback 材质创建集中在 `Function/Render/MaterialBuiltins.cpp`；`Material.cpp` 继续承担材质对象、JSON 解析/序列化和运行时版本计算
- 当前静态 mesh `Surface` 使用的 engine-host shader 为：
  - `project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshBasePass.hlsl`
  - `project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshDepthOnly.hlsl`
- 当前材质拼接占位 include 属于 Engine shader 内部文件：
  - `project/src/engine/Shaders/MaterialV2/Includes/UserShader.hlsli`
  - `project/src/engine/Shaders/MaterialV2/Includes/GeneratedMaterialBindings.hlsli`
- 材质 shader 属于运行期资产，应与 `.AshMat/.AshMatIns` 同级维护；默认 surface PBR shader 为：
  - `product/assets/materials/v2/M_SurfacePBR.hlsl`
- 这样可以避免多材质、多 section、多 view 之间共享一个可变 `GraphicsProgram` 而发生状态互踩，并把 V1 `SceneSurfacePBR` 兼容路径从运行时彻底移除

### 11.4 Scene / Entity：保留 facade，内部切到 ECS-style 存储

`Scene` 的公共 facade 仍然保留 `Scene / Entity` 这套易用接口，但内部存储已切到 `entt` 驱动的 ECS-style 实现。

当前内部结构大致为：

- `entt::registry`
- `EntityId -> entt::entity` 映射
- 内部 `HierarchyComponent`
- 内部 `EntityIdComponent`
- 显式 `entity_order`

当前公共能力包括：

- entity 创建 / 查找 / 销毁
- 父子层级维护
- `reparent_entity()`
- root entity 查询
- 按组件查询实体
- scene save / load
- dirty 状态维护
- `instantiate_model()`
- `instantiate_ashasset()`
- `instantiate_asset()`

`Entity` 当前暴露的组件为：

- `Name`
- `Transform`
- `Camera`
- `Light`
- `Mesh`

其中：

- `Name` 与 `Transform` 是默认存在的基础组件
- `Camera` / `Light` / `Mesh` 是可选组件
- `MeshComponent` 当前包含：
  - `asset_path`
  - `mesh_index`
  - `material_overrides`
  - `visible`
  - `mobility`
  - `layer_mask`

当前 `material_overrides` 约定：

- 按 `material_slot` 保存 section 级材质引用覆盖
- `.scene` 与 `.ashasset` 都会保留该数组
- 该字段只保存声明式材质路径，不直接保存 render resource

同时仍保留组件 / 枚举描述接口，供 Editor 侧做反射式展示与编辑。

### 11.5 当前 prefab / scene 关系

当前推荐关系是：

- 外部模型文件 -> 导入成 `Model`
- `Model` -> 可选转换成 `AshAsset`
- `AshAsset` -> 通过 `Scene::instantiate_ashasset()` 实例化到场景
- 运行中的 `Scene` -> 保存为 `.scene/.ashscene` 这类场景文件

也就是说：

- `Model` 更偏向“导入后的源数据”
- `AshAsset` 更偏向“可重复实例化的 prefab 资源”
- `Scene` 更偏向“运行时对象集合与层级状态”

### 11.6 当前边界与后续扩展方向

这套 Asset / Scene 基础模块当前的定位是：

- **Engine 通用基础数据层**
- **Editor 可直接依赖的场景/资源 facade**
- **不是完整 gameplay/world framework**

当前明确边界：

- 这里不直接持有 GPU 资源
- 这里不直接处理动画、skin、骨骼重定向、材质编译、streaming
- 这里不把后端 RHI 类型暴露给上层

以后如果这里继续扩展，需要同步在本文档补充：

- ownership 模型
- 线程模型
- async loading / asset cooking 约束
- 动画 / skin / material asset 设计
- scene/world 生命周期规则

### 11.8 Scene 到渲染的当前主路径

当前主干已经具备：

- 逻辑 `Scene` / `Entity` / ECS façade
- `AssetDatabase` 的 CPU 资源加载
- 第一阶段线程模型（logic/render/worker）
- `Renderer` / `RenderDevice` 的高层提交流程

当前主干已经补齐了“逻辑 Scene 真正转换为 scene-driven 渲染提交”的正式桥接层。当前真实主路径是：

- `Application` 持有 `ScenePresentationSubsystem`
- 上层通过 `Application::get_scene_presentation()` 声明 `Scene + Camera + Output + 少量 per-view overrides`
- 子系统内部维护 per-scene `RenderScene` cache、`SceneView` 构建、`VisibleRenderFrame` 构建与 render-thread submit
- `RenderScene`、`SceneView`、`VisibleRenderFrame`、`SceneRenderer` 继续保留为 Engine 内部细节

当前内部 scene 渲染链仍然由下列类型构成：

- `RenderScene`
- `SceneProxy`
- `PrimitiveSceneProxy`
- `SceneView`
- `VisibleRenderFrame`
- `SceneRenderer`

当前公共 scene presentation 入口包括：

- `SceneOutputHandle`
- `SceneViewBindingHandle`
- `UISurfaceHandle`
- `SceneOutputDesc`
- `SceneCameraSelector`
- `SceneViewOverrides`
- `SceneViewBindingDesc`

当前 update / submit 阶段约定为：

- update phase：
  - 无 logic thread 时，跟在 `_on_update()` 后执行
  - 有 logic thread 时，跟在 `_on_logic_startup()` / `_on_logic_update()` 后执行
- submit phase：
  - 默认 `Application::_on_render()` 内按 `begin_frame() -> _on_render_debug() -> scene presentation submit -> _on_gui() -> end_frame()` 执行

当前第一阶段范围为：

- 只先支持静态 mesh 主链路
- render thread 支持同一帧顺序提交多个 scene view
- CPU 多线程 frustum culling
- 逻辑线程构建可见帧数据
- 渲染线程只消费不可变的 render frame 并提交 draw
- 静态网格主链路支持按 `program + render asset + section` 合批，并通过 per-instance vertex stream 传递 object-to-clip 矩阵
- 当一个 view 只有 0 或 1 个可见静态网格 draw 时，`SceneRenderer` 会跳过 batch map 构建；单 draw 情况直接逐 section 提交，并复用一个单实例 vertex buffer，以降低 Sandbox/Sponza 这类单可见 mesh 帧的固定 CPU 开销

当前明确不在第一阶段完成的系统包括：

- skeletal mesh / animation
- 完整灯光渲染
- 阴影
- 动态材质实例
- skeletal mesh / GPU-driven instancing
- occlusion culling

设计原则：

- `Scene` 继续作为逻辑世界 source of truth
- render thread 不直接读取 `Scene` / `entt`
- scene 到 render 的跨线程同步，优先通过不可变 frame packet 完成
- `Renderer` 保持通用 render facade 身份，不直接演化成 world 管理器
- scene-driven 上层入口收口到 `ScenePresentationSubsystem`

当前 `SceneRenderer` 的内部提交约定仍然是按 view 显式提交：

- `VisibleRenderFrame` 只保存 scene 可见性结果和 draw 所需的不可变数据，不再持有 `output_target`
- `VisibleRenderFrame` 中的 static mesh section 携带最终 `MaterialInterface`；`ScenePresentationSubsystem` 在 render-thread submit phase 通过 `MaterialRenderProxy::prepare_surface_staticmesh(...)` 解析并缓存 draw-time proxy
- `RenderScene` rebuild/sync 阶段会为 static mesh section 预取 CPU-only `MaterialRenderProxy`；render-thread submit 阶段只负责缺失兜底与 GPU program / texture binding preparation
- `MaterialRenderProxy` 使用 material change version、compile hash、节流后的 shader 文件签名检查、binding snapshot version 和 texture asset change version 判断脏状态；贴图仍处于 Loading 但 fallback resource/version 未变化时，不应每帧重新打包参数或重绑 program，shader 文件签名也不应退化成逐 section 的每帧 filesystem 热路径
- render thread 每次提交一个 view 时，显式提供 `SceneRenderViewContext`
- `SceneRenderViewContext` 负责描述 per-view 提交状态：
  - `output_target`
  - 可选 `depth_target`
  - 可选 `viewport` / `scissor`
  - color / depth 的 load action 与 clear value
- `SceneRenderer` 的主入口为 `SceneRenderer::render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context)`
- `SceneRenderer` 不再持有一个覆盖全场景的共享 `GraphicsProgram`
- static mesh `Surface` 现在统一走 V2 `MaterialSystem + MaterialRenderProxy` 资源模板路径
- `SceneRenderer` 当前只负责：
  - per-view render pass / depth 目标管理
  - 静态网格 batch 构建、单可见静态网格 direct section submit、instance buffer 更新和 draw 提交
  - 消费 section 的 `MaterialRenderProxy`
- 当前 `Surface.StaticMesh` shader family 仍使用 instanced vertex layout，所以单可见 draw fast path 不是非 instanced shader 路径；它只绕过 batch lookup / instance vector 构建，并继续绑定 slot 1 的单实例 buffer
- `Transparent` blend mode 已进入 V2 材质静态状态和编译键，但当前 `SceneRenderer` 的 `Surface.StaticMesh.BasePass` 仍只正式提交 `Opaque` 与 `Masked`；透明队列需要后续单独接入

当前 depth 规则为：

- 如果调用方传入 `depth_target`，则 `SceneRenderer` 直接使用该 depth，生命周期和跨 pass 语义由调用方负责
- 如果调用方不传入 `depth_target`，则 `SceneRenderer` 会按输出目标尺寸和格式获取内部 scratch depth
- 这张 scratch depth 只保证本次 scene pass 可用；如果后续 pass 需要继续读取或复用 depth，必须由调用方显式提供 depth target

第一版多 view 仍有一个明确边界：

- `viewport` / `scissor` 只约束 draw 的光栅化区域
- `RenderLoadAction::Clear` 仍然是整 attachment clear，而不是 rect clear
- 因此多个 binding 共享同一输出附件时，第一个 binding 适合 clear，后续 binding 默认应采用 preserve/load 语义

当前 V1 同步策略为：

- `Scene::get_change_version()` 专用于 render sync，不复用 `mark_clean()`
- 内部按 `Scene*` 维护 `RenderScene`
- 当 scene change version 变化或 binding 请求 refresh 时，允许按 scene 粗粒度 `RenderScene::rebuild_from_scene(...)` 退化同步
- `build_scene_view_for_camera_entity(...)` 已提供显式 camera entity 入口，`PrimaryCamera` 保留为便捷 fallback
- `Window` output 不暴露 `UISurfaceHandle`
- `Offscreen` output 通过 `UISurfaceHandle` 交给 `UIContext` 采样展示

当前上层落地状态为：

- `Sandbox` 主窗口已迁移到 `Window` output + persistent binding，不再手动驱动 `Renderer::begin_frame()` / `SceneRenderer`
- `Editor` 的 scene/game viewports 已迁移到 engine-owned offscreen outputs + `UISurfaceHandle` 展示，不再直接持有 viewport `RenderTarget`
- custom / non-scene renderer 继续走 `Renderer` 直接驱动路径；Editor 侧旧的 `CodexLogoDemoRenderer` 已经移除，不再作为参考实现保留

相关设计与接入文档见：

- `docs/ScenePresentationSubsystemGuide.md`
- `docs/EngineDeveloperGuide.md`

### 11.7 Sandbox：Engine 自维护测试工程

当前仓库新增了 `project/src/sandbox`，它是一个**独立的 Engine 侧测试可执行项目**，定位是：

- 快速验证引擎新功能，而不是把 demo/验证逻辑长期塞在 Editor 生命周期里
- 承载 Scene -> Render 集成验证
- 承载共享渲染路径 smoke test
- 作为以后继续补充 runtime test case 的落点

当前 `Sandbox` 复用了与 `Editor` 相同的 `EntryPoint.h + Application` 启动模式，但默认运行路径已经收口为**单一标准场景运行时**，不再把旧的 asset-pipeline demo、Codex logo demo、bridge smoke test 作为默认启动链路。

当前默认标准场景为：

- `product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`

Sandbox 会从 `product/assets/models/gltfs/` 下枚举 `.gltf` 模型，并在窗口内提供 `Sandbox Model` overlay 下拉框。运行时选择新模型时，Sandbox 会先销毁旧的 scene presentation binding/output，再 reset 旧标准场景状态并异步加载新模型；新模型 ready 后才重新注册窗口 output 和 primary-camera binding，避免切换后继续提交旧场景实体。
标准场景会保留 glTF 导入得到的材质槽和贴图绑定，不再为验证目的向 mesh 注入固定 debug material override；V2 材质链路由 glTF 默认材质生成的 `.AshMatIns` 覆盖。

当前标准场景路径会真实走通：

- `AssetDatabase` 异步模型加载
- `Scene::instantiate_model()`
- 逻辑线程相机更新
- `Sandbox` 在标准场景 ready 后声明一个 `Window` output 和一个 scene binding；模型切换期间会临时销毁该 binding/output
- `ScenePresentationSubsystem` 在 update phase 内完成 `RenderScene` / `SceneView` / `VisibleRenderFrame` 准备
- render thread 在 scene presentation submit phase 内构造 `SceneRenderViewContext` 并提交
- 最终通过正常 present 路径显示到屏幕

当前 `Sandbox` 的默认人工交互控制为：

- `W / A / S / D`：平移
- `Q / E`：下降 / 上升
- 按住右键：鼠标视角
- 滚轮：移动速度
- `Shift`：加速

当前 `Sandbox` 还是第一阶段线程模型的首个落地用例：

- `EngineInitConfig.threading.enable_logic_thread = true`
- render thread 保持负责 `Renderer` 帧循环
- logic thread 负责场景加载、自由相机更新与 scene presentation declaration/update
- render thread 只消费 prepared packets 并提交 draw
- startup 完成后，仍会通过 `ASH_ENQUEUE_RENDER_COMMAND` 向 render thread 回投一条确认消息

当前内置 glTF 样例资产位于：

- `product/assets/models/gltfs/Avocado/glTF/Avocado.gltf`
- `product/assets/models/gltfs/BoomBox/glTF/BoomBox.gltf`
- `product/assets/models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf`
- `product/assets/models/gltfs/Sponza/glTF/Sponza.gltf`

当前版本的标准场景路径不再默认写出旧的 prefab / generated-scene 中间产物；Sandbox 运行验证和后续专项测试扩展生成物统一写入 `Intermediate/test-reports/sandbox/`。

推荐运行方式：

```powershell
product/bin64/Debug-windows-x86_64/Sandbox.exe --smoke-test-seconds=5
```

Base 层自测方式：

```powershell
product/bin64/Debug-windows-x86_64/Sandbox.exe --engine-self-test
```

当前 self-test 覆盖 `H_ASSERT` 语句安全性、typed allocator 对齐、`StackAllocator::free_marker()`、`LinearAllocator::deallocate()`、`Array` 扩容与初始尺寸、`file_delete()` / file text helper 返回值、`AshSubresourceRange::resolve()`、`AshBarrier` value 语义、shader pool source hash、RGBA8 texture mip 生成、DDS/KTX2 cooked texture decode、DX12 validation build-type gating、glTF indexed primitive 顶点复用，以及 DX12 per-subresource tracker 的混合状态回归。它应保持 headless，不应创建窗口、RHI device 或加载场景资产，生成物应写入 `Intermediate/test-temp/engine/`。

维护约定：

- 以后需要快速验证 Engine 功能时，优先把验证接到标准场景路径或往 `Sandbox` 增加专项模式，而不是继续把临时 demo 塞进 `Editor`
- 如果是共享渲染/资源路径改动，建议至少用 `Sandbox` 跑一遍 Vulkan + DX12
- `Sandbox` 中的 shader 文件默认作为源码资源存在，不作为 VS 的 FXC build step；运行期仍按相对路径从仓库读取

---

## 12. 日志、验证、调试与泄露定位

### 12.1 日志

当前日志输出路径：

- `product/logs/AshEngineLogFile_*.logfile`
- `product/logs/AshAppLogFile_*.logfile`

日志系统在 `Base/hlog.cpp` 中初始化。

### 12.2 Vulkan Validation / DX12 Validation

当前 validation 开关来自 `product/config/Engine.ini`。

建议配合本地 skill `ash-engine-validation-loop` 做最终验收，它会执行：

- Premake 重新生成
- 编译
- `Sandbox` 在 Vulkan 下运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- `Sandbox` 在 DX12 下运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- `Editor` 在 Vulkan 下运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- `Editor` 在 DX12 下运行 25 秒，并通过 `ASH_ENGINE_SMOKE_TEST_SECONDS` 走引擎内的优雅退出路径
- 校验 validation / debug layer / 泄露 / backend 错配

Vulkan 支持：

- 开关
- GPU Assisted
- Synchronization Validation
- BreakOnValidationError

DX12 支持：

- Debug Layer
- GPU Validation
- D3D12 debug layer warning / error / corruption 通过 `ID3D12InfoQueue1` 回调直接进入引擎日志；如果运行环境不支持该接口，则退化为 `ID3D12InfoQueue` 轮询 drain
- DXGI debug 消息通过 `IDXGIInfoQueue` 在 swapchain create / resize / present / destroy 节点写入引擎日志
- 重复的 DX12 / DXGI debug 消息会被收敛，避免每帧写入同一条 warning
- 对会执行 `RenderLoadAction::Clear` 的 color/depth render target，优先在 `RenderTargetDesc` 中配置匹配的 optimized clear value；否则 DX12 可能给出 clear-value warning
- 对带 `initial_data` 的 GPU-only buffer，立即上传必须使用独立的临时 command allocator / command list；不要复用当前帧正在录制的 allocator，否则会命中 `ID3D12CommandAllocator::Reset` debug-layer 错误

运行验证注意事项：

- `Sandbox.exe` / `Editor.exe` 实际加载的是 `product/bin64/<Config>-windows-x86_64/Engine.dll`
- 仅确认 `_BUILD/.../Engine.dll` 已编出还不够；如果 `product/bin64/.../Engine.dll` 没同步，运行时可能仍在执行旧引擎代码
- 当出现“源码已改、构建已过，但运行现象完全没变化”的情况，优先检查：
  - `product/bin64/.../Engine.dll` 与 `_BUILD/.../Engine.dll` 的时间戳 / 哈希是否一致
  - 是否仍有残留 `Editor.exe` / `Sandbox.exe` 进程占用旧 DLL，导致 postbuild copy 失败

### 12.3 RHI Debug Name 与 Tracy Profiling

Debug 构建下，RHI 资源名必须真正下沉到 native GPU object，而不是只停留在 Engine 对象字段里：

- DX12 后端统一通过 `dx12_set_debug_name()` 调用 `ID3D12Object::SetName()`；新建 resource / PSO / root signature / descriptor heap / command queue / command allocator / fence / command list / swapchain back buffer 时都应设置可读名称
- Vulkan 后端统一通过 `VulkanContext::set_resource_name()` 写入 debug utils 名称；新建 buffer / buffer view / image / image view / framebuffer / render pass / pipeline / pipeline layout / shader module / sampler 时都应设置可读名称
- 如果 creation desc 里的 `const char* name` 可能来自临时字符串，后端对象必须自己持有 `std::string`，再把 desc 内部指针改成 owned storage 的 `c_str()`；不要缓存外部裸指针
- `set_resource_name()` / `dx12_set_debug_name()` 必须容忍空 handle、空字符串和未启用 validation/debug-utils 的运行环境

CPU profiling 使用 `Base/hprofiler.h` 的 Tracy facade。新增打点时遵守以下粒度：

- 在 `.cpp` 中 include `Base/hprofiler.h`，不要把 Tracy 头传播到公共头
- 优先覆盖 hot path 边界：frame begin/end、present、resource barrier submit、pass begin/draw submit、program binding、descriptor update、pipeline apply/create
- 对批量操作使用 `ASH_PROFILE_SCOPE_VALUE()` 记录 count，对 program / pipeline 这类有名字的对象用 `ASH_PROFILE_SCOPE_TEXT()` 附加名称
- 避免在每个资源/每个 draw 内部无差别制造大量短 zone；确实需要细分时，应先确认 Tracy 视图里当前粒度仍无法定位瓶颈
- `TRACY_ENABLE` 未定义时所有 profiling 宏必须保持 no-op，不应改变运行逻辑

当前默认验收基线：

- 只要改动涉及 Engine 共享路径、Renderer、RenderDevice、Scene/Asset、Application 生命周期、DynamicRHI、配置、日志、验证、UI 后端或任一双后端共享抽象，就不再只跑 `Sandbox`
- 默认需要同时验证：
  - `Sandbox + Vulkan`
  - `Sandbox + DX12`
  - `Editor + Vulkan`
- `Editor + DX12`

### 12.4 Runtime Frame Stats Overlay

- Engine 内建一套轻量 frame-stats 统计：
  - frame size
  - draw call count
  - graphics pass count
  - compute dispatch count
  - instantaneous CPU frame time / FPS
  - moving-average CPU frame time / FPS
- 统计由 `Renderer` 维护，并通过引擎侧 `UIContext` 在窗口左上角绘制一个常驻 overlay。
- 这层 overlay 属于 Engine runtime debug UI，不需要 Editor 自己实现，也不会暴露 backend-specific UI 细节。
- 只有当改动可以被严格证明为“单后端私有”或“单可执行项目私有”时，才可以缩小验证矩阵

### 12.5 Vulkan VMA 泄露定位

当前 Vulkan 侧已经有基于宏开关的 VMA 泄露跟踪能力，关键实现位于：

- `Graphics/Vulkan/VulkanContext.h`
- `Graphics/Vulkan/VulkanContext.cpp`

当前能力包括：

- 分配点记录
- 释放点 untrack
- allocator shutdown 前 dump live allocations
- 可选 stack trace 采集

如果调整这套宏、日志格式或采样方式，需要更新本文档。

### 12.6 调试建议

遇到渲染 / validation / 泄露问题时，优先顺序建议：

1. 看 `product/logs`
2. 按 backend 打开 validation
3. RenderDoc 抓帧看资源内容、layout、pass 输出
4. Vulkan 问题看 resource tracker / barrier 位置
5. 泄露问题看 VMA leak dump

### 12.7 当前 Scene -> Render 垂直切片的最近修复点

在当前 `Sandbox` 的 `SceneRenderFlowSmoke` 验证链路中，最近确认并修复过两类容易回归的问题：

- Vulkan root constants 共享语义：
  - 旧行为会把 `SceneStaticMesh` shader 中的 `AshRootConstants` 误当成普通 descriptor resource，进而报“C++ 未绑定”
  - 当前已改为 Vulkan 编译前 rewrite 成真正的 push constants 语义
- Vulkan staging buffer 池回收：
  - `VulkanStagingBufferPool::alloc_buffer()` 新建 buffer 时必须补齐 `PACK_BUFFER_ITEM.u64DeviceSize`
  - 若遗漏该字段，回收到 staging pool 时会命中 `free_buffer()` 的 `u64DeviceSize` 断言
- Vulkan / DX12 clear color tag 存储：
  - `AshColorValue` 现在是 `v_type + payload union` 的 tagged 结构，不再把 `v_type` 与 `float32/int32/uint32` 共用同一层 `union`
  - Vulkan clear 值转换依赖 `v_type`，因此填充 clear color 时必须通过正确构造或显式维护 tag
- Vulkan descriptor set 生命周期 / pool 复用：
  - `VulkanRenderProgram` 不再在同一个 in-flight frame slot 内反复覆写单个 `VkDescriptorSet`；每次 resource binding 会保留独立 descriptor set 到当前 in-flight frame bucket，避免 command buffer 仍引用旧 set 时被 update / destroy
  - `VulkanDescriptorPool` 现在区分“仍被高层对象持有的 live set 数”和“尚未真正归还给 Vulkan pool 的 resident set 数”；延迟 free 尚未 flush 前会避免错误复用已耗尽 pool，从而规避 `VK_ERROR_OUT_OF_POOL_MEMORY` 和 shutdown 期的 descriptor-pool validation
  - 当最后一个 deferred descriptor-set free 释放掉 pool 的最终 `shared_ptr` 时，`VulkanDescriptorPool` 会立刻执行 `vkDestroyDescriptorPool`；不要再把 pool 析构本身做第二次 frame-queue 延迟，否则 shutdown 时可能把 destroy 落到错误的 deletion queue，重新触发 `VUID-vkDestroyDevice-device-05137`
  - `VulkanDescriptorSetLayout` 的全局 layout cache 现在集中在一个 cpp-local owner 中，并由 mutex 保护；创建路径、过期 weak entry 清理和 `VulkanContext::shutdown()` 触发的 cache shutdown 都必须走这个 owner，shutdown 时会清空 cache map，不能把旧 device 的 layout 复用到下一次 context 初始化，也不能再散落新的 static map / set
- Vulkan sampler cache：
  - 固定枚举大小的 sampler cache 使用 `std::array`，不再使用项目自研 `Array`；后续只有 frame pool、command buffer queue 这类明确依赖底层 allocator / 固定帧资源语义的路径才保留自研容器
- Asset / Material 架构收口：
- `AssetDatabase` 对 Mesh / Model / Material / AshAsset async load 维护 in-flight `shared_future` 表，重复请求会复用同一个后台任务，失败结果会缓存到下一次 `refresh()`
- `.AshAsset` JSON 读写与层级重建集中在 `Function/Asset/AshAssetSerializer.cpp`；`AssetData.cpp` 继续承担 OBJ/glTF/FBX 模型导入、mesh/model 基础方法和模型到 AshAsset 的转换
- `MaterialShaderMap` 的 resource template 生成改为直接消费 shader reflection artifact，避免为了读取 reflection 创建临时 graphics program
- `RenderScene` sync 阶段预取 CPU-only material proxy，render submit 只在 proxy 缺失、材质版本变化、节流后的 shader 文件签名变化或贴图资源版本变化时准备 GPU program / binding

---

## 13. 当前公共开发约束

### 13.1 不要直接把后端对象暴露给上层

高层接口应通过 Function 层包装对象暴露，不要要求 Editor / Game 直接持有：

- `Vk*`
- `ID3D12*`
- `RHI::*` 后端细节对象

### 13.2 改动共享渲染路径时，优先看 Vulkan 合法性

原因不是 Vulkan 更重要，而是它对 barrier / render pass 区间约束更严格。共享设计如果先满足 Vulkan，通常更不容易在 DX12 上走偏。

### 13.3 配置、路径、运行目录变更要同步文档

尤其是：

- `product/config`
- `product/logs`
- `product/caches`
- `product/bin64/<Config>-windows-x86_64`

### 13.4 专题文档可以补充，不要替代总文档

可以继续新增专题文档，但不应让核心事实只存在于零散专题里。公共开发者首先应能从本文档理解当前主干引擎。

---

## 14. 推荐的文档更新方式

每次做完 Engine 任务后，按下面顺序检查是否需要回写文档：

1. 是否改了公共 API 或分层边界
2. 是否改了配置项、目录、构建产物位置
3. 是否改了渲染路径、状态转换、资源绑定规则
4. 是否改了 UI 分层或 Editor 可见边界
5. 是否需要增加一个新的专题文档

若答案为“是”，至少更新：

- `docs/EngineDeveloperGuide.md`

若还影响 Editor 使用方式，再更新：

- `docs/EditorDeveloperGuide.md`

---

## 15. 当前相关文档索引

- Engine 总览：`docs/EngineDeveloperGuide.md`
- Editor 总览：`docs/EditorDeveloperGuide.md`
- Engine UI 分层：`docs/EngineUIContext.md`
- Scene presentation 接入：`docs/ScenePresentationSubsystemGuide.md`
