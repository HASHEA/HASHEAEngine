# 编辑器开发视角的引擎补充清单

> 范围说明
>
> - 只基于当前主线代码：`project/src/engine` + `project/src/editor`
> - `KEnginePub` 不参与本次判断
> - 目标不是列所有理想中的底层需求，而是只列出“从当前编辑器继续往下做，仍然需要引擎同学补充的内容”

## 1. 这次更新里已经补上的内容

以下几项和我之前的清单相比，已经有明显进展，可以先从“待补充”里移除：

### 1.1 窗口事件链路已经比之前完整

- `Window` 已经提供事件队列接口：`poll_event()`、`push_event()`、`pop_event()`
- `WindowWin` 已经把 resize、最小化、关闭、键鼠、滚轮等 GLFW 回调转成 `WindowEvent`
- `Application` 已经开始统一消费窗口事件

对应代码：

- `project/src/engine/Base/window/Window.h`
- `project/src/engine/Base/window/WindowWin.cpp`
- `project/src/engine/Function/Application.cpp`

### 1.2 输入状态骨架已经补上

- `Application::get_input()` 已经公开
- `InputState` 已经能记录 key down/pressed/released、鼠标按钮、鼠标位置、滚轮增量

对应代码：

- `project/src/engine/Base/input/Input.h`
- `project/src/engine/Function/Application.h`
- `project/src/engine/Function/Application.cpp`

### 1.3 渲染器已经具备离屏 RenderTarget 基础能力

- `Renderer` 已经支持：
  - `get_back_buffer()`
  - `create_render_target()`
  - `acquire_transient_render_target()`
  - `begin_pass()`
  - `dispatch()`
- `RenderTarget`、`GraphicsProgram`、`ComputeProgram` 的 Function 层封装已经存在

对应代码：

- `project/src/engine/Function/Render/Renderer.h`
- `project/src/engine/Function/Render/Renderer.cpp`
- `project/src/engine/Function/Render/RenderDevice.h`

## 2. 当前仍需引擎同学补充的清单

下面这些是我看完最新代码后，认为仍然缺口明确、并且会直接影响编辑器继续推进的部分。

---

## 2.1 P0：先解决会直接卡住编辑器的缺口

### [ ] 1. 补回并稳定 `ImGuiLayer` 的 Function 层接口

这是当前最直接的阻塞项。

原因：

- 编辑器主程序已经在使用 `AshEngine::ImGuiLayer`
- 但当前仓库里看不到 `project/src/engine/Function/Gui/ImGuiLayer.h/.cpp` 实体文件
- `Editor` 仍然在包含并实例化它

对应代码：

- `project/src/editor/Editor.h`
- `project/src/editor/Editor.cpp`
- `project/src/engine/Engine.vcxproj`

建议引擎侧交付：

- `ImGuiLayer::init()`
- `ImGuiLayer::begin_frame()`
- `ImGuiLayer::render()`
- `ImGuiLayer::shutdown()`
- 明确支持当前实际后端
  - Vulkan
  - DX12
- 明确 docking 是否开启
- 明确多视口是否先关闭

这是“编辑器能不能稳定跑起来”的前置项。

### [ ] 2. 提供 RenderTarget 到 ImGui 的显示桥接

现在引擎已经能创建离屏 `RenderTarget`，但编辑器还不能把这个目标真正画进 `ViewportPanel`。

现状：

- `ViewportPanel` 现在只拿到了一个 `RenderTarget`
- 但 `RenderTarget` 的公开接口只有宽高、格式、深度信息
- 没有 editor-safe 的 ImGui 纹理桥接接口
- 现在视口面板也明确写着“场景仍直接渲染到 swapchain”

对应代码：

- `project/src/editor/Editor.cpp`
- `project/src/editor/Panels/ViewportPanel.cpp`
- `project/src/engine/Function/Render/RenderDevice.h`

建议引擎侧至少提供一种统一方案：

- 方案 A：`ImGuiLayer::draw_render_target(RenderTarget*, size, uv...)`
- 方案 B：`ImGuiLayer::register_render_target(RenderTarget*) -> ImTextureID`
- 方案 C：给 `RenderTarget` 提供 editor-safe 的 UI 句柄导出接口

同时建议把下面问题一起统一掉：

- DX12 / Vulkan 的纹理朝向差异
- SRV/descriptor 生命周期
- RenderTarget 重建后的 UI 句柄更新

不补这个接口，编辑器下一步做真正的 Viewport 会很别扭。

---

## 2.2 P1：补最小 Scene Runtime，才能把假数据面板替换掉

### [ ] 3. 提供最小 Scene / Entity 编辑模型

当前编辑器的层级面板还是写死的假数据：

- `SceneRoot`
- `MainCamera`
- `DirectionalLight`

对应代码：

- `project/src/editor/Panels/SceneHierarchyPanel.cpp`

建议引擎侧提供最小可编辑模型，至少包括：

- `Scene`
- `Entity`
- 稳定的 entity id
- 名称
- 父子层级
- 创建 / 删除 / 遍历接口

要求：

- 由 Function 层向 Editor 暴露
- 不让 Editor 直接依赖底层 ECS/RHI 细节

### [ ] 4. 提供最小基础组件集合

