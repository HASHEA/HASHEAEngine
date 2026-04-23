# ImGuiLayer 引擎接口说明草案

> 文档状态：**历史草案 / 参考资料**
>
> 这份文档保留用于回溯早期 Editor / Engine 协作阶段的接口设想，不再作为当前正式接口约定。
>
> 当前主线应优先参考：
>
> - `docs/Editor.UIContextGapChecklist.md`
> - `docs/EditorProgress.UIContextAcceptance.md`
> - `docs/EditorToEngineGapChecklist.md`

> 目标
>
> 给引擎同学一份可直接落地的 Function 层接口约定，用来支撑编辑器当前骨架继续开发。
>
> 本文档对应接口草案：
>
> - `docs/ImGuiLayer.InterfaceDraft.h`

## 1. 设计目标

这个接口不是要把 ImGui 暴露成“编辑器随便调用底层后端”的入口，而是要完成两件事：

1. 让编辑器能稳定拥有自己的 ImGui 生命周期
2. 让编辑器能把引擎 `RenderTarget` 安全显示到 ImGui 窗口里

因此这层接口应满足下面几个原则：

- 放在 `Function` 层
- Editor 可直接依赖
- Editor 不直接接触 DX12/Vulkan descriptor 或 native handle
- 后端差异由引擎内部吸收
- 后续可扩展到多视口、布局持久化、输入路由

## 2. 当前编辑器为什么需要它

当前编辑器代码已经依赖了 `ImGuiLayer` 的生命周期接口：

