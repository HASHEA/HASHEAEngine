# Render Debug View 设计

## 目标

把当前偏 AO 局部的 `DebugView` 升级为 Engine 级 Render Target 调试视图。功能由 `product/config/Engine.ini` 中的 `[RenderDebugView]` 开关控制；开启后，Engine 侧 ImGui overlay 显示一个下拉框，允许从当前帧 active render target 列表中选择一个目标，并把该目标可视化后直接输出到主画面。

第一版只做主画面替换输出，不做 ImGui 面板内 RT 预览。这样 selected RT 会在同一个 RenderGraph 生命周期内被尾部 debug pass 消费，不需要把 transient render target 暴露给 UI 或跨帧持有。

## 范围

包含：

- 新增 Engine 级 `RenderDebugView` 模块。
- 新增 `[RenderDebugView]` runtime config。
- Engine overlay 中新增 debug view 下拉框。
- `SceneRenderer` 在 deferred path 中注册当前帧可调试 RT。
- RenderGraph 尾部按 selected RT 插入统一 `RenderDebugViewPass`，把选中 RT 可视化写入 `SceneOutput`。
- 逐步替代 AO 局部 `AmbientOcclusion.DebugView` 的职责。

不包含：

- Editor 专属 panel、dock layout 或属性面板。
- ImGui 窗口内 RT 预览。
- 跨帧保存所有 transient RT。
- RenderDoc 捕获或离线图像导出。

## 配置

新增配置段：

```ini
[RenderDebugView]
Enabled=false
Selected=Off
```

配置语义：

- `Enabled=false`：完全关闭系统化 debug view，主渲染路径保持正常输出。
- `Enabled=true`：Engine overlay 显示 debug view UI，RenderGraph 可按 selected 项替换主画面输出。
- `Selected=Off`：开启 UI 但不替换主画面。
- `Selected=SceneOutput`：显示正常主画面输出，语义等同于 `Off`，不额外插入 debug pass。
- `Selected=<RTName>`：若当前帧存在同名 active RT，则输出该 RT 的可视化结果；若不存在，则保留正常画面并在 UI 中显示 unavailable 状态。

`Selected` 是稳定字符串，不绑定枚举值。这样新 pass 增加调试 RT 时不需要改配置 schema。

## 模块结构

新增文件：

```text
project/src/engine/Function/Render/RenderDebugView.h
project/src/engine/Function/Render/RenderDebugView.cpp
project/src/engine/Shaders/Debug/RenderDebugView.hlsl
```

`RenderDebugView` 负责：

- typed runtime config 的加载、发布和查询。
- 当前帧 active RT registry。
- Engine overlay 下拉框绘制。
- 选择项解析和 unavailable 状态维护。
- 向 RenderGraph 追加 `SceneRenderDebugViewPass`。
- 持有可视化 shader program 和必要 sampler。

`SceneRenderer` 只负责：

- 在创建或拿到 RT 时注册 debug item。
- 在 tone-map 输出到 `SceneOutput` 后调用 `RenderDebugView` 追加主画面替换 pass。

`Application` 只负责：

- 初始化时读取 `[RenderDebugView]`。
- 在 `draw_engine_overlay()` 中转调 debug view UI。

## Debug Item 数据

每个当前帧条目至少包含：

```text
name              稳定内部名，例如 SceneDeferredDepth
display_name      UI 名称，例如 Depth
texture           RenderGraphTextureRef
visualization     Color / LinearHDR / Depth / Normal / MotionVector / AO / Scalar
format            可选，用于 UI 文本
width / height    可选，用于 UI 文本
```

第一版的候选项：

- `SceneOutput`，作为正常主画面输出别名，不作为 sampled debug input。
- `SceneDeferredDepth`
- `SceneGBufferA`
- `SceneGBufferB`
- `SceneGBufferC`
- `SceneGBufferD`
- `SceneGBufferE`
- `SceneAmbientOcclusionRaw`
- `SceneAmbientOcclusion`
- `SceneAmbientOcclusionTemporal`
- `SceneDeferredLightingDiffuse`
- `SceneDeferredLightingSpecular`
- `SceneDeferredSceneHDRLinear`

AO 关闭或 temporal AO 未启用时，对应条目自然不出现。UI 保留当前 `Selected` 字符串，但显示为 unavailable，避免自动改写用户配置。

## 渲染数据流

正常路径：

```text
SceneGBufferPass
-> SceneAmbientOcclusionPass
-> SceneDeferredLightingAccumPass
-> SceneDeferredCompositePass
-> SceneDeferredToneMapPass
-> SceneDebugDrawOverlayPass
-> SceneOutput
```

DebugView 选中 RT 时：

```text
SceneGBufferPass
-> SceneAmbientOcclusionPass
-> SceneDeferredLightingAccumPass
-> SceneDeferredCompositePass
-> SceneDeferredToneMapPass
-> SceneRenderDebugViewPass
-> SceneDebugDrawOverlayPass
-> SceneOutput
```

