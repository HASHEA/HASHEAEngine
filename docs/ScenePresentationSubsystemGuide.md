# ScenePresentationSubsystem 开发说明

> 面向 Engine、Editor、Sandbox，以及未来 client 的 scene-driven 3D 渲染接入说明。

## 1. 目标

`ScenePresentationSubsystem` 的目标，是把 scene-driven 3D 渲染主路径从“上层亲自提交渲染”改成“上层声明要展示什么 scene view”。

实现后，上层只需要关心：

- 往 `Scene` 中添加、删除、更新对象和组件
- 选择哪个 `Scene` 的哪个相机要显示到哪个 output
- 可选覆写少量 per-view 参数，例如 clear mode、clear color、pixel rect

上层不再直接持有或编排：

- `RenderScene`
- `SceneView`
- `VisibleRenderFrame`
- `SceneRenderViewContext`
- `SceneRenderer`
- scene-driven viewport 自己的 `RenderTarget`

## 2. Public API 入口

运行时统一入口：

```cpp
auto* scene_presentation = AshEngine::Application::get_scene_presentation();
```

核心句柄：

- `SceneOutputHandle`
- `SceneViewBindingHandle`
- `UISurfaceHandle`

核心描述：

- `SceneOutputDesc`
- `SceneCameraSelector`
- `SceneViewCameraOverride`
- `SceneViewOverrides`
- `SceneViewBindingDesc`

## 3. Output / Binding 模型

### Output

`SceneOutputHandle` 表示一个 engine-owned 输出目标。

当前支持两类 output：

- `SceneOutputKind::Window`
  - 绑定到宿主窗口最终输出
  - 不暴露可供 UI 采样的 surface
- `SceneOutputKind::Offscreen`
  - engine-owned 离屏表面
  - 可以通过 `UISurfaceHandle` 在 `UIContext` 中展示

### Binding

`SceneViewBindingHandle` 表示一个持久 scene view 声明，描述：

- `Scene*`
- 相机选择方式
- 输出目标
- 少量 per-view overrides
- enabled / sort order

binding 是持久声明，不是逐帧命令。上层通过 `create_*` / `update_*` / `destroy_*` 维护声明，实际渲染发生在 `Application` 驱动的固定帧阶段。

### Camera Selector

`SceneCameraSelector` 当前支持：

- `SceneCameraSource::PrimaryCamera`：使用 scene 中 primary camera。
- `SceneCameraSource::EntityId`：使用指定 camera entity。
- `SceneCameraSource::Override`：使用 `SceneViewCameraOverride` 的显式 view/projection matrix 与 camera position，`override_view.enabled` 必须为 true。

`Override` 适合 Editor camera、preview camera、外部工具相机等不应该写入 scene entity 的视图。它仍然需要有效 `Scene*`，因为 scene presentation 需要从 scene 构建 `RenderScene` 和 `VisibleRenderFrame`。clear、pixel rect、show flags 等参数继续放在 `SceneViewOverrides` 中，不和 camera source 混在一起。

## 4. 固定运行阶段

`Application` 统一驱动两个 scene presentation 阶段：

- update phase
  - 无 logic thread 时，跟在 `_on_update()` 后执行
  - 有 logic thread 时，跟在 `_on_logic_startup()` / `_on_logic_update()` 后执行
- submit phase
  - 在默认 `Application::_on_render()` 内执行
  - 固定顺序是 `begin_frame() -> _on_render_debug() -> scene presentation submit -> _on_gui() -> end_frame()`

这意味着：

- 上层的 scene mutation 发生在 update / logic 侧
- render thread 不直接读取 `Scene` / `entt`
- render thread 只消费 update phase 准备好的不可变 packet

## 5. 当前内部实现约定

当前 V1 采取“public API 按长期方向设计，internal implementation 允许阶段性退化”的策略。

具体约定：

