# TAA（时域抗锯齿）接入方案

- 日期：2026-06-18
- 状态：待评审（仅方案，未写实现代码）
- 适用引擎：HASHEAEngine（DX12 + Vulkan 双后端）

## 1. 目标与已确认决策

接入工业级 TAA，消除几何边缘锯齿与高频着色噪声（含 GTAO/体积光的时域残留），并为后续超分/降噪打基础。

评审已确认的三项关键决策：

1. **空间位置**：TAA Resolve 在 **HDR 线性空间**进行，插入到 **Bloom 之后、Tonemap 之前**。
   - 理由：在 tonemap 前做 reprojection 与历史混合，避免在已压缩的 LDR 上做时域累积导致的色彩偏移；Bloom 读的是 jitter 后的当前帧，TAA 输出再喂给 Tonemap。
2. **历史约束**：采用 **Variance Clipping（YCoCg 空间 AABB）**，而非简单的 min/max neighborhood clamp。
   - 理由：方差包围盒对高对比边缘更稳，鬼影更少。
3. **本次范围**：**仅产出方案**，经批准后再分阶段写代码。

> 沟通语言：中文。未经明确同意不提交、不 push。

## 2. 现状盘点（已通过读码确认）

### 已具备
- **Motion Vector 已输出**：`GBufferD`（`RGBA16_SFLOAT`）的 RGB 为 `MotionVector3D`，A 为 `TemporalFlags`（见 `GBufferLayout.cpp`）。
  - 计算位置：`SurfaceStaticMeshGBuffer.hlsl:150-186`，`motion_vector = current_uv - previous_uv`（均来自 clip 矩阵）。
- **逐物体 previous-clip 已有**：`SceneStaticMeshInstanceData` 含 `object_to_clip` 与 `previous_object_to_clip`（`VertexLayoutPresets.h:13-24`）。
- **上一帧 VP 已缓存**：`SceneRenderer::SceneTemporalViewState.view_projection`（`SceneRenderer.h:64-69`），通过 `resolve_temporal_view_key / find_previous_temporal_view_state / commit_temporal_view_state` 维护，按 view 键控。
- **跨帧持久 RT 与 per-view ping-pong 模式已有先例**：`VolumetricLightingPass` 的 `m_history_entries`（`unordered_map<view_key, {std::array<RenderTarget,2>}>`，`VolumetricLightingPass.h:67-102`）。TAA 直接复用该模式。
- **全屏后处理 Pass 模板**：`PostProcessToneMapPass`（输入一张 HDR、输出一张 target，持 point/linear sampler）。
- **渲染链路明确**（`SceneRenderer.cpp`）：
  - 体积光 → `m_bloom_pass.add_passes(...)`（1323）→ `graph_resources.scene_hdr_linear = bloom_outputs.scene_hdr_linear`（1330）→ `m_post_process_tone_map_pass.add_pass(...)`（1375）。
  - **TAA 插入点：1330 与 1375 之间。**
  - `commit_temporal_view_state(temporal_view_key, frame)` 在 1418，TAA 的 history 提交与之并列处理。

### 缺失（需新增）
- **相机 jitter（亚像素抖动）完全不存在**：`SceneView` / `VisibleRenderFrame` 的 `projection` 均无抖动（`SceneView.h:23-34`，`RenderScene.cpp:276`）。这是 TAA 的核心前置。
- **TAA Pass 本身**（C++ + HLSL）。
- **HDR history 资源**（双缓冲、per-view）。
- **场景配置项**（`scene_config.taa`）。

## 3. 总体架构

```
GBuffer(含MotionVector, 由jittered VP光栅化)
        │
   Deferred Lighting / Env / Sky
        │
   Volumetric Lighting
        │
   Bloom  ── scene_hdr_linear(jittered, 当前帧) ──┐
        │                                          │
   ┌────▼─────────────────────────────────────────▼───┐
   │  TAA Resolve (compute / fullscreen, HDR linear)   │
   │   in:  current_hdr, history_hdr(上一帧输出),       │
   │        GBufferD(motion), GBufferDepth              │
   │   out: taa_output_hdr  →  也写入 history(双缓冲)    │
   └────┬──────────────────────────────────────────────┘
        │ taa_output_hdr
   Tonemap → DebugView → Overlay → Present
```