`SceneRenderDebugViewPass` 读取 selected RT，写入 `SceneOutput`。该 pass 使用 `RenderLoadAction::Clear` 或 full-screen overwrite 语义，不依赖旧 output 内容。若 selected 为 `Off` 或 `SceneOutput`，不插入该 pass，避免同一 pass 同时采样和写入 output。

Debug draw overlay 继续放在 debug view pass 后面。这样用户看 GBuffer/depth/AO 时仍能看到 Engine debug lines；如果后续需要纯 RT 检查，可再加 `ShowDebugDrawOverlay` 开关。

## 可视化规则

`RenderDebugView.hlsl` 使用统一 fullscreen shader，靠 root constants 指定 visualization mode：

- `Color`：直接显示 LDR color。
- `LinearHDR`：走简单 exposure / tone-map，避免 HDR RT 直接过曝。
- `Depth`：按当前 view reverse-Z 标志做 depth remap，背景深度显示为黑色或远平面色。
- `Normal`：把 normal 或 normal-oct decode 到 `0.5 * n + 0.5`。
- `MotionVector`：把 screen-space velocity 映射到可见色域，静止接近中性灰。
- `AO`：`ao.rrr` 灰度显示。
- `Scalar`：`value.rrr` 灰度显示。

GBuffer 的具体可视化 hint 由 `SceneRenderer` 注册时指定：

- `GBufferD` 默认按 `MotionVector`。
- `GBufferE` 默认按 `Normal`。
- 其余 GBuffer 初期按 `Color` 或 `Scalar`，后续可细化到 channel inspector。

## UI 行为

当 `RenderDebugView.Enabled=true` 且 `UIContext` 可用时，Engine overlay 新增一个轻量窗口：

- 标题：`Render Debug View`
- 下拉项：`Off` + 当前帧 active RT
- 状态文本：当前 selected、可用性、格式与尺寸

UI 只修改进程内 runtime selection，不在第一版写回 `Engine.ini`。配置文件提供启动默认值；运行时选择用于快速调试。

## 生命周期与同步

第一版不把 `RenderGraphTextureRef` 或 transient `RenderTarget` 交给 ImGui 持有。所有 selected RT 的读取都发生在同一个 RenderGraph 执行期间：

1. `SceneRenderer` 构图并注册 debug item。
2. `RenderDebugView` 根据当前 selection 查找 item。
3. 若存在，向 graph 追加读取 selected RT、写入 `SceneOutput` 的 pass。
4. Graph 执行完毕后 transient RT 正常归还池子。

这条路径避免 transient RT 跨帧生命周期问题，也避免 UI backend 为每个临时 RT 注册/注销 SRV descriptor。

## 错误处理

- 配置解析失败时使用默认值，并输出 warning。
- `Selected` 当前帧不存在时不失败、不打断渲染，只回退正常输出并在 UI 标记 unavailable。
- shader program 或 sampler 创建失败时，`RenderDebugView` 初始化失败，应用初始化应按现有关键渲染模块失败处理。
- pass 注册和 draw 失败时返回 false，沿 `SceneRenderer::render_visible_frame()` 的 process-error 路径上报。

## 与 AO DebugView 的关系

`AmbientOcclusion.DebugView` 是局部、临时的 AO 调试入口；系统化方案上线后，AO pass 应改为注册 `SceneAmbientOcclusionRaw`、`SceneAmbientOcclusion`、`SceneAmbientOcclusionTemporal` 等 debug item，而不是让 `SceneRenderer` 因 AO debug 跳过 lighting/composite。

第一版可以保留 `AmbientOcclusion.DebugView` 的配置解析兼容，但默认建议文档引导使用 `[RenderDebugView]`。

## 测试与验证

新增或扩展 Engine self-test：

- `[RenderDebugView]` 默认配置解析。
- `Enabled` bool 解析。
- `Selected` 字符串保留。
- invalid bool 时回退默认值。
- debug item registry 对 duplicate name 的处理。
- selected item 不存在时返回 unavailable，而不是失败。

运行验证：

- Debug / Release build。
- `Sandbox --engine-self-test`。
- Vulkan Sandbox smoke。
- DX12 Sandbox smoke。
- 若改动影响 shared render/UI path，继续跑 Editor Vulkan / DX12 smoke。

视觉验证重点：

- `Enabled=false` 与 `Selected=Off` 保持正常画面。
- 选择 depth、normal、motion vector、AO、HDR lighting 时主画面有明显对应输出。
- selected RT 不存在时画面不黑、不崩溃，UI 显示 unavailable。

## 文档维护

实现完成时同步更新：

- `README.md`：补充 deferred path 中系统化 Render Debug View。
- `docs/EngineDeveloperGuide.md`：补充 `[RenderDebugView]` 配置、pass 位置和可调试 RT 列表。
- `docs/EngineUIContext.md`：补充 Engine overlay 可承载通用 render debug UI，但不下沉 Editor panel 语义。
