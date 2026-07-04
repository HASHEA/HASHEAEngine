# Feature Spec: Ambient Occlusion（SSAO / HBAO / GTAO）

## 行为

`AmbientOcclusionPass` 在 GBuffer 之后、光照累积之前运行，输入 GBuffer + 深度，
输出 `SceneAmbientOcclusion`（RGBA8），被 Base/Point/Spot 光照 pass 与环境光照 pass 采样。
`mode=Off` 时不建 pass，输出注册为 1x1 中性白纹理（AO=1）。

pass 链（按 config 逐级可选）：

1. `SceneAmbientOcclusionPass`：按 mode 选 SSAO/HBAO/GTAO 全屏 pass 生成 raw AO；
   `half_resolution=true` 时按输出 1/2 分辨率。
2. `SceneAmbientOcclusionBlurPass`（`blur=true`）：深度感知模糊。
3. `SceneAmbientOcclusionTemporalPass`（`temporal=true`）：逐 view 双缓冲外部 history
   （AO + meta 两组 ping-pong RT），运动向量重投影，深度/法线阈值拒绝历史；
   mode/quality/half_resolution/blur 变化或 view 尺寸变化时重置历史。
4. `SceneAmbientOcclusionDebugPass`（`debug_view != Off`）：生成全分辨率调试图并
   **替换整帧场景输出**（SceneRenderer 跳过光照后续管线，直接进 ToneMap）。

quality 映射（`AmbientOcclusionPass.cpp`）：sample/direction/step 数
Low=6/4/3，Medium=10/6/4，High=16/8/6。采样旋转使用逐像素 interleaved gradient noise
（`AshAOStableNoise`，抓帧确定性、无时间抖动）。

## 配置

`AmbientOcclusionConfig`（`AmbientOcclusionConfig.h`），scene json `scene_config.ambient_occlusion`，
经 `sanitize_ambient_occlusion_config` 清洗：

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| mode | Off | Off / SSAO / HBAO / GTAO |
| quality | Medium | Low / Medium / High（采样数档位） |
| radius | 1.5 | 世界空间采样半径 |
| intensity / power | 1.0 / 1.0 | 遮蔽强度 / 幂次曲线 |
| half_resolution | false | 半分辨率计算 |
| blur | true | 深度感知模糊 |
| temporal | false | 时域滤波开关 |
| temporal_blend | 0.85 | 历史混合权重 |
| temporal_depth_threshold / temporal_normal_threshold | 0.01 / 0.75 | 历史拒绝阈值 |
| debug_view | Off | RawAO / FinalAO / Depth / Normal / MotionVector / TemporalAO / HistoryWeight |

## 实现

- pass 类：`AmbientOcclusionPass.h/.cpp`（`project/src/engine/Function/Render/`）。
- shader（`project/src/engine/Shaders/Deferred/`）：`AmbientOcclusionCommon.hlsli`、
  `AmbientOcclusionSSAO.hlsl`、`AmbientOcclusionHBAO.hlsl`、`AmbientOcclusionGTAO.hlsl`、
  `AmbientOcclusionBlur.hlsl`、`AmbientOcclusionTemporal.hlsl`、`AmbientOcclusionDebug.hlsl`。
- 参数经 inline root constants 下发（viewport、radius、quality 档位、temporal 阈值等）。

## 约束与已知限制

- temporal history 为 pass 内部持有的外部 RT，仅支持单 view 追踪（view_id 变化即重置）。
- debug_view 开启时直接替换场景输出，属破坏性调试路径，不能与正常画面同屏。
- 噪声函数为屏幕像素坐标驱动的稳定噪声，无逐帧 jitter；改动它会同时影响 RenderGate golden。

## 验证

- `RunRenderGate.bat`（golden SSIM 0.995 / 跨后端 0.99）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView：`Ambient Occlusion Raw`、`Ambient Occlusion`、`Ambient Occlusion Temporal`。
- 更细粒度用 scene_config 的 `ambient_occlusion.debug_view` 专用调试视图
  （RawAO/FinalAO/Depth/Normal/MotionVector/TemporalAO/HistoryWeight）。

## 历史

- `docs/superpowers/specs/2026-05-20-ambient-occlusion-design.md`
- SSAO banding 修复：commit `ddeae97`（改用 stable interleaved gradient noise）。
