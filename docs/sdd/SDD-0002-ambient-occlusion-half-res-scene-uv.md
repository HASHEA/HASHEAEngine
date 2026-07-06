# Mini SDD: AO 半分辨率场景纹理 UV 对齐

## Goal

修复 Ambient Occlusion 以半分辨率运行、同时采样全分辨率场景纹理时出现的可见条纹。
当 AO 输出目标被降采样时，AO pass 读取 SceneDepth / GBuffer 必须落在稳定的全分辨率
texel center 上，而不是落在 texel 边界附近。

## Non-goals

- 不重做 SSAO / HBAO / GTAO 算法或 quality 档位。
- 不新增 depth-aware AO upsample pass。
- 不改变 deferred lighting 消费 `SceneAmbientOcclusion` 的契约。
- 不修改 Editor 代码。

## Files

允许的实现范围：

- `project/src/engine/Function/Render/AmbientOcclusionPass.cpp`
- `project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli`
- `project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl`
- `project/src/engine/Shaders/Deferred/AmbientOcclusionTemporal.hlsl`
- `project/src/engine/Shaders/Deferred/AmbientOcclusionDebug.hlsl`
- `project/src/engine/Base/EngineSelfTests.cpp`

文档范围：

- `docs/specs/features/ambient-occlusion.md`

## Approach

`AmbientOcclusionPass` 通过 root constant 下发一个标志，告诉 shader 当前 AO render target
是否为降采样目标、而 SceneDepth / GBuffer 是否仍为全分辨率。`AmbientOcclusionCommon.hlsli`
根据该标志，在采样 SceneDepth、GBufferE 和 motion vector 前，把场景纹理 UV 偏移半个
全分辨率 scene texel。

共享的 AO surface 加载路径（`AshAOLoadSurface`）统一持有调整后的 scene UV，使 SSAO、
HBAO、GTAO、blur 深度权重、temporal metadata 和 AO debug view 都遵守同一套采样契约。
debug pass 自身以全分辨率输出，因此禁用半分辨率偏移。

`EngineSelfTests.cpp` 新增源码级契约测试，避免后续改动绕过调整后的场景纹理采样 helper。

## Verification

按 `docs/VERIFY.md` 对渲染 pass / shader 改动的要求，需要执行：

```bat
build_sandbox.bat Debug
build_editor.bat Debug
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
```

专项检查：

- 在默认 Sandbox 场景中对比 `half_resolution=true` 与 `half_resolution=false` 的 AO。
- 检查 AO debug view：RawAO、FinalAO、Depth、Normal、MotionVector、TemporalAO、HistoryWeight。
- 检查 `product/logs`，确认 Vulkan / DX12 validation 或 debug layer 没有报错。

## Risk / rollback

风险限制在 AO pass 族内。改动复用现有 root constant 通道（`AshAOParams0.w`）承载
half-resolution 场景纹理采样标志；当前 shader 不依赖该通道做 mode 选择，C++ 仍然直接选择
SSAO / HBAO / GTAO program。

回滚方式是正常 git revert AO 半分辨率 UV 对齐提交。如果 RenderGate 出现符合预期的画面变化，
更新 golden 必须经过用户明确确认，并通过 `RunRenderGate.bat -BlessGolden` 执行。
