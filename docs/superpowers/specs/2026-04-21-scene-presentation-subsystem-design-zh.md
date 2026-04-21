# ScenePresentationSubsystem 设计提案

**状态：** 提议  
**日期：** 2026-04-21  
**范围：** `project/src/engine/Function/Application.*`、`project/src/engine/Function/Render/*`、`docs/EngineDeveloperGuide.md`、`docs/EditorDeveloperGuide.md`

## 1. 目标

把当前上层仍然需要亲自编排的 scene 渲染提交流程：

- `Renderer`
- `RenderScene`
- `SceneView`
- `VisibleRenderFrame`
- `SceneRenderViewContext`
- `SceneRenderer`

收口到一个 Engine-facing 的通用子系统里，让 `Editor`、`Sandbox`、未来 `Client` 在 scene-driven 3D 渲染主路径上，只需要关心：

- 往 `Scene` 中添加和更新对象
- 给对象挂 `Camera` / `Mesh` / `Light` 等组件
- 声明“哪个 `Scene` 的哪个相机要显示到哪个 viewport / window / offscreen surface”
- 可选覆写少量 per-view 参数，例如 clear color、显示区域、show flags

这次提案的目标不是再包一层 `SceneRenderer` helper，而是把“scene-driven 3D 渲染的宿主编排责任”从上层拿回 Engine。

## 2. 为什么要改

当前多 view 版本虽然已经把 `SceneRenderer` 调整成 per-view 显式提交，但上层依然在亲自串整条提交流程。

当前泄漏到上层的职责包括：

- 获取和驱动 `Renderer`
- 管理 scene viewport 的输出 RT 生命周期
- 从 `Scene` 构建或重建 `RenderScene`
- 构建 `SceneView`
- 构建 `VisibleRenderFrame`
- 组装 `SceneRenderViewContext`
- 调用 `SceneRenderer::render_visible_frame(...)`

这会带来几个问题：

- `Editor`、`Sandbox`、未来 `Client` 都会长出各自的一套 scene submission orchestration
- camera binding 不是一等语义，多 viewport 想看不同相机时会继续推高上层复杂度
- output target 生命周期和宿主 UI 语义混在一起，DX12 back-buffer sampling hazard 之类的低层问题会继续外溢
- 当前 logic/render 双线程模型没有在 scene submission 上形成统一所有权，`Sandbox` 和 `Editor` 的路径已经开始分叉

换句话说，当前 `SceneRenderer` 接口已经更合理了，但**上层真正需要收掉的，不是某个函数签名，而是整条 scene-driven render orchestration。**

## 3. 设计边界

本提案明确采用以下边界。

### 3.1 保持不变的边界

- `Scene` 继续是逻辑世界 source of truth
- `Renderer` 继续是通用 frame/pass/present facade
- `RenderScene`、`SceneView`、`VisibleRenderFrame`、`SceneRenderer` 继续存在
- `UIContext` 继续只承载通用 DevUI 能力，不承载 Editor workspace 语义

### 3.2 本次新增的边界

- `Application` 下新增一个通用 scene presentation 子系统：
  - `ScenePresentationSubsystem`
- `Editor` / `Sandbox` / `Client` 不再直接接触：
  - `RenderScene`
  - `SceneView`
  - `VisibleRenderFrame`
  - `SceneRenderViewContext`
  - `SceneRenderer`
- scene-driven 3D 渲染主路径改为“持久注册 + 自动驱动”的声明式模式

### 3.3 本次明确不做的事

- 不把 `Scene` 演化成完整 `World` / gameplay framework
- 不把 `Renderer` 演化成 world manager
- 不把自定义非 scene renderer 强行并入第一版主路径
- 不做 render graph
- 不做 rect clear emulation
- 不承诺第一版所有 scene 变更都是真正的细粒度增量同步

## 4. 推荐方案

推荐采用：

- `Application` 持有 `ScenePresentationSubsystem`
- 上层通过**持久注册式 view binding** 描述：
  - `Scene`
  - `Camera`
  - `Output`
  - 少量 per-view overrides
- 子系统内部负责：
  - scene dirty 跟踪
  - render asset 同步
  - `RenderScene` cache
  - `SceneView` 构建
  - `VisibleRenderFrame` 构建
  - output surface 分配 / resize
  - `SceneRenderViewContext` 组装
  - `SceneRenderer` 提交

