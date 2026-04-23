# AshEngine Editor 开发指南

> 面向 Editor 层新开发者，帮助你快速理解项目架构、可用 API 与开发注意事项。

> 维护约定：
> - 本文档是 **Editor 侧长期维护主文档**。
> - Editor 工作流、Editor 可直接使用的 Engine 接口、Editor 集成方式变化时，应同步更新本文档。
> - Engine 架构、Runtime、RHI、渲染底层、DynamicRHI、Shader 编译与缓存等变化，请同步更新 `docs/EngineDeveloperGuide.md`。
> - 若某次改动同时影响 Engine 与 Editor 边界，应同时更新两份文档。
> - 开始新的 Editor 开发任务前，先阅读 `docs/README.md`、`docs/editor/README.md` 与本文档；若涉及 UI、Engine / Editor 边界或历史问题，再补读对应专题文档。
> - 若本次协作涉及项目背景、目录边界、命令、验证方式、提交流程，请同步参考 `docs/EditorContributorGuide.md`。
> - 若本次协作涉及主线程拆任务、多子线程并行、模块进度回写，请同步参考 `docs/EditorParallelCollaboration.md` 与对应模块进度文档。
> - 若本次协作涉及阶段目标、里程碑和任务卡，请同步参考 `docs/EditorTaskPlanning.md`。
> - 每次完成功能实现后，在提交或交付前至少回写本文档；若影响专题内容，再同步更新对应专题文档。

---

## 1. 项目架构总览

```
AshEngine/HASHEAEngine/
├── premake5.lua                     ← 顶层构建配置 (Premake5)
├── project/
│   ├── src/
│   │   ├── engine/                  ← Engine 层 (你不需要修改)
│   │   │   ├── Base/                ← 基础设施 (日志、内存、窗口、数据结构)
│   │   │   ├── Function/            ← Function 层 ★ Editor 唯一应依赖的 Engine 接口
│   │   │   │   ├── Application.h    ← 应用基类
│   │   │   │   └── Render/
│   │   │   │       ├── RenderDevice.h  ← 底层渲染命令
│   │   │   │       └── Renderer.h      ← 高层渲染器 (推荐使用)
│   │   │   ├── Graphics/            ← RHI 层 (Vulkan 等后端实现, Editor 不应直接使用)
│   │   │   └── EntryPoint.h         ← main() 入口与应用工厂
│   │   └── editor/                  ← Editor 层 ★ 你的工作目录
│   │       ├── Editor.h / .cpp      ← Editor 主类
│   │       ├── Shaders/             ← Editor 专属 Shader (HLSL)
│   │       └── premake5.lua         ← Editor 构建配置
│   └── thirdparty/                  ← 第三方库 (ImGui, entt, glm 等)
└── product/                         ← 构建输出、运行目录与运行期资产
```

### 层级边界规则

| 层级 | 命名空间 | Editor 能否使用 |
|------|----------|:---:|
| **Function 层** (`Function/`) | `AshEngine` | **可以** |
| **Base 层** (`Base/`) | `AshEngine` | 部分可以 (日志、Service 等) |
| **RHI/Graphics 层** (`Graphics/`) | `RHI` | **禁止** |

**核心原则：Editor 只通过 Function 层的公开接口与 Engine 交互。** 不要 `#include` 任何 `Graphics/` 目录下的头文件。

---

## 2. 编译与构建

### 构建系统

项目使用 **Premake5** 生成 Visual Studio 解决方案 (`.sln`)，而非 CMake。

- Engine 编译为 **SharedLib (DLL)**，通过 `ASH_ENGINE` 宏控制符号导出 (`dllexport`)
- Editor 编译为 **ConsoleApp (EXE)**，链接 Engine DLL，通过 `dllimport` 使用引擎接口

### 重要预处理宏

| 宏 | 含义 | 在 Editor 中 |
|---|---|---|
| `ASH_ENGINE` | Engine DLL 内部编译 | **未定义** |
| `ASH_EDITOR` | Editor 项目编译 | **已定义** |
| `ASH_DEBUG` | Debug 配置 | 视配置而定 |
| `ASH_VULKAN` | 启用 Vulkan 后端 | 全局定义 |