- [Editor.h](/D:/个人/WorkSpace/HASHEAEngine/project/src/editor/Editor.h#L11)
- [Editor.cpp](/D:/个人/WorkSpace/HASHEAEngine/project/src/editor/Editor.cpp#L33)

同时，视口面板现在已经拿到了 `RenderTarget`，但还不能真正显示图像：

- [Editor.cpp](/D:/个人/WorkSpace/HASHEAEngine/project/src/editor/Editor.cpp#L86)
- [ViewportPanel.cpp](/D:/个人/WorkSpace/HASHEAEngine/project/src/editor/Panels/ViewportPanel.cpp#L57)

所以这份接口草案的核心诉求很明确：

- 先让编辑器稳定跑起来
- 再让 Viewport 真正显示离屏结果

## 3. 建议头文件

见：

- [ImGuiLayer.InterfaceDraft.h](/D:/个人/WorkSpace/HASHEAEngine/docs/ImGuiLayer.InterfaceDraft.h)

## 4. 生命周期接口说明

### `bool init(const ImGuiLayerConfig& config = {})`

职责：

- 创建 ImGui context
- 配置 `ImGuiIO`
- 开启 docking
- 初始化平台后端
- 初始化渲染后端
- 建立字体纹理和必要的后端资源

建议行为：

- 允许重复调用保护
- 初始化失败时返回 `false`
- 失败时输出明确日志

最低要求：

- Vulkan 可用
- DX12 可用
- Docking 默认开启
- Multi-Viewport 第一阶段默认关闭

### `bool begin_frame()`

职责：

- 准备新一帧 ImGui
- 调用平台后端 new frame
- 调用渲染后端 new frame
- 调用 `ImGui::NewFrame()`

建议行为：

- 当窗口、swapchain 或 backend 未就绪时返回 `false`
- 不抛给 Editor 底层状态细节

### `void render()`

职责：

- 调用 `ImGui::Render()`
- 提交 draw data 到当前活动 back buffer

建议行为：

- 与当前 renderer/frame 生命周期兼容
- 不要求 Editor 关心命令缓冲、descriptor heap、render pass 细节

### `void shutdown()`

职责：

- 释放平台后端资源
- 释放渲染后端资源
- 销毁 ImGui context
- 清空 RenderTarget 注册表

建议行为：

- 可安全重复调用

## 5. Viewport 桥接接口说明

### `ImTextureID register_render_target(const std::shared_ptr<RenderTarget>& render_target)`

职责：

- 把引擎 `RenderTarget` 注册成 ImGui 可显示纹理
- 返回可交给 `ImGui::Image()` 的 `ImTextureID`

实现建议：

- 内部维护 `RenderTarget -> backend ui handle` 的映射表
- DX12 内部处理 SRV/descriptor heap 分配
- Vulkan 内部处理 descriptor set / sampled image 绑定

注意：

- Editor 不应该知道 descriptor 是什么
- `ImTextureID` 的具体语义完全由引擎后端负责

### `void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target)`

职责：

- 释放某个 `RenderTarget` 对应的 UI 句柄
- 清理后端侧 descriptor / registration

适用场景：

- 视口关闭
- 资源销毁
- editor shutdown

### `ImTextureID get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target) const`

职责：

- 查询已注册目标当前对应的 `ImTextureID`

为什么要有它：

- 某些 `RenderTarget` 可能在 resize 后内部重建
- Editor 不应自己推断旧句柄是否失效
- 引擎可以在内部把句柄刷新后继续对外返回最新值

### `void draw_render_target(const std::shared_ptr<RenderTarget>& render_target, const ImVec2& size)`

职责：

- 提供一个更高层的便捷接口
- 内部完成：
  - 注册或查找纹理
  - 处理 uv 朝向
  - 调用 `ImGui::Image()`

建议：

- 如果引擎同学愿意把后端差异全部收在这里，这个接口会让编辑器端最干净
- 未来还可以在这里统一加入：
  - checkerboard 背景
  - gamma/srgb 处理
  - 空资源占位

## 6. 输入捕获接口说明

### `bool wants_capture_mouse() const`
### `bool wants_capture_keyboard() const`
### `bool wants_text_input() const`

职责：

- 把 `ImGuiIO` 当前输入捕获状态暴露给编辑器

主要用途：

- Viewport 相机控制时，避免和 ImGui 拖拽冲突
- 快捷键系统中区分“当前键盘输入给 UI 还是给场景”
- 文本编辑场景下禁止全局快捷键误触发

建议语义：

- 直接对应 `ImGui::GetIO()....`
- 由 `begin_frame()` 后的当前帧状态驱动

## 7. 推荐调用顺序

编辑器每帧推荐顺序如下：

1. `renderer->begin_frame()`
2. `imguiLayer->begin_frame()`
3. Editor 绘制各面板
4. `imguiLayer->render()`
5. `renderer->end_frame()`
6. `renderer->present()`

这和当前编辑器主循环保持一致：

- [Editor.cpp](/D:/个人/WorkSpace/HASHEAEngine/project/src/editor/Editor.cpp#L124)

## 8. 后端实现要求

### Vulkan

需要解决：

- `RenderTarget` 对应 SRV/采样描述如何映射到 ImGui backend
- 纹理布局转换是否稳定
- resize 后 descriptor 是否需要重建

### DX12

需要解决：

- `RenderTarget` 的 SRV descriptor 分配与回收
- descriptor heap 生命周期
- `ImTextureID` 与 descriptor handle 的映射
- resize 或资源重建后的句柄更新

### 共通要求

- 不能要求 Editor 直接访问底层 texture view
- 不能要求 Editor 自己处理后端分支
- 不能把 descriptor 泄漏成 Editor 层 API

## 9. 第一阶段可以先不做的内容

为了避免接口过早做大，第一阶段可以明确先不包含：

- 多 ImGui context 管理
- 多窗口原生 platform viewport 支持
- 字体热重载
- editor theme 系统
- docking 布局持久化接口
- gizmo 相关封装

## 10. 推荐最小交付

如果按“尽快解除编辑器阻塞”来排，建议引擎同学先交付下面这组：

```cpp
class ImGuiLayer
{
public:
	virtual bool init(const ImGuiLayerConfig& config = {}) = 0;
	virtual bool begin_frame() = 0;
	virtual void render() = 0;
	virtual void shutdown() = 0;

	virtual ImTextureID register_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;
	virtual void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;
	virtual ImTextureID get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target) const = 0;
};
```

如果还能顺手补一个高层便捷接口，优先建议再加：

```cpp
virtual void draw_render_target(const std::shared_ptr<RenderTarget>& render_target, const ImVec2& size) = 0;
```

## 11. 一句话结论

这套接口的本质不是“给 Editor 一个 ImGui 封装”，而是：

**让引擎在 Function 层正式承担 ImGui 生命周期管理和 RenderTarget-to-ImGui 的桥接职责。**

只要这层补稳，编辑器这边下一步就能很自然地继续做：

- 真正的 Viewport
- 场景离屏渲染
- 视口交互
- 后续 Inspector / Asset Browser 的稳定 UI 生命周期