这条路线的原因：

- 它能把 scene-driven 3D 渲染的宿主复杂度统一收回 Engine
- 它不会污染 `Scene` 的职责边界
- 它能同时适配：
  - `Sandbox` 主相机
  - `Editor` 多 viewport
  - future `Client` 的主相机 / spectator / minimap / reflection view
- 它和当前第一阶段 logic/render 线程模型天然兼容

## 5. Public API 草案

以下 API 名称为推荐形状，允许后续在命名上做小幅调整，但职责边界不建议改变。

### 5.1 `Application` 暴露入口

```cpp
class ASH_API Application
{
public:
    static ScenePresentationSubsystem* get_scene_presentation();
};
```

设计意图：

- `Renderer` 仍然保留给 generic/custom rendering path
- scene-driven 3D 渲染主路径不再从 `Application::get_renderer()` 起步

### 5.2 Handle 类型

```cpp
namespace AshEngine
{
    struct SceneOutputHandle
    {
        uint32_t value = 0;
        bool is_valid() const { return value != 0; }
    };

    struct SceneViewBindingHandle
    {
        uint32_t value = 0;
        bool is_valid() const { return value != 0; }
    };

    struct UISurfaceHandle
    {
        uint32_t value = 0;
        bool is_valid() const { return value != 0; }
    };
}
```

设计意图：

- 上层不直接持有 `RenderTarget`
- output / binding / UI display surface 都是 engine-owned 句柄

### 5.3 Output 描述

```cpp
namespace AshEngine
{
    enum class SceneOutputKind : uint8_t
    {
        Window,
        Offscreen
    };

    enum class SceneOutputFormat : uint8_t
    {
        Auto,
        SRGB8,
        RGBA16F
    };

    struct SceneOutputDesc
    {
        const char* debug_name = nullptr;
        SceneOutputKind kind = SceneOutputKind::Offscreen;
        uint32_t width = 1;
        uint32_t height = 1;
        SceneOutputFormat format = SceneOutputFormat::Auto;
        bool srgb = true;
    };
}
```

约定：

- `Window` 表示绑定到宿主窗口输出
- `Offscreen` 表示 engine-owned 离屏表面
- `Window` output 不向上暴露 back buffer
- `Offscreen` output 不向上暴露 `RenderTarget`

### 5.4 Camera 选择器

```cpp
namespace AshEngine
{
    enum class SceneCameraSource : uint8_t
    {
        PrimaryCamera,
        EntityId
    };

    struct SceneCameraSelector
    {
        SceneCameraSource source = SceneCameraSource::PrimaryCamera;
        EntityId entity_id = 0;
    };
}
```

设计意图：

- 让 camera binding 成为一等语义
- `EntityId` 是多 viewport / 多相机的主路径
- `PrimaryCamera` 只保留为便捷 fallback

### 5.5 Per-view overrides

```cpp
namespace AshEngine
{
    enum class SceneClearMode : uint8_t
    {
        Default,
        Clear,
        Preserve,
        DontCare
    };

    enum class SceneViewRectMode : uint8_t
    {
        FullOutput,
        PixelRect
    };

    struct ScenePixelRect
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct SceneViewShowFlags
    {
        uint64_t bits = 0;
        static SceneViewShowFlags default_flags();
    };

    struct SceneViewOverrides
    {
        SceneClearMode color_clear_mode = SceneClearMode::Default;
        glm::vec4 clear_color{ 0.025f, 0.03f, 0.05f, 1.0f };

        SceneClearMode depth_clear_mode = SceneClearMode::Default;
        float clear_depth = 1.0f;

        SceneViewRectMode rect_mode = SceneViewRectMode::FullOutput;
        ScenePixelRect rect{};

        SceneViewShowFlags show_flags = SceneViewShowFlags::default_flags();
    };
}
```

第一版只要求：

- clear mode
- clear color / clear depth
- full-output 或 pixel rect
- `show_flags` 先作为扩展点保留

### 5.6 View binding 描述