### `ASH_ENGINE` 的影响

由于 Editor 中**没有** `ASH_ENGINE`，以下方法对你**不可见**（也不应使用）:

```cpp
// 这些方法被 #if defined(ASH_ENGINE) 保护, Editor 中无法调用:
Application::get_graphics_context()  // ✗ 不可用
Application::get_swapchain()         // ✗ 不可用
Application::get_render_device()     // ✗ 不可用

// 以下方法在 Editor 中可用:
Application::get_window()                 // ✓ 可用
Application::get_renderer()               // ✓ 可用 ← generic/custom rendering 入口
Application::get_scene_presentation()     // ✓ 可用 ← scene-driven viewport 声明入口
```

### 构建步骤

1. 用 Premake5 生成 VS 解决方案: `premake5 vs2022`（在 `HASHEAEngine/` 根目录）
2. 打开 `AshEngine.sln`，启动项目为 `Editor`
3. 构建输出至 `product/bin64/<Config>/Editor.exe`

### 开发工作流要求

1. 开始实现前先阅读：
   - `docs/README.md`
   - `docs/EditorDeveloperGuide.md`
   - 若本次涉及 UIContext / Editor 边界 / 历史设计问题，再补读相关专题文档
2. 修改 Editor 代码或构建配置后：
   - 运行 `premake5 vs2022`
   - 再编译 `Editor` 目标确认无回归
   - 若改动的是运行时 Editor UI，优先使用 `UIContext`，不要在活跃运行路径里重新引入 `ImGui::` / `imgui.h`
3. 完成功能后：
   - 至少更新本文档
   - 若涉及 Engine / Editor 协作边界，同步更新 `docs/EngineDeveloperGuide.md` 或对应专题文档

### Editor 可用的第三方库 (已在 premake 中 include)

| 库 | 用途 | 头文件路径 |
|---|---|---|
| **ImGui** | 编辑器 UI | `thirdparty/ImGui/` |
| **entt** | ECS 实体组件系统 | `thirdparty/entt/include/` |
| **glm** | 数学库 | `thirdparty/glm/include/` |
| **spdlog** | 日志（通过 HLog 宏使用） | `thirdparty/spdlog/include/` |
| **GLFW** | 窗口/输入 | `thirdparty/GLFW/include/` |

---

## 3. 应用生命周期

### 入口点模式

Engine 通过 `EntryPoint.h` 中的 `main()` 驱动应用。Editor 需要实现两个工厂函数：

```cpp
// Editor.cpp 底部 — 必须实现
AshEngine::Application* create_application()
{
    AshEngine::EngineInitConfig config{};
    config.initWidth = 1920;
    config.initHeight = 1080;
    config.title = "Ash Engine Editor";
    config.bVsync = false;
    config.swapchainBufferCount = 3;
    return new AshEditor::Editor(config);
}

void destroy_application(AshEngine::Application* app)
{
    delete app;
}
```

### 主循环生命周期

```
main()
  ├── create_application()  → 初始化窗口、GraphicsContext、Swapchain、RenderDevice、Renderer
  ├── Application::start()  → 进入主循环:
  │     while (!window->should_close())
  │     {
  │         _on_update()         // ① 逻辑更新 (输入处理、ECS 更新等)
  │         if (!window->is_minimized())
  │         {
  │             _on_render()     // ② 渲染 (包含 begin_frame / end_frame)
  │             _present()       // ③ 呈现到屏幕
  │         }
  │     }
  └── destroy_application()  → 逆序销毁所有资源
```

### 你需要 Override 的虚方法

```cpp
class Editor final : public AshEngine::Application
{
protected:
    // ① 每帧逻辑更新 — 处理输入、更新 ECS、处理 Editor 命令
    auto _on_update() -> void override;

    // ② ImGui 绘制 — 在这里绘制所有编辑器面板
    auto _on_gui() -> void override;

    // ③ 调试渲染 — Gizmo、辅助线框等
    auto _on_render_debug() -> void override;

    // ④ 主渲染 — 通常保留基类默认顺序:
    // begin_frame -> _on_render_debug -> scene presentation submit -> _on_gui -> end_frame
    auto _on_render() -> void override;

    // ⑤ 呈现 — 通常直接调用 AshEngine::Application::_present()
    auto _present() -> void override;
};
```

