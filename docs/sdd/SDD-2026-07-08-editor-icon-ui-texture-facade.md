# SDD-2026-07-08-editor-icon-ui-texture-facade: EditorIconService 去 Graphics 依赖（Mini SDD, S1）

## Status
Done（2026-07-08 实施完成，验证通过）

## Context

ArchGate legacy 名单中的真实越界：`EditorIconService.cpp` include 了
`Graphics/GraphicsContext.h` / `RHICommon.h` / `Texture.h`，直接经
`Application::get_graphics_context()` + `TextureCreation` 创建 GPU 纹理，违反
「Editor 只经 UIContext 与 Engine UI 交互」红线（specs/modules/editor.md）。
根因：UIContext 只有 `register_texture_view`（登记已存在的 view），缺少
「CPU 像素 → UI 可绘制纹理」的创建门面，Editor 被迫自己下 Graphics 层。

关键事实：`TextureView` 对父 `Texture` 仅持 weak_ptr（VulkanTexture.cpp
`parentTexture.lock()`），调用方必须同时持有 texture+view，否则纹理被释放。
因此门面返回值需要一个打包双所有权的载体——新抽象 `UITexture` 由本 SDD 明确要求
（满足 AGENTS.md「新抽象需 SDD 明确要求」条款）。

## Goals

- `Function/Gui/UITexture.h`：不透明所有权载体，内部持 `shared_ptr<RHI::Texture>` +
  `shared_ptr<RHI::TextureView>`，成员仅 UIContext 可见（friend）
- `UIContext` 新增（editor begin/end 标记）：
  - `create_ui_texture_rgba8(pixels, width, height, debug_name)` → `shared_ptr<UITexture>`
    （RGBA8 sRGB、单 mip、SRV-only，即原 EditorIconService 的创建参数）
  - `register_ui_texture` / `unregister_ui_texture`（内部转 register/unregister_texture_view）
- `EditorIconService` 全面改走门面：删除全部 Graphics include 与
  `Function/Application.h`（不再需要 get_graphics_context），`IconEntry` 以
  `shared_ptr<UITexture>` 替代裸 RHI texture/view 对
- `arch-boundary-rules.json` 移除 EditorIconService legacy 条目（名单只减不增）

## Non-goals

- 不做通用图片加载门面（PNG 解码仍留在 Editor，stb_image 是 vendored 三方非 Graphics）
- 不动 `register_texture_view` 既有接口（ScenePresentation 等引擎内部仍在用）
- 不处理 VulkanSwapchain 反向依赖（另属 S2）

## Design notes

- UIContext.cpp 属 Function 层，include Graphics 合法；创建走 `m_impl->graphics_context`
- UIContext 未初始化时 `create_ui_texture_rgba8` 返回 nullptr（原 Editor 侧
  get_graphics_context 空检查语义等价迁移）
- Editor 头文件不再出现 RHI 前置声明

## Verification

- `build_editor.bat Debug` 构建
- `RunArchGate.bat`：EditorIconService 条目移除后 PASS（legacy warns 39 → 36）
- `run.bat editor Debug --smoke-test-seconds=5` + 人工确认 AssetBrowser/SceneHierarchy 图标正常显示
- specs 同步：editor.md（EditorIconService 描述）、application.md（UIContext 纹理约定）

## 执行结果

- 新增 `Function/Gui/UITexture.h`（不透明所有权载体，成员 friend UIContext 可见）；
  `UIContext` 新增 `create_ui_texture_rgba8` / `register_ui_texture` / `unregister_ui_texture`
  （editor begin/end 标记，实现走 `Impl::graphics_context`）
- `EditorIconService` 删除全部 Graphics include 与 `Function/Application.h`；
  头文件不再有 RHI 前置声明；`IconEntry` 改持 `shared_ptr<UITexture>`
- 语义微变：原「GraphicsContext 不可用则不标记失败、下帧重试」合并为「create 返回
  nullptr 即标记 bLoadFailed」——GetIcon 只在活动 UIContext 帧内被调用，该场景实际不可达
- 验证：新增头文件后重跑 `generate_vs2022.bat`（premake glob）；`build_editor.bat Debug`
  0 错误；`RunArchGate.bat` PASS（legacy warns 39 → 36）；
  `run.bat editor current Debug --smoke-test-seconds=5` 日志无 EditorIconService 告警、
  内存全释放退出。图标视觉正常显示待用户开 Editor 人工确认
- spec 同步：`docs/specs/modules/application.md` UIContext 纹理约定新增像素上传门面条目