- `Scene::get_change_version()` 仍用于通用 scene 脏状态观察，但 render sync 不复用 `Scene::mark_clean()` / `is_dirty()`。
- render sync 使用 `Scene::get_render_primitive_version()`、`Scene::get_render_transform_version()`、`Scene::get_render_light_version()` 三类版本。
- primitive version 变化、binding 请求 refresh 或 cached `RenderScene` 无效时，才按 scene 粗粒度 `RenderScene::rebuild_from_scene(...)` 重建 static mesh primitives。
- 仅 transform version 变化时，`ScenePresentationSubsystem` 调用 `RenderScene::update_transforms_from_scene(...)` 更新 primitive world transform / bounds，并重建灯光快照以覆盖 light entity transform。
- 仅 light version 变化时，只调用 `RenderScene::rebuild_lights_from_scene(...)`。
- 内部按 `Scene*` 维护 `RenderScene`
- `build_scene_view_for_camera_entity(...)` 已提供显式 camera entity 入口
- `build_scene_view_from_matrices(...)` 已提供显式 view/projection matrix 入口，并由 `SceneCameraSource::Override` 使用
- `PrimaryCamera` 仍保留为便捷 fallback

## 6. UI 展示路径

scene-driven viewport 的 UI 展示不再直接拿 `RenderTarget`，而是拿 `UISurfaceHandle`：

```cpp
context.ui_context->draw_surface_fill_available(viewport.surface, preserve_aspect);
```

当前 `UIContext` 新增了：

- `image_surface(...)`
- `draw_surface_fill_available(...)`

`UIContext` 会在 Engine 内部把 `UISurfaceHandle` 解析回当前有效的离屏 `RenderTarget`。Editor / Sandbox 不应该依赖这张底层 RT。

## 7. Editor 接入模式

Editor scene-driven viewport 的推荐接入模式是：

1. `ViewportPanel`
   - 只维护 requested size、panel open、focus/hover、toolbar/UI 语义
   - 只负责显示 `UISurfaceHandle`
2. `EditorViewportService`
   - 为每个 viewport 维护一个 `Offscreen` output 和一个 persistent binding
   - 在 update 阶段同步 requested size / panel state 到 `ScenePresentationSubsystem`
3. `Editor::_on_render()` / `_present()`
   - 保持调用基类默认实现
   - 不再自己 `begin_frame()` / `end_frame()` / `SceneRenderer::render_visible_frame(...)`

当前 `Scene` / `Game` 视口已经迁移到这条路径。

## 8. Sandbox 接入模式

Sandbox 标准场景主路径的推荐模式是：

1. 运行时只维护 scene 内容、相机逻辑、资源加载和场景变更
2. 注册一个 `Window` output
3. 注册一个 persistent binding
4. 保持 `Application::_on_render()` / `_present()` 默认流程

当前 `Sandbox` 已经迁移到这条路径，不再手动驱动 `Renderer::begin_frame()` / `SceneRenderer`

## 9. 非 scene renderer 的边界

`ScenePresentationSubsystem` 只负责 scene-driven 3D 渲染主路径。

以下路径仍然继续使用 `Renderer` 直接驱动：

- 已退役的 editor demo renderer
- custom pass / compute / 后处理
- 纯调试渲染
- 非 scene-based 的特殊渲染器

不要为了“统一接口”强行把 custom renderer 塞进 scene presentation。

## 10. V1 明确限制

当前第一版有以下边界：

- `viewport` / `scissor` 只约束光栅化区域
- `RenderLoadAction::Clear` 仍然是整 attachment clear，不是 rect clear
- 多个 binding 共享同一 output 时，后续 binding 默认应使用 preserve/load 语义
- `Window` output 不返回有效 `UISurfaceHandle`
- `show_flags` 当前主要作为扩展点预留

## 11. 协作约定

如果你在做 Editor / Sandbox / client 的 scene-driven 视图接入：

- 优先把需求表达成 `Scene + Camera + Output + Overrides`
- 不要重新把 `RenderScene`、`SceneView`、`VisibleRenderFrame`、`SceneRenderer` 暴露回上层
- 不要让 UI 层直接持有 scene viewport 的 `RenderTarget`
- 需要 UI 展示时，用 `UISurfaceHandle + UIContext::draw_surface_fill_available(...)`

如果你在做 custom / non-scene 渲染：

- 继续使用 `Renderer`
- 保持这条路径和 scene presentation 的职责边界清晰