**重要**:

- `_on_update()` 中务必调用 `AshEngine::Application::_on_update()` 以确保窗口事件被正确处理。
- 如果你走的是 scene-driven viewport 主路径，`_on_render()` / `_present()` 通常应直接保留基类默认实现。

---

## 4. 渲染 API 使用指南

当前 Editor 有两条渲染路径：

- scene-driven 3D viewport 主路径：通过 `ScenePresentationSubsystem`
- generic/custom rendering：继续通过 `Renderer`

因此当前建议是：

- 如果你在做 generic/custom pass、compute、后处理、调试渲染，继续看本章 `Renderer` 用法
- 如果你在做 scene-driven viewport / game view / 多相机视图，优先使用 `ScenePresentationSubsystem`

相关接入说明见：

- `docs/ScenePresentationSubsystemGuide.md`
- `docs/EngineDeveloperGuide.md`
- `docs/EngineUIContext.md`

### Scene-driven viewport 主路径

当前 `Scene` / `Game` 视口的真实边界是：

- `EditorViewportService` 在 update 阶段维护每个 viewport 的 requested size、panel open、primary viewport 状态
- service 通过 `Application::get_scene_presentation()` 为每个 viewport 创建或更新一个 `Offscreen` output 和一个 persistent binding；这两个句柄只保留在 service 内部，不再暴露给 UI-facing `EditorViewportInstance`
- `ViewportPanel` 只负责 UI 语义和 `UISurfaceHandle` 展示，不再直接持有 viewport `RenderTarget`
- `Editor::_on_render()` / `_present()` 保持基类默认实现，Editor 不再自己 `begin_frame()` / `end_frame()` / `SceneRenderer::render_visible_frame(...)`

典型 scene-driven 视口声明如下：

```cpp
auto* scene_presentation = AshEngine::Application::get_scene_presentation();

AshEngine::SceneOutputDesc output_desc{};
output_desc.debug_name = "EditorSceneViewport";
output_desc.kind = AshEngine::SceneOutputKind::Offscreen;
output_desc.width = requested_width;
output_desc.height = requested_height;

const AshEngine::SceneOutputHandle output = scene_presentation->create_output(output_desc);

AshEngine::SceneViewBindingDesc binding_desc{};
binding_desc.debug_name = "EditorSceneViewportPrimaryCamera";
binding_desc.scene = &active_scene;
binding_desc.camera.source = AshEngine::SceneCameraSource::PrimaryCamera;
binding_desc.output = output;
binding_desc.enabled = true;

const AshEngine::SceneViewBindingHandle binding = scene_presentation->create_view_binding(binding_desc);
const AshEngine::UISurfaceHandle surface = scene_presentation->get_ui_surface(output);

ui->draw_surface_fill_available(surface, preserve_aspect);
```

### 获取 Renderer

```cpp
auto* renderer = AshEngine::Application::get_renderer();
```

### 4.1 资源创建

所有资源通过 Renderer 创建，返回智能指针，资源生命周期由引用计数管理。

```cpp
// RenderTarget (纹理/渲染目标)
auto rt = renderer->create_render_target({
    .width = 1920,
    .height = 1080,
    .format = AshEngine::RenderTextureFormat::RGBA16_SFLOAT,
    .shader_resource = true,    // 可作为 shader 输入
    .unordered_access = false,  // 可读写 (Compute)
    .name = "SceneColor"
});

// Uniform Buffer
auto ubo = renderer->create_uniform_buffer({
    .size = sizeof(MyConstants),
    .cpu_write = true,
    .initial_data = &myData,
    .name = "EditorConstants"
});

// Vertex / Index / Storage Buffer — 类似模式
auto vb = renderer->create_vertex_buffer({...});
auto ib = renderer->create_index_buffer({...});
auto sb = renderer->create_storage_buffer({...});
```

### 4.2 Shader Program 创建

Engine 使用 **HLSL** 作为着色器语言，通过 DXC 编译为 SPIR-V。

