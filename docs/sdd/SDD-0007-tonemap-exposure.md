# SDD-0007: Tonemap 曝光参数（scene_config.tonemap.exposure）

## Status

Done（2026-07-06。验证结果：双 target 构建 PASS；EngineSelfTests 全过（含 exposure 钳制 100→64 与快照链 1.25 透传断言）；默认值 RenderGate PASS golden 不变（vk 0.999992 / dx12 0.999669）；exposure=1.25 演练 FAIL→heatmap 全画面均匀亮度差→用户确认→`-BlessGolden`→复跑 PASS（vk 0.999996 / dx12 0.999995 / cross 0.999457）；双后端 smoke 无 validation 报错；PerfGate Standard PASS。结论已回写 tonemap.md / scene-config.md。）

## Context

Tone map 的曝光乘数当前硬编码为 1.0（`PostProcessToneMapPass.cpp` add_pass
回调内 `make_tone_map_root_constants(frame, output, 1.0f, ...)`），场景无法
调整画面整体曝光。取证结论：**shader 与绑定链路早已支持曝光**——
`DeferredToneMap.hlsl` 的 `PSMain` 读 `AshCameraPositionAndFlags.w` 做
`hdr *= exposure`，root constants 结构已有该槽位。缺的只是"场景配置 →
pass 调用点"的数据通路。

任务 #21 同时要求：默认值必须保持 golden 不变；用非默认值练一遍
`RunRenderGate.bat -BlessGolden` 流程。

## Goals

- scene_config 新增 `tonemap` 配置块，字段 `exposure`（线性乘数，默认 1.0），
  经既有快照链（Scene → RenderScene → VisibleRenderFrame）逐帧传到 tone map pass。
- 默认值 1.0 下渲染结果与现状逐 bit 等价（golden 不变）。
- 用非默认曝光值实际走一遍 RenderGate FAIL → heatmap 确认 → 用户批准 →
  `-BlessGolden` 的完整流程。

## Non-goals

- 不做自动曝光 / eye adaptation、白点、对比度等其他 tone map 参数。
- 不改 tone map 算子（仍 ACES filmic）与 shader（`DeferredToneMap.hlsl` 零改动，
  shader cache 不失效）。
- 不做 Editor 的 scene_config 编辑 UI（scene_config 现状统一走 JSON 手编）。
- 不改 RenderGraph API / RHI / 绑定约定（root constant 槽位复用既有 `.w`）。

## Current implementation

- Entry points：`SceneRenderer::render_visible_frame` →
  `m_post_process_tone_map_pass.add_pass(graph, frame, scene_hdr_linear, output, view_context)`
  （`SceneRenderer.cpp` ~1581）。
- Modules：`PostProcessToneMapPass`（Function/Render）；配置链在
  Function/Scene（`SceneConfig.{h,cpp}`、`Scene.cpp` 的
  `deserialize/serialize_scene_render_config`、`set_render_config` sanitize）。
- Data flow：scene JSON `scene_config.*` → `SceneRenderConfig`（5 块，均为
  `Function/Render/*Config.{h,cpp}` 独立文件 + `make_default_*` + `sanitize_*`）
  → `VisibleRenderFrame::render_config` 逐帧值拷贝 → pass 只读 `frame.render_config.*`。
- Known constraints（scene-config.md）：新增配置块必须同步 Config 结构体 +
  sanitize/default、Scene.cpp 反/序列化、spec 表格；未知 json key 静默忽略；
  配置禁止跨帧缓存到成员。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function/Render | 新增 `ToneMapConfig`（`float exposure = 1.0f`）+ `make_default_tone_map_config` + `sanitize_tone_map_config`（非有限值回退 fallback；clamp 到 [0.01, 64.0]） | `ToneMapConfig.{h,cpp}`（新建，循既有每块一文件的惯例） |