```cpp
namespace AshEngine
{
    struct SceneViewBindingDesc
    {
        const char* debug_name = nullptr;
        Scene* scene = nullptr;
        SceneCameraSelector camera{};
        SceneOutputHandle output{};
        SceneViewOverrides overrides{};
        bool enabled = true;
        int32_t sort_order = 0;
    };
}
```

设计意图：

- binding 是持久对象，不是逐帧 command
- 上层通过改 binding 属性来改变 scene view 行为
- 多个 binding 可以串行提交到不同 output，也可以串行提交到同一 output

### 5.7 子系统 public API

```cpp
namespace AshEngine
{
    class ASH_API ScenePresentationSubsystem
    {
    public:
        SceneOutputHandle create_output(const SceneOutputDesc& desc);
        bool update_output(SceneOutputHandle handle, const SceneOutputDesc& desc);
        void destroy_output(SceneOutputHandle handle);

        SceneViewBindingHandle create_view_binding(const SceneViewBindingDesc& desc);
        bool update_view_binding(SceneViewBindingHandle handle, const SceneViewBindingDesc& desc);
        void destroy_view_binding(SceneViewBindingHandle handle);

        bool set_binding_enabled(SceneViewBindingHandle handle, bool enabled);
        bool request_refresh(SceneViewBindingHandle handle);

        UISurfaceHandle get_ui_surface(SceneOutputHandle handle) const;
    };
}
```

约定：

- `create_*` / `update_*` 只修改声明，不立即触发渲染
- 真正渲染发生在 `Application` 驱动的固定帧阶段
- 上层不应假定更新操作之后本行代码后面就能立刻读到新的 GPU 输出

### 5.8 UI 展示入口

建议在 `UIContext` 上补一条基于句柄的展示能力：

```cpp
bool UIContext::image_surface(UISurfaceHandle surface, const glm::vec2& size);
```

设计意图：

- `ViewportPanel` 等 UI 层代码不再接触 `RenderTarget`
- DX12 back-buffer sampling hazard 继续留在 Engine 内部处理

### 5.9 使用示例

#### Editor scene viewport

```cpp
auto* presentation = AshEngine::Application::get_scene_presentation();

m_scene_output = presentation->create_output({
    .debug_name = "EditorSceneViewport",
    .kind = AshEngine::SceneOutputKind::Offscreen,
    .width = viewport_width,
    .height = viewport_height
});

m_scene_view = presentation->create_view_binding({
    .debug_name = "EditorSceneView",
    .scene = &active_scene,
    .camera = {
        AshEngine::SceneCameraSource::EntityId,
        editor_camera_entity_id
    },
    .output = m_scene_output,
    .overrides = {
        .color_clear_mode = AshEngine::SceneClearMode::Clear
    }
});

ui->image_surface(presentation->get_ui_surface(m_scene_output), viewport_size);
```

这个路径里，Editor 仍然负责：

- viewport panel 的布局、显示尺寸、焦点和输入语义
- 选择哪个 scene / camera 显示到哪个 viewport

但不再负责：

- `RenderScene` rebuild
- `SceneView` 构建
- `VisibleRenderFrame` 构建
- scene output `RenderTarget` 生命周期
- `SceneRenderer` 提交链

#### Sandbox 主窗口相机

```cpp
auto* presentation = AshEngine::Application::get_scene_presentation();

m_main_output = presentation->create_output({
    .debug_name = "SandboxMainWindow",
    .kind = AshEngine::SceneOutputKind::Window
});

m_main_view = presentation->create_view_binding({
    .debug_name = "SandboxMainCamera",
    .scene = &runtime_scene,
    .camera = {
        AshEngine::SceneCameraSource::EntityId,
        player_camera_entity_id
    },
    .output = m_main_output
});
```

这个路径里，Sandbox 只维护：

- scene 内容
- 相机控制
- binding 属性更新

而不再亲自串 `Renderer -> RenderScene -> SceneView -> SceneRenderer`。

## 6. 内部职责分解

### 6.1 长期对象

子系统内部建议拆成三类状态。

#### `ScenePresentationSceneState`

按 `Scene*` 维护：

- 对应 `RenderScene`
- scene 同步状态
- dirty 标记
- 当前同步路径是增量还是退化 rebuild

#### `ScenePresentationOutputState`

按 `SceneOutputHandle` 维护：