```cpp
// Graphics Program (顶点 + 像素着色器)
auto program = renderer->create_graphics_program({
    .shader_path = "project/src/editor/Shaders/MyShader.hlsl",
    .vertex_entry = "VSMain",       // 顶点着色器入口点
    .fragment_entry = "PSMain",     // 像素着色器入口点
    .shader_macro = nullptr,        // 可选: "FOO=1;BAR=2"
    .state = {
        .cull_mode = AshEngine::RenderCullMode::Back,
        .primitive_topology = AshEngine::RenderPrimitiveTopology::TriangleList,
        .depth_test = true,
        .depth_write = true,
    },
    .name = "SceneForward"
});

// Compute Program
auto compute = renderer->create_compute_program({
    .shader_path = "project/src/editor/Shaders/MyCompute.hlsl",
    .compute_entry = "CSMain",
    .name = "PostProcess"
});
```

### 4.3 资源绑定

通过 Program 按名称绑定资源（名称对应 HLSL 中的变量名）：

```cpp
program->set_uniform_buffer("SceneConstants", ubo);
program->set_texture("BaseColorTex", texture_rt);
program->set_sampler("LinearSampler", AshEngine::RenderSamplerState::Default);
program->set_storage_buffer("LightBuffer", light_sb);

// 修改渲染状态
program->apply_render_state([](AshEngine::GraphicsProgramState& state) {
    state.cull_mode = AshEngine::RenderCullMode::None;
    state.depth_write = false;
});
```

### 4.4 渲染流程 (一帧)

以下示例适用于 generic/custom rendering 路径，不是 scene-driven viewport 主路径：

```cpp
auto Editor::_on_render() -> void
{
    auto* renderer = AshEngine::Application::get_renderer();
    if (!renderer || !renderer->begin_frame())
        return;

    // --- Compute Pass (不需要 begin_pass) ---
    AshEngine::ComputeDispatchDesc dispatch{};
    dispatch.program = m_compute.get();
    dispatch.group_count_x = (width + 7) / 8;
    dispatch.group_count_y = (height + 7) / 8;
    renderer->dispatch(dispatch);

    // --- Graphics Pass ---
    auto back_buffer = renderer->get_back_buffer();
    AshEngine::PassDesc pass{};
    pass.name = "MainScenePass";
    pass.color_attachments.push_back({
        back_buffer,
        AshEngine::RenderLoadAction::Clear,
        { 0.1f, 0.1f, 0.1f, 1.0f }  // 清屏颜色
    });
    pass.depth_attachment = {
        m_depth_target,
        AshEngine::RenderLoadAction::Clear,
        { 1.0f, 0 }
    };

    AshEngine::Renderer::GraphicsPassContext pass_ctx;
    if (renderer->begin_pass(pass, pass_ctx))
    {
        AshEngine::GraphicsDrawDesc draw{};
        draw.program = m_program.get();
        draw.vertex_buffers = {{ 0, m_vb, 0 }};
        draw.index_buffer = m_ib;
        draw.index_count = m_index_count;
        pass_ctx.draw(draw);
        pass_ctx.end();  // ★ 必须显式调用 end()
    }

    renderer->end_frame();
}
```

### 4.5 关键注意事项

- **scene-driven viewport 主路径通常不应手动 `begin_frame()` / `end_frame()` / `present()`** — 保留 `Application::_on_render()` / `_present()` 默认流程
- **generic/custom rendering 需要保持** `begin_frame()` / `end_frame()` / `present()` 对称
- **Pass 必须显式关闭**: 调用 `pass_ctx.end()` 结束当前 Pass
- **GraphicsPassContext 不可复制**, 只能移动
- **Transient RenderTarget**: 临时使用的 RT 应使用 `acquire_transient_render_target()` / `release_transient_render_target()`，避免显存浪费
- **资源状态转换由引擎自动处理**: 你不需要手动插入 barrier
- **窗口最小化时不渲染**: 基类已经处理此逻辑 (`is_minimized()`)

---

## 5. 日志系统

Engine 使用 spdlog，并封装了便捷宏：