| Function/Scene | `SceneRenderConfig` 加 `tonemap` 成员；default/equal 同步；`deserialize/serialize_scene_render_config` 加 `tonemap.exposure`；`set_render_config` sanitize 链加一条 | `SceneConfig.{h,cpp}`、`Scene.cpp` |
| Function/Render | add_pass 回调内 `1.0f` → `frame.render_config.tonemap.exposure`（add_pass 已收 `frame`，**签名不变**） | `PostProcessToneMapPass.cpp` |
| Base | 自测扩展：`test_render_scene_copies_scene_render_config_to_visible_frame` 覆盖 `tonemap.exposure` 非默认值透传；检查 `test_engine_ini_excludes_scene_render_config_sections` 的块名单是否需加 `tonemap` | `EngineSelfTests.cpp` |
| docs | `tonemap.md`（行为/配置节改写）、`scene-config.md`（块表加行）；SDD 结论回写 | `docs/specs/features/{tonemap,scene-config}.md` |

### API / contract changes

- scene JSON schema 新增块（这是本 SDD 定级 S2 的唯一原因）：

  ```json
  "scene_config": { "tonemap": { "exposure": 1.0 } }
  ```

  语义：pre-tonemap 线性乘数（直接对应 shader `hdr *= exposure`），非 EV 档。
  缺失块/字段 = 默认 1.0，与现状等价；旧场景文件无需迁移。
- 无 RenderGraph / RHI / shader 绑定约定变化。

### Backend impact

无差异面：曝光值经既有 inline root constants 下发，双后端路径完全一致；
shader 零改动，不涉及 DXC rewrite / SPIR-V 格式问题。验证仍按规双后端跑。

### Performance

零开销（常量来源从字面量变为帧配置字段）。PerfGate 阈值不需调整，按矩阵照跑。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 构建 + 自测 | 双 target 编译、EngineSelfTests（解析/透传/Engine.ini 排除） | `build_editor.bat Debug` + `build_sandbox.bat Debug` + run smoke 看自测日志 |
| 默认值回归 | exposure=1.0（含块缺失）golden 必须不变 | `RunRenderGate.bat`（期望 PASS，SSIM 与现基线一致） |
| 生效性 + Bless 流程演练 | Sandbox.scene.json 设 `exposure=1.25` → RenderGate 预期 FAIL，heatmap 应为全画面亮度差 → 用户确认画面正确后 `-BlessGolden` | `RunRenderGate.bat` → 用户确认 → `RunRenderGate.bat -BlessGolden` → 复跑 PASS |
| 双后端 smoke | 日志无 validation 报错 | `run.bat all Debug --smoke-test-seconds=5` |
| 性能门禁 | 按渲染改动矩阵 | `RunPerfGate.bat -Profile Standard` |

## Task breakdown

1. `ToneMapConfig.{h,cpp}` + `SceneConfig` 接入 + `Scene.cpp` 反/序列化与 sanitize
   链。验收：自测通过；序列化回写含 `tonemap` 块；非法值（NaN/负数）被钳制。
2. `PostProcessToneMapPass.cpp` 单行替换硬编码。验收：默认场景 RenderGate PASS
   golden 不变。
3. EngineSelfTests 扩展。验收：非默认 exposure 经快照链透传断言通过。
4. Bless 流程演练（exposure=1.25）。验收：FAIL→heatmap→用户确认→bless→PASS 全程
   走通；结束后按 Open question 1 的决议处置场景值与 golden。
5. spec 回写 + SDD 归档。验收：tonemap.md 不再写"固定 1.0"；scene-config.md
   块表 6 行。

## Risks

| Risk | Mitigation |
| --- | --- |
| 默认值路径意外改变画面（golden 漂移） | 默认 1.0 与原字面量逐 bit 相同、shader 未动；步骤 2 立即跑 RenderGate 验证 |
| json key 拼错静默用默认（既有限制） | spec 明示块名；自测覆盖正确 key 的解析 |
| 非法曝光值（0/负/NaN）导致黑屏或 NaN 扩散 | `sanitize_tone_map_config` clamp [0.01, 64.0] + 非有限回退默认 |
| bless 演练后基线状态混乱 | 演练用独立步骤执行，场景值与 golden 的最终状态先经用户决议（Open question 1）再收尾 |

## Open questions

已全部决议（2026-07-06 用户批准时）：

1. Bless 演练后**保留 exposure=1.25 + 新 golden**，作为配置真实生效的常驻证明。
2. json 块名用 `tonemap`。
3. 曝光语义用**线性乘数**（直接对应 shader `hdr *= exposure`）。