- `SceneOutputDesc`
- 对于 `Offscreen` 的实际 engine-owned surface
- 对应 UI display handle
- resize / rebuild 状态

#### `ScenePresentationBindingState`

按 `SceneViewBindingHandle` 维护：

- `SceneViewBindingDesc`
- 解析后的 camera 结果
- 当前有效 overrides
- 最近一次 prepared packet
- 启用状态 / 排序键 / refresh 标记

### 6.2 固定阶段

建议固定成两个 phase：

1. `Presentation Update Phase`
2. `Presentation Submit Phase`

#### `Presentation Update Phase`

负责：

- 吃掉 output / binding descriptor 更新
- 解析 `Scene` 与 `Camera`
- 根据 output 尺寸构建 `SceneView`
- 同步 `RenderScene`
- 生成不可变的 `VisibleRenderFrame`
- 产出 render-thread 只读的 `PreparedScenePresentationPacket`

#### `Presentation Submit Phase`

负责：

- 解析当前帧真实 output
- `Window` output 绑定到当前帧 back buffer
- `Offscreen` output 完成真实 RT 分配和 resize
- 组装 `SceneRenderViewContext`
- 调用 `SceneRenderer::render_visible_frame(...)`
- 更新 UI surface 映射

### 6.3 `Renderer` 与 `SceneRenderer` 的定位

这次设计里：

- `Renderer` 仍然只负责 frame/pass/present 与 generic rendering 能力
- `SceneRenderer` 仍然是内部 scene opaque pass submitter
- `RenderScene` / `SceneView` / `VisibleRenderFrame` 继续作为 scene 渲染链路内部细节存在

不建议：

- 把 `Renderer` 扩成 world manager
- 把 `SceneRenderer` 继续向上暴露为 Editor/Sandbox/Client 的公共主入口

## 7. 线程模型

### 7.1 推荐 ownership

- 如果启用了 logic thread：
  - `Presentation Update Phase` 在 logic thread
  - `Presentation Submit Phase` 在 render thread
- 如果没有启用 logic thread：
  - `Presentation Update Phase` 退化到主线程
  - `Presentation Submit Phase` 仍然在主线程/渲染线程

### 7.2 核心线程规则

- render thread 不直接读取 `Scene` / `entt`
- logic/update 侧不直接发 draw，不直接调 `SceneRenderer`
- update phase 只发布不可变 packet 给 render thread
- render thread 只消费 packet 和 output state

这条规则的意义在于：

- 统一 `Sandbox` 和 `Editor` 现有分叉中的所有权模型
- 保持 scene/render handoff 的长期方向和已有第一阶段线程模型一致

## 8. 同步策略

本提案建议采用：

- **public API 按增量同步方向设计**
- **internal implementation 允许阶段性退化**

具体约定：

- 上层不感知“这次是增量同步还是整体 rebuild”
- 子系统内部优先基于 scene/component dirty 走局部更新
- 如果某类 delta 路径尚不稳定，则允许对单个 `ScenePresentationSceneState` 退化为：
  - `RenderScene::rebuild_from_scene(...)`

这能保证：

- 对上 API 一开始就是长期正确方向
- 第一版不会被“必须一次性做完整 proxy delta sync”卡死

## 9. 自定义 renderer 的边界

第一版按你的要求采用：

- scene-driven 3D 渲染先收口
- 自定义非 scene renderer 保持独立路径
- 但在设计上预留未来扩展点

因此当前建议：

- `CodexLogoDemoRenderer` 这类路径继续通过 `Renderer` 直接驱动
- `ScenePresentationSubsystem` 暂不统一调度所有 custom renderer
- 将来如需扩展，可在 `Application` 帧循环里给 scene presentation 之后预留 extension slot

但第一版不建议：

- 为了“统一”强行把所有 renderer 类型塞进同一个抽象

## 10. 运行规则与错误处理

### 10.1 Binding 不是立即执行命令

- `create_view_binding()` / `update_view_binding()` 只是更新声明
- 真正渲染发生在固定帧阶段
- 上层不应假定“更新完 binding 后这一行后面马上拿到新结果”

### 10.2 Camera 解析规则

建议约定：