```cpp
#include "Base/hlog.h"

HLogInfo("Panel opened: {}", panel_name);
HLogWarning("Asset not found: {}", path);
HLogError("Failed to create render target: {}x{}", w, h);
HLogTrace("Debug trace info...");
```

**注意**: 在 Editor 中 (`ASH_ENGINE` 未定义)，日志走 `app_logger` 通道，和 Engine 内部日志区分。

---

## 6. Service 系统

Engine 提供了一个轻量的 Service Manager，可用于注册全局服务：

```cpp
#include "Base/hserviceManager.h"
#include "Base/hservice.h"

// 定义你的 Service
struct MyEditorService : AshEngine::Service {
    static constexpr const char* k_name = "my_editor_service";
    ASH_DECLARE_SERVICE(MyEditorService);

    auto init(void* config) -> bool override { /* ... */ return true; }
    auto shutdown() -> bool override { /* ... */ return true; }
};

// 获取 (首次调用会自动注册)
auto* svc = AshEngine::ServiceManager::get<MyEditorService>();
```

适用场景：Editor 的全局管理器（如 SelectionService、UndoService、AssetDatabase 等）。

---

## 7. 着色器开发

### 位置

Editor Shader 放在 `project/src/editor/Shaders/` 目录下。

### 语言与约定

- 使用 **HLSL** (Shader Model 6.x)
- 通过 DXC 编译为 **SPIR-V** (Vulkan 后端)
- 入口点默认命名: `VSMain`, `PSMain`, `CSMain`
- Shader 路径是**相对于引擎根目录**的 (即 `HASHEAEngine/` 所在目录)

### 资源绑定示例 (HLSL)

```hlsl
// 对应 C++ 中: program->set_uniform_buffer("SceneData", buffer);
cbuffer SceneData : register(b0) { float4x4 viewProj; };

// 对应: program->set_texture("Albedo", rt); + program->set_sampler("LinearSampler", ...);
Texture2D<float4> Albedo;
SamplerState LinearSampler;

// 对应: program->set_rw_texture("Output", rt);  (Compute shader)
RWTexture2D<float4> Output;

// 对应: program->set_storage_buffer("Lights", sb);
StructuredBuffer<float4> Lights;
```

---

## 8. 自定义渲染路径

Editor 侧旧的 `CodexLogoDemoRenderer` 已经从仓库移除，不再作为示例文件保留。

如果后续需要新增一条 custom / non-scene 渲染路径，建议直接基于引擎 `Renderer` 能力实现一个新的独立模块，并遵守以下约束：

1. **和正式 scene-driven viewport 主路径解耦**
2. **不要重新把 demo renderer 挂回 Editor 正式主循环**
3. **UI 展示仍通过 `UIContext` / `UISurfaceHandle` 接入**
4. **需要新的可复用能力时，优先补 Engine / UIContext 正式接口**

---

## 9. Editor 开发路线建议

### 推荐的模块划分

```
editor/
├── Editor.h / .cpp              ← 主类, 管理生命周期
├── Core/                        ← 共享命令与编辑器上下文
├── Panels/                      ← 各 UI 面板
│   ├── SceneHierarchyPanel.h    ← 场景层级树
│   ├── InspectorPanel.h         ← 属性检查器
│   ├── ViewportPanel.h          ← 3D 视口
│   ├── AssetBrowserPanel.h      ← 资源浏览器
│   └── ConsolePanel.h           ← 日志/控制台
├── Services/                    ← Selection / Scene / UndoRedo / Viewport 等服务
├── Shaders/                     ← Editor 专属 Shader
└── premake5.lua
```

历史遗留的 `project/src/editor/Scene/**` 已归档到 `docs/editor/legacy-scene-runtime/`，不再保留在活跃源码树内。

### 关于 ImGui

ImGui 仍然是底层实现，但 **运行时 Editor 不应直接依赖原生 ImGui 接口**。当前约定如下：

- Engine 内部拥有 ImGui 后端初始化与平台 / RHI 绑定。
- Editor 运行时面板、菜单、dockspace、视口预览一律通过 `UIContext` 调用。
- `project/src/editor/ImGui/EditorImGuiLayer.*` 与 `project/src/editor/ImGui/EditorStyle.*` 仅保留为历史参考实现，已在 `project/src/editor/premake5.lua` 中排除，不属于当前运行时路径。
- 如果 Editor 新需求缺少某个“可复用、立即模式、后端无关”的 UI 原语，应先补到 `UIContext`，而不是在 Editor 里重新 `#include "imgui.h"`。

