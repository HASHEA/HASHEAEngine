# Mini SDD: Terrain LOD/morph 一致的几何法线

## Status

Superseded

## Goal

消除 Terrain 平面与坡面上跟随 Component/LOD 周期出现的密集条纹。GBuffer 几何法线必须描述实际光栅化的、已经完成 LOD geomorph 的曲面，而不是从原始全分辨率高度场另算一张不一致的曲面。

## Non-goals

- 不修改 RHI、RenderGraph、shader binding、root constants、实例打包或场景格式。
- 不实现 normal pyramid/atlas，也不扩展邻居 LOD/morph metadata。
- 不顺带修复方向光静态阴影缓存失效、Editor grid 或实时雕刻。
- 不新增或 bless Terrain golden、RenderGate golden 或 PerfGate baseline。

## Files

- `project/src/engine/Shaders/Terrain/TerrainSurface.hlsl`
- `project/src/tests/Terrain/terrain_render_graph_tests.cpp`
- `docs/specs/features/terrain.md`
- 本 Mini SDD

## Approach

在 Terrain GBuffer permutation 中，用已经 geomorph 的 object-space position 计算并传递 translation-free world-space offset：只应用 object-to-world 的线性部分，导数与绝对 world position 等价但不携带会放大远原点插值误差的平移。Pixel shader 通过 `ddx` / `ddy` 得到当前光栅三角形的 world-space 几何法线；两条 derivative 先各自检查有限正 length squared 并单位化，再用单位方向叉积的无量纲长度做相对角度退化判定。变换后的 Terrain local +Y 也执行相同安全单位化，非法时回退 world +Y，随后统一法线朝向并作为退化 fallback；现有切线空间材质法线混合继续复用该几何法线。移除 GBuffer vertex path 对 raw `AshTerrainLocalNormal` 的调用；depth-only 与 LOD debug permutation 保持原路径与资源边界。先增加 shader 源码契约 RED，再修改 HLSL。

该方案原位替换现有 normal varying，插值带宽不增加，并删除每顶点四次原始高度法线采样。代价是少量 pixel derivative ALU，粗 LOD 会呈现与真实网格一致的几何分面；由双后端 A/B 与性能门禁判定是否可接受。

## Verification

- `RunTests.bat Debug --test-case="Terrain GBuffer normals follow the morphed raster surface"`
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `build_editor.bat Debug` / `build_sandbox.bat Debug`
- `build_editor.bat Release` / `build_sandbox.bat Release`
- `RunArchGate.bat`
- 双后端 Terrain 定向 readiness 与同相机 Final / `SceneGBufferE` 非 bless A/B；检查 Component 边界无新 seam，日志无 validation/debug-layer/error。
- `RunRenderGate.bat`（不得 bless）
- `RunPerfGate.bat -Profile Standard`
- 由人类测试者在 Vulkan 与 DX12 Editor 中确认平面、坡面与雕刻区域无原条纹；AI 不代签。

## Risk / rollback

主要风险是导数法线方向、退化三角 fallback 或粗 LOD 分面。源码契约、双后端 shader 编译、GBuffer A/B 和 RenderGate 覆盖这些风险；若任一后端产生 seam、validation 或不可接受画面，回退本 shader 变更，不改变任何持久数据。

## Result

- RED：focused Debug 用例在旧 shader 上按预期失败，报告缺少 `position_ws : TEXCOORD0`、仍存在 `normal_ws : TEXCOORD0`，且 GBuffer VS 未写入 world position。
- 数值复核 RED：第一版仍传递含平移的绝对 world position、以固定 world-unit threshold 判断叉积退化，并裸 `normalize` transformed local +Y；收敛后的 source contract 对这些不安全路径按预期失败。
- 实现：GBuffer varying 原位改传实际 geomorph 后的 translation-free world-space offset；PS 安全单位化 up 与两条 pixel derivative direction，用相对角度退化判定生成定向几何法线，并移除 Terrain surface 对 raw `AshTerrainLocalNormal` 的调用。
- GREEN：`RunTests.bat Debug --test-case="Terrain GBuffer normals follow the morphed raster surface"` 通过，1/1 用例、23/23 断言成功；既有 shader binding focused 用例通过，1/1 用例、21/21 断言成功。
- Fresh 全量：`RunTests.bat Debug` / `Release` 分别通过 `419/419` test cases、`24897/24897` assertions 与 `419/419` test cases、`24896/24896` assertions；Editor/Sandbox Debug/Release 构建、`RunArchGate.bat` 与 AIDevDoctor ValidatePlan 均通过。
- 非 bless `RunRenderGate.bat` PASS：Sandbox Vulkan/DX12/cross SSIM 为 `0.996278 / 0.996177 / 0.999747`，Particles 为 `1 / 1 / 1`；未更新 golden。Standard PerfGate 四组合均 PASS、`failures=[]`、`warnings=[]`、GPU timing coverage 为 `1.0`；四格 baseline 均为 `MISSING`，因此该项只证明绝对门槛与运行健康，不声称历史性能比较。
- 双后端 readiness capture 均由资源就绪信号触发并 clean exit。标准 Terrain Final 与 `SceneGBufferE` 的 Vulkan/DX12 对比均为 SSIM `1.0`；使用缺陷诊断时的同一 shifted scene，Final cross-backend SSIM `1.0`、仅 194 像素有不超过 2 的通道差，GBuffer 仅 43 像素有不超过 1 的通道差。
- 该次同场景 pre/post 差异只隔离了 fixture 坡面带，不能证明用户随后在平面、远距和新雕刻曲面报告的规则纹路已消失；因此不得把剩余条纹归因为 fixture 实际几何。
- 本设计以逐三角 derivative normal 为目标，已被用户批准的 [Terrain 编辑取景与表面稳定性 S2](SDD-2026-07-15-terrain-authoring-visual-stability.md) 取代。新合同使用全分辨率 canonical shading gradient、粗邻边端点插值和 PS 归一化；本 SDD 标记 Superseded，不再作为人工验收依据。