- `EntityId` 是正式多相机主路径
- `PrimaryCamera` 是 fallback
- 如果 camera 无效：
  - 本帧该 binding 不提交 scene pass
  - `Window` output 可保持当前内容或 clear-only
  - `Offscreen` output 建议 clear-only

### 10.3 Output ownership 规则

- `Window` output 不暴露 back buffer
- `Offscreen` output 不暴露 `RenderTarget`
- output resize 由子系统自动处理
- 上层不再处理 scene viewport RT 生命周期

### 10.4 单 output 多 binding 规则

第一版必须保留当前多 view 限制：

- `viewport` / `scissor` 只约束光栅化区域
- `Clear` 仍然是整 attachment clear

因此如果多个 binding 串行写入同一 output：

- 第一个 binding 可以 clear
- 后续 binding 默认应使用 preserve/load
- 或者拆成多个独立 output

### 10.5 错误处理策略

建议采用“单 binding 失败降级，不拖垮整帧”的策略。

单 binding 的典型失败包括：

- scene 为空
- camera 无效
- output 无效
- `RenderScene` 同步失败
- `VisibleRenderFrame` 构建失败
- `SceneRenderer` 提交失败

推荐处理方式：

- 记录带 binding 名称、scene 名称、camera、output 名称的错误日志
- 标记该 binding 本帧失败
- 继续处理其他 binding

只有全局性故障才升级为整帧失败，例如：

- 子系统初始化失败
- `Renderer` 不可用
- output allocator 故障导致 scene presentation 路径整体无法继续

## 11. V1 明确限制

第一版明确不承诺以下能力：

- rect clear
- render graph
- 完整 post-process 编排
- 完整自定义 renderer 统一调度
- 完整 world/system/gameplay lifecycle
- 所有 scene 改动都一定走细粒度增量同步
- instancing / occlusion / shadow / dynamic material instance 的完整生产能力

## 12. 对上层的最终效果

本提案要达成的核心效果可以概括成一句话：

**上层从“亲自提交渲染”变成“声明要展示什么 scene view”。**

### `Sandbox`

只保留：

- scene 内容
- 自由相机逻辑
- output / binding 描述

不再保留：

- `Renderer` 场景提交流程
- `RenderScene` / `SceneView` / `SceneRenderer` 编排细节

### `Editor`

只保留：

- viewport panel / 布局 / 输入焦点 / UI 展示语义
- 哪个 viewport 看哪个 scene / camera

不再保留：

- scene viewport RT 自己分配 / resize
- `RenderScene` rebuild
- `SceneRenderer` 提交链拼装

### `Client`

未来可以直接复用这套 binding 语义做：

- 主相机
- spectator
- minimap
- reflection / security cam 风格视图

## 13. 分阶段落地建议

### Phase 1

- 引入 `ScenePresentationSubsystem`
- 接入 `Application`
- 定义 output / binding public API
- `Sandbox` 先切换到新入口
- `Editor` 先切 scene viewport 主路径
- 内部允许 `RenderScene` 按 dirty 退化 rebuild

### Phase 2

- 完整收口 output ownership
- `UIContext` 支持 surface handle 展示
- camera binding 稳定支持 `EntityId`
- Editor viewport 不再直接持有 scene RT 语义

### Phase 3

- 细化 scene/component dirty tracking
- `show_flags` 真正生效
- 预留 custom presenter / custom extension slot

## 14. 相关文档

- `docs/EngineDeveloperGuide.md`
- `docs/EditorDeveloperGuide.md`
- `docs/superpowers/specs/2026-04-16-scene-to-render-flow-design-zh.md`
- `docs/superpowers/specs/2026-04-21-scene-renderer-multi-view-design-zh.md`

## 15. 结论

当前 explicit `SceneRenderer` 多 view 路径解决了 per-view depth/output 的接口问题，但它仍然只是 scene-driven 3D 渲染链路中的内部提交层，不应继续作为 `Editor`、`Sandbox`、`Client` 的长期上层入口。

下一阶段更合理的方向，是在 `Application` 下挂一层通用的 `ScenePresentationSubsystem`，把 scene/camera/output binding 变成上层唯一需要表达的渲染语义，并把 `RenderScene`、`SceneView`、`VisibleRenderFrame`、`SceneRenderViewContext`、`SceneRenderer` 彻底收回 Engine 内部。