### 当前 Editor UI 组织方式

当前运行时编辑器工作区已经恢复为基于 `UIContext` 的 dockspace 布局：

- `EditorApplication` 负责创建 dockspace host window，并通过 `dock_builder_*` 构建默认布局。
- `ViewportPanel` 只负责呈现某个 `EditorViewportInstance`，不直接拥有全局唯一视口状态。
- `EditorViewportService` 管理多视图实例，并在内部维护 scene presentation 的 output / binding 生命周期；面板层只消费 `UISurfaceHandle`。
- `Editor::_on_update()` 通过 `EditorApplication` 同步 scene presentation 声明，`Editor::_on_render()` / `_present()` 保持基类默认帧流程，不再自己编排 `SceneRenderer`。
- `EditorViewportService::get_viewports()` 会按稳定顺序发布 `scene -> game -> auxiliary`，避免 `unordered_map` 迭代顺序把渲染和 UI 排序变成非确定行为。
- `Window -> Reset Layout` 会重建默认 dock graph，而不是再用早期的手工浮窗坐标布局。

当前已经补上的面板可用性能力：

- `SceneHierarchyPanel` 支持 `Add Root`、`Add Child`、`Delete Selected` 这类基础场景树编辑动作。
- `AssetBrowserPanel` 支持按文本搜索、按资源类型过滤，以及带文件夹标识和层级缩进的目录树浏览。
- `ConsolePanel` 支持文本过滤和清空消息，方便在 Editor 运行日志增多时快速聚焦问题。

### 跨模块状态边界约定

- `scene lifecycle`
  - `startup scene load`
  - `new scene`
  - `reload active scene`

  上述路径必须统一走 `EditorApplication` 的 scene-change helper，不允许各面板各自手工 reset。
  `reload active scene` 失败时会真实回退到新的默认场景，并同步清掉 `last_scene_path`，不再只写一条与实际行为不一致的日志。

- `selection / undo-redo / inspector draft`
  - scene changed 时先清 `SelectionService`
  - 再清 `UndoRedoService`
  - 最后重新建立默认 selection
  - 面板内部草稿状态必须依赖这条统一链路归零，不允许跨 scene 残留

- `viewport shared state`
  - `EditorContext.viewport` 只表示当前 primary viewport 的单份共享快照
  - 共享快照由 `EditorApplication::update_editor_context()` 统一发布
  - 非 primary viewport 的状态只能保留在各自 `EditorViewportInstance`
  - 新消费者优先通过 `EditorViewportService + viewport id` 查询，不要继续扩展 `context.viewport`

- `shared commands`
  - 同一语义的编辑命令优先收口到共享 `EditorCommand`
  - 不允许长期保留 `Inspector` / `Hierarchy` 各自维护一套同语义命令实现
  - 当前已统一：`RenameEntityCommand`、`TransformEntityCommand`
  - 当前也已统一：`SetCameraComponentCommand`、`SetLightComponentCommand`、`SetMeshComponentCommand`
  - 当前也已统一：`CreateEntityCommand`、`ReparentEntityCommand`、`DeleteEntityCommand`

- `undo / redo failure semantics`
  - `EditorCommand::undo()` 必须返回 `bool`
  - `UndoRedoService::undo()` 只有在命令真正撤销成功时才返回 `true`
  - `UndoRedoService::redo()` 执行失败时必须把命令放回 redo 栈，不能悄悄丢失历史

### 关于 entt (ECS)

entt 已经在 Editor 的 include path 中。建议使用 entt 来管理场景中的实体和组件：

```cpp
#include <entt/entt.hpp>

entt::registry registry;
auto entity = registry.create();
registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
registry.emplace<MeshComponent>(entity, mesh_handle);
```

### 关于 GLM (数学)