如果没有基础组件，Inspector 和场景编辑都只能停留在“选中一个名字”。

建议第一批只做最小集：

- `NameComponent`
- `TransformComponent`
- `CameraComponent`
- `LightComponent`
- `MeshComponent`

注意：

- 不要求一次做完整运行时
- 但至少要让编辑器能围绕统一组件模型展开，而不是继续用临时结构

### [ ] 5. 提供场景保存 / 加载接口

当前主菜单里已经有：

- New Scene
- Open Scene
- Save Scene

但这些还都不能工作。

对应代码：

- `project/src/editor/Editor.cpp`

建议引擎侧提供：

- 新建空场景
- 保存场景
- 加载场景
- 场景 dirty 标记的基础支持

这一层不一定一开始就要非常复杂，但必须先有统一入口，不然编辑器菜单只能一直是空壳。

---

## 2.3 P1：补 Inspector 所需的数据描述能力

### [ ] 6. 提供最小反射/属性描述接口

当前 Inspector 面板已经接上 selection，但还没有真实属性数据来源。

对应代码：

- `project/src/editor/Panels/InspectorPanel.cpp`

建议引擎侧至少提供“够用版”的类型描述能力，不要求一开始就做大而全的反射系统，但至少要支持：

- 组件类型名
- 字段名
- 字段类型
- 字段偏移或 getter/setter
- 基础数值类型
- `vec2/vec3/vec4`
- bool
- enum

这样编辑器才能开始做：

- 通用 Inspector 绘制
- 属性修改
- 未来的 Undo/Redo 命令封装

### [ ] 7. 提供 Inspector 可写回的安全修改入口

即便先不做完整命令系统，也建议引擎先提供一层明确的“组件读写边界”，避免编辑器后面直接到处改运行时内部数据。

建议形式：

- 组件句柄 + 字段修改接口
- 或者 Scene/Entity facade 上的显式 setter

目标：

- 后续接 Undo/Redo 时不用重构整条属性修改链

---

## 2.4 P2：补 Asset Browser 真正需要的底座

### [ ] 8. 提供最小 Asset 数据库或资源索引服务

当前资源面板也还是占位状态。

对应代码：

- `project/src/editor/Panels/AssetBrowserPanel.cpp`

建议引擎侧补充最小能力：

- 扫描项目资源目录
- 资源唯一标识
- 路径到资源记录的映射
- 基础 meta 信息
- 文件变更后重新索引

第一阶段不一定要把完整 import pipeline 一次做完，但至少要先把“资源列表不是硬编码文本”这件事落下来。

### [ ] 9. 提供 Editor 可用的资源加载入口

当 Asset Browser 从“看得到”走到“点得开、拖得动、放得进场景”时，编辑器需要一条稳定的引擎入口。

建议引擎侧提供：

- 按路径/GUID 查询资源
- 资源加载状态查询
- 失败信息反馈

---

## 2.5 P2：补视口真正要用的运行时渲染入口

### [ ] 10. 提供“把场景渲染到指定 RenderTarget”的高层入口

当前编辑器渲染仍然是 `CodexLogoDemoRenderer` 示例路径，不是正式场景渲染接口。

对应代码：

- `project/src/editor/CodexLogoDemoRenderer.cpp`

建议引擎侧后续提供高层入口，例如：

- `render_scene(scene, camera, target)`
- `render_world(world, viewport_desc)`
- 或一个 `EditorViewportRenderer` 可调用的 Function 层 facade

这样编辑器才不需要长期把 demo 渲染逻辑当正式管线使用。

### [ ] 11. 提供基础渲染统计和调试信息接口

这项不是最前面的硬阻塞，但很快会用到。

建议至少能拿到：

- 当前渲染分辨率
- draw call 数
- pass 数
- GPU/CPU 帧时间
- 当前 backend 信息

后面可用于：

- 状态栏
- Viewport overlay
- Console / Profiler 面板

## 3. 我建议引擎同学现在优先做的顺序

如果只排“最影响编辑器继续开发”的顺序，我建议是：

1. `ImGuiLayer` 补齐并稳定
2. RenderTarget 到 ImGui 的显示桥接
3. 最小 `Scene / Entity / Component` facade
4. 场景保存 / 加载
5. 最小反射/属性描述
6. Asset 数据索引
7. 场景渲染到指定 RenderTarget 的高层入口

## 4. 可以先不让引擎同学做的内容

下面这些不是现在最急的，可以后放，避免和编辑器骨架开发互相卡住：

- Undo / Redo 具体实现
- Editor 布局保存
- 快捷键系统的完整上下文管理
- Prefab
- Play / Edit 模式切换
- 插件系统
- 完整 Render Graph

## 5. 一句话结论

这次引擎更新后，**窗口事件、输入状态、基础渲染封装已经比之前可用了很多**；但从编辑器继续推进的角度，当前最缺的仍然是：

- `ImGuiLayer`
- Viewport 的 RenderTarget 显示桥接
- 最小 Scene Runtime
- Inspector 所需的属性描述
- Asset Browser 所需的资源索引入口

把这 5 件事补上，编辑器就能从“骨架 UI”继续推进到“真正编辑内容”的阶段。