要点：
- TAA 的"当前帧"是 **jitter 后**的渲染结果；history 是 **上一帧 TAA 的输出**（已解抖动累积，稳定）。
- Tonemap 之后的 DebugView/Overlay 不变。

## 4. Jitter 注入设计

### 4.1 序列
- 使用 **Halton(2,3)** 低差异序列，长度建议 8（或 16，配置可选）。
- 每帧索引：`jitter_index = frame_index % jitter_sequence_length`。
- 偏移范围：`[-0.5, +0.5]` 像素，转换到 NDC：
  - `jitter_ndc.x = (halton.x * 2 - 1) / render_width`
  - `jitter_ndc.y = (halton.y * 2 - 1) / render_height`（注意 Y 方向与 NDC 约定一致）

### 4.2 注入方式（关键：只动 projection 的平移项）
对投影矩阵施加 NDC 平移（列主序 glm，`reverse_z` 不影响 xy 抖动）：
```
jittered_projection = projection;
jittered_projection[2][0] += jitter_ndc.x;   // 列2 行0
jittered_projection[2][1] += jitter_ndc.y;   // 列2 行1
jittered_view_projection = jittered_projection * view;
```

### 4.3 注入点
在 **`VisibleRenderFrame` 组装后、进入 `SceneRenderer` 光栅化前**注入。两种可选落点：

- **方案 A（推荐）**：在 `RenderScene::build_visible_render_frame`（`RenderScene.cpp:274-279`）写入 `view/projection/view_projection` 后，紧接着应用 jitter，并把抖动量写入 frame 新字段。
  - 需要把 `jitter`（或 `frame_index + 序列长度 + 是否启用`）作为参数传入，或由调用方在拿到 frame 后覆写。
- **方案 B**：在 `SceneRenderer::render` 入口拿到 `frame` 后，由渲染器统一施加 jitter（更内聚，TAA 全部逻辑收敛在 renderer 内，不污染 RenderScene）。

> 倾向 **方案 B**：jitter 是渲染器后处理特性，集中在 `SceneRenderer` 内可避免 `RenderScene` 关心 TAA。`frame.projection / frame.view_projection` 在 render 早期被就地替换为 jittered 版本，下游 geometry pass 自然继承。

### 4.4 新增 frame 字段
在 `VisibleRenderFrame`（`RenderScene.h:60`）追加：
```cpp
glm::vec2 taa_jitter_ndc{ 0.0f, 0.0f };       // 当前帧 jitter（NDC）
glm::vec2 taa_previous_jitter_ndc{ 0.0f, 0.0f };// 上一帧 jitter（NDC）
bool      taa_enabled = false;
```
`taa_previous_jitter_ndc` 由 `SceneTemporalViewState` 缓存并回填（与 `view_projection` 同一提交点）。

## 5. Motion Vector 与 jitter 耦合处理（核心正确性）

当前 motion vector 由 `current_uv(jittered) - previous_uv(jittered)` 得到（`SurfaceStaticMeshGBuffer.hlsl`），两帧 jitter 不同 → motion vector 中混入 `(jitter_curr - jitter_prev)` 抖动分量。若不处理，静止画面会被持续"抖"，TAA 反而引入噪声。

**处理策略（在 TAA Resolve shader 中解耦，几何 pass 不改）：**