项目全局配置了：
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` — 深度范围 [0, 1]（匹配 Vulkan）
- `GLM_FORCE_LEFT_HANDED` — 左手坐标系

编写相机、变换等逻辑时，请注意这些约定。

---

## 10. 常见陷阱与注意事项

### 不要做

- **不要 include `Graphics/` 下的任何头文件** — 那是 RHI 内部实现，Function 层已为你封装
- **不要直接调用 `RenderDevice`** — 在 Editor 中它不可见（被 `ASH_ENGINE` 宏保护），应使用 `Renderer`
- **不要在 `_on_update()` 中执行渲染操作** — 渲染仅在 `_on_render()` 中进行
- **不要忘记调用基类方法** — `_on_update()` 中必须调用 `Application::_on_update()` 以处理窗口事件
- **不要在 scene-driven viewport 路径里自己分配 viewport `RenderTarget`** — output/surface 生命周期应由 `ScenePresentationSubsystem` 持有
- **不要在 scene-driven viewport 路径里自己调 `SceneRenderer::render_visible_frame(...)`** — 这条提交链已经收回 Engine
- **不要忽略 `begin_frame()` 的返回值** — 这条约束适用于 generic/custom rendering 路径
- **不要在 `_on_render()` 外创建 transient 资源** — transient RT 的生命周期与当前帧绑定
- **不要继续在 Editor 层扩大 scene viewport 对 `RenderScene` / `SceneView` / `SceneRenderer` 的直接依赖** — 当前主干已经收口到 Engine 侧 `ScenePresentationSubsystem`

### 要做

- **资源提前创建，每帧复用** — GPU 资源创建开销大，应在初始化时创建，逐帧更新数据
- **善用 `shared_ptr` 管理资源生命周期** — Engine 的资源 API 基于智能指针设计
- **scene-driven viewport 展示请使用 `UISurfaceHandle` + `UIContext::draw_surface_fill_available(...)`**
- **Shader 路径使用相对于引擎根目录的路径** — 如 `"project/src/editor/Shaders/XXX.hlsl"`
- **渲染结束后务必调用 `pass_ctx.end()`** — 否则会导致未定义行为
- **保持 `_on_render()` 中的 begin/end 对称** — `begin_frame` 配 `end_frame`，`begin_pass` 配 `pass_ctx.end()`

---

## 11. 调试工具

- **spdlog 日志** — 通过 `HLogInfo/Warning/Error` 输出
- **Tracy Profiler** — Debug 配置下启用 (`TRACY_ENABLE`)，可查看 CPU 帧性能
- **RenderDoc** — 外部 GPU 调试器，可以抓帧分析渲染管线
- **Vulkan Validation Layer** — Debug 配置下启用 (`VULKAN_DEBUG_REPORT`, `VULKAN_SYNCHRONIZATION_VALIDATION`)

---

## 12. 快速 QA

**Q: 我能不能绕过 Renderer 直接用底层 API？**
A: 不行。`RenderDevice` 和 `GraphicsContext` 在 Editor 中被宏隔离，无法访问。这是设计意图。

**Q: Shader 支持什么语言？**
A: HLSL。Engine 内部通过 DXC 编译到 SPIR-V。不支持直接写 GLSL。

**Q: 坐标系是什么？**
A: 左手坐标系，深度范围 [0, 1]。

**Q: Scene / Game viewport 应该怎么接？**
A: 通过 `Application::get_scene_presentation()` 声明 `SceneOutputHandle` + `SceneViewBindingHandle`，UI 层只拿 `UISurfaceHandle` 调 `draw_surface_fill_available(...)`。不要自己维护 viewport `RenderTarget` 或直接调 `SceneRenderer`。

**Q: 我想加一个新的渲染 Pass，流程是什么？**
A: 创建 RenderTarget → 创建 GraphicsProgram → 在 `_on_render()` 中用 `begin_pass()` + `draw()` + `end()` 执行；如果是 scene-driven viewport，则优先走 `ScenePresentationSubsystem`，不要再恢复旧 demo renderer。

**Q: 如何与 Engine 开发者协作？**
A: 如果你需要新的 Function 层接口（如新的资源类型、新的渲染特性），请向 Engine 开发者提需求。不要试图自己修改 Engine 代码。
