---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Feature Spec: Temporal AA

## 行为

- 两部分组成：
  1. **相机 jitter**（`SceneRenderer::render_visible_frame`）：`taa_config.enabled` 时计算亚像素抖动并加到投影矩阵 `frame.projection[2][0] / [2][1]`，同时把上一帧 jitter 存入 `frame.taa_previous_jitter_ndc`（按 temporal view key 跨帧记录）。施加是**幂等**的：同一 `VisibleRenderFrame` 被重复渲染（prepare/submit 节奏不同步）时先撤销上次 jitter 再重加，禁止改回累加写法（SDD-0004 抖动根因）。
  2. **Resolve pass** `SceneTemporalAAResolvePass`（compute）：Bloom 之后、tone map 之前，输入当前 HDR、历史 HDR、GBuffer D 运动向量、深度，输出 resolved HDR 替换下游 `scene_hdr_linear`，同时写入下一帧历史。
- Resolve 算法：3×3 邻域 YCoCg 均值/方差 → `variance_gamma` 缩放的 AABB 对历史做 variance clipping（向中心线夹逼而非分量 clamp）；最近深度 texel 的运动向量膨胀（减轻轮廓 ghosting）；运动向量先减去 `jitter_curr - jitter_prev`（GBuffer 运动向量含 jitter 泄漏，必须去除否则静态画面漂移）；`history_blend` 混合，`luminance_weighting=true` 时按 1/(1+luma) 反亮度加权抗闪烁。历史永远写入干净 resolved 色（debug 输出不污染累积）。

## 确定性约定（RenderGate，不得破坏）

frame-dump 模式（`Application::get_frame_dump_path()` 非空）**强制 jitter = (0,0)**（`SceneRenderer.cpp` render_visible_frame 内）。这是 RenderGate（SDD-0001）抓帧确定性要求：TAA 时序抖动会使同参数两次 dump 出现全画面边缘噪声（SSIM 底约 0.989）。任何 TAA 改动必须保留此分支。

## 配置

`scene_config.temporal_aa`（`sanitize_temporal_aa_config` 兜底）：
`enabled`(false)、`jitter_sequence_length`(8)、`history_blend`(0.9)、`variance_gamma`(1.0)、`luminance_weighting`(true)、`debug_view`(Off，可选 MotionVectors/HistoryWeight/Variance)。

## 实现

- 类：`TemporalAAPass` / `TemporalAAConfig`（`project/src/engine/Function/Render/TemporalAAPass.{h,cpp}`、`TemporalAAConfig.{h,cpp}`）。
- Shader：`project/src/engine/Shaders/Deferred/TemporalAAResolve.hlsl` + `TemporalAACommon.hlsli`（entry `CSMain`，8×8 线程组）。
- Jitter：`temporal_aa_compute_jitter_ndc(frame_index, jitter_sequence_length, width, height)`——Halton(2)/Halton(3) 低差异序列（1-based 索引避开零样本），映射 [-0.5, +0.5] 像素再转 NDC；`jitter_sequence_length<2` 或尺寸为 0 时返回零。
- 历史：按 view key 双缓冲 RT（`TemporalAAHistoryEntry`）；`clear_history()` 场景切换时调用。

## 约束与已知限制

- 依赖 GBuffer D 运动向量（alpha 通道为 temporal 有效位）；无运动向量的绘制不受历史累积保护。
- Resolve shader 两个 UAV（`SceneTaaResolveOutput`/`SceneTaaHistoryWrite`）必须声明为 `RWTexture2D<min16float4>`：view 是 RGBA16F，`<float4>` 会让 DXC 推导 SPIR-V `Format=Rgba32f` 触发 Vulkan validation `StorageImage FormatMismatch`（undefined value）。`[[vk::image_format]]` 属性在运行时编译管线的 rewrite 阶段会被丢弃，不可用（SDD-0005）。
- 历史越界 / temporal 无效时回退当前帧色。
- ~~DX12 后端交互态整画面抖动~~（**已解决**，2026-07-06 SDD-0004）：根因是 `render_visible_frame` 对共享 `VisibleRenderFrame` **原地累加** jitter（`frame.projection[2][0] += ...`），而 prepare（逻辑线程）/submit（渲染线程）节奏不同步时同一 frame 会被重复渲染——渲染 fps 超过逻辑 tick 率的复用帧被施加 2× jitter（实测 DX12 约 30+ 次/秒复用，Vulkan 为 0，故仅 DX12 可见）。RenderDoc 取证：GBufferD 运动向量恒为 `jitter_uv(N) − 2×jitter_uv(N−1)`（committed prev VP 带 2× jitter），而补偿常量存原始 1× jitter 故欠补偿。修复：jitter 施加幂等化（重入时先撤销上次 jitter）。早前 SDD-0003 的 MAILBOX tearing 修正本身成立，但只是放大器而非根因；TAA resolve 数学经取证排除嫌疑。
- Resolve 不对当前帧做 unjitter（行业标准做法，避免重采样模糊）：收敛区域帧间残差 ≈ `(1-history_blend) × jitter delta`；`use_history=false` 的像素（无运动向量的绘制如天空、历史越界、首帧）以全振幅 jitter 输出，属设计内。

## 验证

- `RunRenderGate.bat`（同时覆盖抓帧禁 jitter 路径）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView 定位：`SceneGBufferD`（MotionVector 可视化）看运动向量，`SceneTemporalAAResolved`（LinearHDR）看 resolve 结果；scene_config `debug_view` 可切 MotionVectors/HistoryWeight/Variance。

## 历史

- docs/superpowers/plans/2026-06-18-taa-implementation.md