reproject 历史 UV 时，从采样到的 motion vector 中减去 jitter 差：
```
// motion = uv_curr_jittered - uv_prev_jittered
// 真实几何运动 motion_geom = motion - (jitter_curr_uv - jitter_prev_uv)
float2 jitter_curr_uv = taa_jitter_ndc * float2(0.5, -0.5);        // NDC位移→UV位移
float2 jitter_prev_uv = taa_previous_jitter_ndc * float2(0.5, -0.5);
float2 motion_geom = motion_sample.xy - (jitter_curr_uv - jitter_prev_uv);
float2 history_uv = current_uv - motion_geom;
```
> 符号需在实现期对齐 `AshClipToUv` 的 Y 约定（与体积光 reproject 中 `float2(0.5, -0.5)+0.5` 一致）。这是实现阶段必须用真机验证的点。

此外，**采样当前帧用于 neighborhood 统计时**，理想上应对当前帧做去抖（dilate/最近深度选取邻域），本方案先用标准 3×3 邻域 + depth-dilation 选取 motion，足够稳。

## 6. TAA Resolve 算法

输入：`current_hdr`（Bloom 输出）、`history_hdr`、`GBufferD`（motion+flags）、`SceneDepth`。
输出：`taa_output_hdr`（同时作为下一帧 history 写入）。

步骤（每像素）：
1. 读当前像素 `c = current_hdr[uv]`。
2. **Depth dilation**：在 3×3 邻域内按最近深度（reverse_z 下取最大）选出代表像素，用其 motion vector 做 reproject，减少边缘拖影。
3. 计算 `history_uv`（按第 5 节解耦 jitter），双线性采样 `history_hdr`。
4. **历史有效性**：`history_uv` 越界 / `TemporalFlags==0`（无效或刚生成）/ 首帧 → 历史无效，直接输出当前帧。
5. **Variance Clipping（YCoCg）**：
   - 当前帧 3×3 邻域转 YCoCg，求均值 `mu` 与标准差 `sigma`。
   - AABB：`[mu - gamma*sigma, mu + gamma*sigma]`，`gamma` 约 1.0（配置 `variance_gamma`）。
   - 把 `history`（转 YCoCg）clip 到该 AABB（线段裁剪，非逐分量 clamp，减少色偏），再转回 RGB。
6. **混合**：`out = lerp(c, history_clipped, alpha)`，`alpha` 即历史权重（配置 `history_blend`，典型 0.9）。
   - 可选 luma 加权抗闪烁：`weight = 1/(1+luma)`，对当前/历史分别加权（配置 `luminance_weighting` 开关）。
7. 写 `taa_output_hdr[uv] = out`，并写 history 双缓冲的写入面。

YCoCg 转换、HG 等小工具放入新 `TemporalAACommon.hlsli`。

## 7. History 资源管理

完全复用 `VolumetricLightingPass` 的 per-view ping-pong 模式：
```cpp
struct TaaHistoryEntry {
    uint32_t width = 0, height = 0;
    bool valid = false;
    glm::vec2 previous_jitter_ndc{0.0f};
    std::array<std::shared_ptr<RenderTarget>, 2> color{};  // ping-pong, RGBA16_SFLOAT
    uint32_t write_index = 0;
};
std::unordered_map<uint64_t /*view_key*/, TaaHistoryEntry> m_history_entries;
```
- view_key 复用 `SceneRenderViewContext` 的 view 标识（与 volumetric/temporal 一致）。
- 分辨率变化 / 首帧 / view 不匹配 → 标记 history 无效（输出纯当前帧）。
- 持久 RT 用 `renderer->create_render_target()`（跨帧），TAA 输出本身可用 graph transient texture，再 copy/直接写入 history RT；或直接以 history 写入面作为输出 RT 喂给 tonemap（省一次拷贝，推荐）。
- `clear_history()` 接口，供分辨率重建/场景切换调用。

## 8. 配置项（场景 JSON）

在 `Sandbox.scene.json` 的 `scene_config` 增加：
```json
"taa": {
  "enabled": true,
  "jitter_sequence_length": 8,
  "history_blend": 0.9,
  "variance_gamma": 1.0,
  "luminance_weighting": true,
  "debug_view": "Off"
}
```
对应新增 `TemporalAAConfig`（仿 `VolumetricLightingConfig` / `BloomConfig`），含从 JSON 解析与默认值工厂；并入 `SceneRenderConfig`。

