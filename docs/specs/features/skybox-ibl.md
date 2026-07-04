# Feature Spec: Skybox 与 IBL 环境光照

## 行为

两个 pass 消费同一份环境贴图运行时资源（`EnvironmentMapRuntimeResource`）：

1. **EnvironmentLightingPass**（`SceneDeferredEnvironmentLightingPass`）：在逐光源 pass 之后、
   Composite 之前，全屏 Additive 叠加 IBL——irradiance cubemap 提供漫反射、
   prefiltered specular cubemap + BRDF LUT 提供镜面项，乘 AO 后写入
   lighting_diffuse / lighting_specular 累积 MRT。环境不存在、`affect_lighting=false`
   或资源未 Ready 时跳过。旋转/强度取自场景环境组件（`rotation_degrees`、
   `intensity * lighting_intensity`）。
2. **SkyBackgroundPass**（`SceneSkyBackgroundPass`）：在 Composite 之后，新建
   `SceneDeferredSceneHDRWithSky`（RGBA16F），背景像素（远平面深度）采样 radiance cubemap，
   其余像素透传场景 HDR，替换 `scene_hdr_linear` 供体积光/Bloom/TAA/ToneMap 继续。
   环境不存在、`visible_background=false` 或 radiance 未就绪时跳过。

### 资源加载链

环境资源来自 AshIBL 容器（magic `ASHIBL`，version 1）：radiance / irradiance /
prefiltered specular 三个 cubemap + BRDF LUT（+ 可选缩略图）。烘焙离线完成
（`EnvironmentMapBaker::bake_to_cooked_data` / 命令行 `--bake-ashibl`）。
`RenderAssetManager::request_environment_map_asset` 加载顺序：

1. 直接读 `.ashibl` 资产路径；
2. 失败且给了源纹理路径时，若 `RuntimeBakeCache=true`，按源文件内容 hash 查找预烘焙缓存文件
   （`make_environment_map_source_cache_path`）；运行时**不现场烘焙**，缓存缺失只告警一次；
3. 都失败则用内置 fallback 环境贴图。

## 配置

`Engine.ini [EnvironmentLighting]`（`EnvironmentLightingConfig`，`EnvironmentMapAsset.h`）：

| 字段 | ini 当前值 | 含义 |
| --- | --- | --- |
| RuntimeBakeCache | true | 允许按源纹理 hash 查预烘焙缓存 |
| DefaultRadianceSize | 1024 | radiance cubemap 尺寸 |
| DefaultIrradianceSize | 64 | irradiance cubemap 尺寸 |
| DefaultPrefilterSize / DefaultPrefilterMipCount | 256 / 8 | prefiltered specular 尺寸 / mip 数 |
| DefaultBRDFLUTSize | 256 | BRDF LUT 尺寸 |
| DefaultSampleCount | 1024 | 烘焙采样数（代码默认 256，ini 覆盖为 1024） |

场景侧开关在环境组件上：`affect_lighting`（IBL）、`visible_background`（天空）、
`rotation_degrees`、`intensity`、`lighting_intensity`。

## 实现

- `EnvironmentLightingPass.h/.cpp`、`SkyBackgroundPass.h/.cpp`、`EnvironmentMapAsset.h/.cpp`
  （AshIBL 读写 + 运行时配置）、`EnvironmentMapBaker.h/.cpp`（烘焙）。
- shader（`project/src/engine/Shaders/Deferred/`）：`EnvironmentCommon.hlsli`、
  `DeferredEnvironmentLighting.hlsl`、`SkyBackground.hlsl`。
- IBL pass 绑定 `SceneEnvironmentIrradiance` / `SceneEnvironmentPrefilteredSpecular` /
  `SceneEnvironmentBRDFLUT`；天空 pass 绑定 `SceneEnvironmentRadiance`。

## 约束与已知限制

- AshIBL 无压缩格式（AshIBLCompression::None），HDR payload 为 RGBA16F。
- 运行时无烘焙路径：新源纹理必须先 `--bake-ashibl` 生成缓存，否则回退 fallback。
- 每帧单一环境；资源以 `(ibl_asset_path, source_texture_path)` 为 key 缓存于 RenderAssetManager。

## 验证

- `RunRenderGate.bat`（golden SSIM 0.995 / 跨后端 0.99）+ `RunPerfGate.bat -Profile Standard`。
- RenderDebugView：`Deferred Lighting Diffuse` / `Deferred Lighting Specular` 看 IBL 叠加，
  `Scene HDR Linear` 看天空合成结果；缓存缺失/回退看 `product/logs` 一次性告警。

## 历史

- `docs/superpowers/specs/2026-05-25-skybox-ibl-design.md`