## 9. Root Constants 布局（遵守 64-DWORD / 256B 限制）

TAA Resolve 的 cbuffer（`AshRootConstants : register(b0)`）建议 ≤ 6 个 vec4：
```
vec4 config0  = { render_width, render_height, history_blend, variance_gamma }
vec4 config1  = { jitter_uv.x, jitter_uv.y, prev_jitter_uv.x, prev_jitter_uv.y }
vec4 config2  = { reverse_z, history_valid, luminance_weighting, debug_view }
```
- 纹理/采样器各占 1 DWORD 描述符表项；当前规模远低于 64-DWORD 上限，安全。
- 严格避免重蹈体积光的 root signature 溢出（HRESULT 0x80070057）。

## 10. 文件改动清单

新增：
- `project/src/engine/Function/Render/TemporalAAConfig.h`
- `project/src/engine/Function/Render/TemporalAAPass.h`
- `project/src/engine/Function/Render/TemporalAAPass.cpp`
- `project/src/engine/Shaders/Deferred/TemporalAAResolve.hlsl`
- `project/src/engine/Shaders/Deferred/TemporalAACommon.hlsli`

修改：
- `RenderScene.h`：`VisibleRenderFrame` 加 jitter 字段；`SceneRenderConfig` 并入 `taa`。
- `SceneRenderer.h`：加 `TemporalAAPass m_taa_pass`；`SceneTemporalViewState` 加 `previous_jitter_ndc`。
- `SceneRenderer.cpp`：jitter 注入（render 入口）；TAA pass 插入（1330↔1375 之间）；history/jitter 提交（≈1418）；debug view 注册。
- 场景配置解析处（`rebuild_render_config_from_scene` 链路）：解析 `taa`。
- `Sandbox.scene.json`：加 `taa` 配置块。
- premake/着色器清单：登记新 HLSL（与现有 Deferred shader 同机制）。

不改：
- `SurfaceStaticMeshGBuffer.hlsl`（motion vector 保持现状，jitter 解耦在 resolve 完成）。
- `GBufferLayout.cpp`（motion vector 已就位）。

## 11. 分阶段实施（批准后执行）

1. **阶段一：Jitter 基建**
   - 加 Halton 工具、frame 字段、render 入口注入、`SceneTemporalViewState` 回填 prev jitter。
   - 验收：开 jitter 但不混合（直通），画面应轻微抖动 → 证明 jitter 生效。
2. **阶段二：TAA Pass 骨架 + reproject**
   - 新建 Pass/HLSL，history 资源，reproject + 解耦 jitter，alpha 固定混合（先不裁剪）。
   - 验收：静止画面稳定不抖；运动有可接受拖影。
3. **阶段三：Variance Clipping + 抗闪烁**
   - 加 YCoCg 方差裁剪、depth-dilation、luma 加权。
   - 验收：边缘锯齿消除，鬼影/拖影显著降低。
4. **阶段四：配置/Debug/双后端**
   - 接 JSON 配置、debug view、`clear_history`。
   - 跑 `ash-engine-validation-loop`（Sandbox/Editor × Vulkan/DX12 烟测），确认 0 validation/leak、后端一致。

## 12. 风险与注意

- **jitter 符号/Y 轴约定**：必须真机比对 `AshClipToUv`，否则 motion 解耦方向错 → 持续抖动。阶段一/二重点验证。
- **首帧与 view 切换**：history 无效路径要稳，避免黑屏/NaN。
- **HDR 数值范围**：variance clipping 在高动态值下需对极亮像素做 luma 加权，否则火花点闪烁。
- **与现有时域特性叠加**：GTAO/体积光各自有时域累积；TAA 在其下游，整体应更稳，但需观察是否过度模糊（必要时降低 `history_blend`）。
- **性能**：全屏 compute，3×3 邻域采样，开销低；history 多一张 `RGBA16_SFLOAT` 全屏 RT（双缓冲）。

---
评审通过后，我从阶段一开始实现，不一次性铺开。
