/*
	filename:       rcpi_scene.h
	author:         Ming Dong
	date:           2016-Aug-15
	description:
*/
#pragma once

#include "rcmod_osdep.h"
#include "rcplugins.h"

struct RCMOD_WaterData
{
	RCMOD_Float3x3 AvgWaterHeight;
	RCMOD_Float3x3 ThresholdsAboveWater;
	int nOceanSurfaceIndex;
};

struct RCMOD_GIParameters
{
	RCMOD_Float4x4 VoxelizationParam;
	RCMOD_Float4x4 IndirectDiffuseQualityParam;
	RCMOD_Float4x4 IndirectSpecularQualityParam;
	RCMOD_Float4x4 IndirectIrradianceQualityParam;
	RCMOD_Float4x4 AreaLightQualityParam;
	RCMOD_Float4x4 SSAOAndDebugParam;
	RCMOD_Float4x4 DiffuseTracingParam;
	RCMOD_Float4x4 SpecularTracingParam;
	RCMOD_Float4x4 AreaLightTracingParam;
};

class RCPI_Scene : public RCPluginInterface
{
public:
	//scene render functions
	virtual bool	renderMainCamera_SceneRender(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ColorTexture, RCMOD_Texture* i_DepthTexture, RCMOD_Float4 i_ViewPort, int RenderType, float i_EnableZTest, float i_EmissiveMultiplier, RCMOD_Texture* i_GrassTexture = NULL) = 0;

	virtual bool	RenderOITTransparent(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture0,
		RCMOD_Texture* i_ColorTexture1,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_OITOpaqueDepthTexture,
		RCMOD_Float4 i_ViewPort,
		int i_RenderQueue) = 0;

	virtual bool RenderRealOIT(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture
	) = 0;

	virtual bool RenderAIMode(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_NormalTexture,
		float i_PicNum,
		float i_RenderType,
		RCMOD_Texture* i_InputTexture = nullptr
	) = 0;

	virtual bool PassContactShdowOptions(
		float i_ContactShadowNearLength,
		float i_ContactShadowFarLength,
		float i_ContactShadowNearDistance,
		float i_ContactShadowFarDistance,
		int   i_nNumSteps,
		bool  i_bEnableContactShadow,
		float i_fContactShadowBias,
		bool  i_bEnablePCF,
		bool  i_bUseDither,
		bool  i_bRemoveSelfShadow,
		float i_fPCFHighDistance,
		float i_fPCFLowDistance,
		float i_fMinShadowDarkness
	) = 0;

	virtual bool PassLightShaftShdowOptions(
		float i_ShaftShdowLength,
		float i_ShaftShdowNearDistance,
		float i_ShaftShdowFarDistance,
		int   i_nNumSteps,
		bool  i_bEnableShaftShdow,
		bool  i_bEnableShaftShdowBlur,
		bool  i_bEnableShaftShdowEnv,
		bool  i_bEnableLightModify,
		float i_fShaftShdowBias,
		float i_fGrassHeight,
		float  i_fSelfShadowDarkness,
		float i_fShadowDarkness,
		float i_DownSampler,
		float i_EnvFactor,
		float i_ShadowFadeout
	) = 0;

	virtual bool PassSDFShdowOptions(
		float i_SoftShadowQuality,
		float i_DownSampler,
		float i_SunLightBiasScale,
		int   i_RayTraceQuality,
		bool  i_bEnableSDFShadow,
		bool  i_bEnablePointLightSDFShadow,
		bool  i_bEnableHFShadow,
		int i_SunLightSDFShadowType,
		int i_PointLightSDFShadowType
	) =0;

	virtual bool DebugTextureByRange(
		RCMOD_Texture* i_GBufferA,
		RCMOD_Texture* i_GBufferB,
		RCMOD_Texture* i_GBufferE,
		bool  i_bEnableDebugColor,
		float i_fDebugRangeLowerBound,
		float i_fDebugRangeUpperBound,
		bool i_bDebugR,
		bool i_bDebugG,
		bool i_bDebugB,
		bool i_bDebugA,
		RCMOD_Float4 i_DebugColor
	) = 0;

	virtual bool RenderClusterForwardDebug(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture, float i_DebugType) = 0;

	virtual bool	RenderAmbientOcclusion(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ColorTexture, RCMOD_Texture* i_DepthTexture, RCMOD_Texture* i_NormalTexture, RCMOD_Float4x4 const& i_matProj, RCMOD_Float4x4 const& i_matView) = 0;

	/*
	* RenderAmbientOcclusion
	*
	* i_AOParam:
	* 00-> m_AORadius;
	* 01->m_AOBias;
	* 02->m_BlurAO;
	* 03->m_BlurSharpness;
	* 04->m_PowerExponent;
	* 05->m_NearAO;
	*/
	virtual bool	RenderAmbientOcclusionWithParam(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_NormalTexture,
		RCMOD_Float4x4 const& i_matProj,
		RCMOD_Float4x4 const& i_matView,
		RCMOD_Float4x4 const& i_AOParam) = 0;

	/*
	*	Render Lights
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, ResovlemainDepth
	*	2. output: LightDiffuse, LightSpecular, DepthStencilView
	*/
	virtual bool	RenderLights(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	Render Lights
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, ResovlemainDepth
	*	2. output: LightDiffuse, LightSpecular, DepthStencilView
	*/
	virtual bool	RenderLightsRTShadow(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	RTX Render Lights
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, ResovlemainDepth
	*	2. output: LightDiffuse, LightSpecular, DepthStencilView
	*/
	virtual bool	RTXRenderLights(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		float* i_pADC,
		float* i_pASC,
		int i_bShadow) = 0;

	/*
	*	RTX Render Non-Opaque(OIT, Softmask.etc.) Object
	*	1. input: GBufferA, GBufferB, GBufferC, ResovlemainDepth, OITMask, SceneColor
	*	2. output: DirectOIT
	*/
	virtual bool	RTXRenderForwardNonOpaque(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount
	) = 0;

	/*
	*	RTX Render Reflect Non-Opaque Object
	*	1. input: GBufferA, GBufferB, GBufferC, ResovlemainDepth, ReflectionRayDirAndHitT, OITMask, ReflectRT
	*	2. output: ReflectOIT
	*/
	virtual bool	RTXRenderReflectNonOpaque(
		RCOSRendererData* i_RendererData,
		float* i_pfParameters,
		int i_ParameterCount,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		int i_bShadow
	) = 0;

	/*
	*	Reflection
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, ResovlemainDepth£¬ AmbientOcclusion, GBufferE
	*	2. output: LightDiffuse, LightSpecular, DepthStencilView
	*/
	virtual bool	RenderReflection(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	RTX Reflection, IBL in Reflection
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, ResovlemainDepth
	*	2. output: IBLDiffuse, IBLSpecular, DepthStencilView
	*/
	virtual bool	RTXRenderReflection(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	SSS
	*	1. input: GBufferB, GBufferC, GBufferD, AmbientOcclusion, ResovlemainDepth£¬LightDiffuse, IBLDiffuse
	*	2. output: SSS, DepthStencilView
	*/
	virtual bool	RenderSSSDeferred(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	RTX Compose Lighting Diffuse and IBL Diffuse
	*	1. input: GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, LightDiffuse, IBLDiffuse
	*	2. output: SSS, DepthStencilView
	*/
	virtual bool	RTXRenderSSSDeferred(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	SSGI
	*	1. input: GBufferB, GBufferC, GBufferD, AmbientOcclusion, ResovlemainDepth£¬LightDiffuse, IBLDiffuse
	*	2. output: SSS, DepthStencilView
	*/
	virtual bool	RenderSSGI(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		RCMOD_Float4 const& i_Param0) = 0;

	/*
*	SSGI
*	1. input: GBufferB, GBufferC, GBufferD, AmbientOcclusion, ResovlemainDepth£¬LightDiffuse, IBLDiffuse
*	2. output: SSS, DepthStencilView
*/
	virtual bool	RenderVolumeGI(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
*	SSGI
*	1. input: GBufferB, GBufferC, GBufferD, AmbientOcclusion, ResovlemainDepth£¬LightDiffuse, IBLDiffuse
*	2. output: SSS, DepthStencilView
*/
	virtual bool	RenderVolumeGIStatic(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	virtual bool	RenderPathTracing(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	virtual bool	RenderCloudSeaTD(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_SceneColorTexture,
		RCMOD_Texture* i_ResolvedDepthTexture,
		RCMOD_Texture* i_Density3DTexture,
		RCMOD_Texture* i_Noise3DTexture,
		RCMOD_Float4 i_Param0,
		RCMOD_Float4 i_Param1,
		RCMOD_Float4x4 const& i_Param2
	) = 0;

	/*
	*	Render Lights
	*	1. input: GBufferA, GBufferB, GBufferC, AmbientOcclusion, SSS£¬LightDiffuse, IBLDiffuse, ResolveDepth
	*	2. output: SSS, DepthStencilView
	*/
	virtual bool	RenderCompositeResult(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	SSR
	*	1. input: ResolveDepth, GBufferA, SceneColor
	*	2. output: IBLSpecular
	*/
	virtual bool	RenderSSR(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	virtual bool	RenderGIRefelctions(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	/*
	*	Bokeh
	*	1. input: SceneColor, DepthBlur, BokehShape
	*	2. output: Bokeh
	*	3. i_BokehParam
	*		x: BokehBrightnessThreshold
	*		y: BokehBlurThreshold
	*		z: BokehFalloff
	*		w: MaxBokehSize
	*/
	virtual bool	RenderBokeh(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		RCMOD_Float4	i_ViewPort,
		RCMOD_Float4	i_BokehParam
	) = 0;


	virtual bool RenderFSRUpsample
	(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* l_UpsampleTexture,
		RCMOD_Texture* l_InputTexture,
		float l_QualityValue
	) = 0;

	//virtual bool RenderFSR2(
	//	RCOSRendererData* i_RendererData,
	//	RCMOD_Texture* i_OutputTexture,
	//	RCMOD_Texture* i_ColorInputTexture,
	//	RCMOD_Texture* i_DepthInputTexture,
	//	RCMOD_Texture* i_MotionVectorsInputTexture,
	//	float i_QualityValue
	//) = 0;

	virtual bool RenderNISUpsample
	(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* l_UpsampleTexture,
		RCMOD_Texture* l_InputTexture,
		float l_Sharpness
	) = 0;

	virtual bool	RenderVolumetricCloud(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_Cirrus2DTexture,
		RCMOD_Texture* i_Weather2DTexture,
		RCMOD_Float3 const& i_LightDir,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4
	) = 0;

	virtual bool	RenderFogVolume(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthTexture
	) = 0;

	virtual bool	RenderRenderToTexture(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_String i_FilePath
	) = 0;

	virtual bool	RenderBrunetonsImprovedAtmosphere(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorOutputTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_GBufferATexture,
		RCMOD_Float3 const& i_SunDir,
		RCMOD_Float3 const& i_SkyDir,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool	RenderAtmosphereDayNightCycle(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorOutputTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_GBufferATexture,
		RCMOD_Float3 const& i_SunDir,
		RCMOD_Float3 const& i_SkyDir,
		float& i_Time,
		RCMOD_Texture* i_MilkywayTexture,
		RCMOD_Texture* i_MoonTexture,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2, 
		RCMOD_Float4x4 const& i_Param3
	) = 0;

	virtual bool RenderFogRayMarch(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorOutputTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_BlueNoiseTexture,
		RCMOD_Texture* i_SkyBlurSrcTexture,
		RCMOD_Texture* i_SkySrcTexture,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5,
		RCMOD_String const& i_BaseNoiseFile,
		RCMOD_String const& i_DetailNoiseFile
	) = 0;

	virtual bool RenderPostProcessTonemap(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_ExposureTexure,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5
	) = 0;

	virtual bool RenderPostProcessTonemapV2(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_ExposureTexure,
		RCMOD_Texture* i_CharacterExposureTexure,
		RCMOD_Texture* i_GBuffer0Texure,
		RCMOD_Texture* i_DepthTexure,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5,
		RCMOD_Float4x4 const& i_Param6
	) = 0;

	virtual bool RenderMobilePostProcessTonemap(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_ColorGradingLUTTexure,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5,
		RCMOD_Float4 const& i_Param6
	) = 0;

	virtual bool	RenderRealSkyMoon(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_DepthInputTexture,
		RCMOD_Texture* i_MoonTexture,
		RCMOD_Float4 const& i_Param,
		RCMOD_Float4 const& i_MoonColor
	) = 0;

	virtual bool RenderPostProcessEyeAdaptation(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool RenderPostProcessEyeAdaptationV2(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_GBuffer0Texture,
		RCMOD_Texture* i_ColorRtTexture0,
		RCMOD_Texture* i_ColorRtTexture1,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool RenderFillLightFactor(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_GBuffer0Texture,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool	RenderProLens(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorOutputTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Float3 const& i_LightDir,
		RCMOD_String i_FilePath,
		RCMOD_Float4 const& i_v4Param,
		RCMOD_Float4 const& i_v4GlobalTintColor
	) = 0;

	virtual bool	RenderVolumetricLighting(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Float4x4 const& i_VolumetricLightingParam,
		RCMOD_Float4x4 const& i_VolumetricLightingParam2) = 0;

	/*
	*	gbuffers
	*/
	virtual bool	RenderGBuffers(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pGrassTex,
		RCMOD_Texture* io_pDepth,
		RCMOD_Texture* o_pGBuffer0,
		RCMOD_Texture* o_pGBuffer1,
		RCMOD_Texture* o_pGBuffer2,
		RCMOD_Texture* o_pGBuffer3,
		RCMOD_Texture* o_pGBuffer4,
		RCMOD_Texture* i_pFusionNoise = nullptr,
		RCMOD_Texture* o_pAOMask = nullptr,
		BOOL i_bEnableFusionVT = FALSE,
		BOOL i_bEnableFusionModel = FALSE,
		BOOL i_bEnableMoss = FALSE,
		RCMOD_Float4x4* i_pFusionVTParam = nullptr,
		RCMOD_Float4x4* i_pFusionModelParam = nullptr,
		RCMOD_Float4x4* i_pAmbientOcclusionParam = nullptr,
		RCMOD_Float4x4* i_pFusionVTParam1 = nullptr
	) = 0;

	virtual bool RenderWaterGBuffers(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* o_pGBuffer0,
		RCMOD_Texture* o_pGBuffer1,
		RCMOD_Texture* o_pGBuffer2,
		RCMOD_Texture* o_pGBuffer3,
		RCMOD_Texture* o_pGBuffer4
	) = 0;

	/*
	*	rtx gbuffers
	*/
	virtual bool	RTXRenderGBuffers(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pInput0,
		RCMOD_Texture* i_pInput1,
		RCMOD_Texture* i_pInput2,
		RCMOD_Texture* i_pInput3,
		RCMOD_Texture* i_pInput4,
		float          i_RoughnessThreshold,
		float          i_fReflectionOpaqueTMax,
		float          i_fReflectionNonOpaqueTMax,
		RCMOD_Texture* o_pGBuffer0,
		RCMOD_Texture* o_pGBuffer1,
		RCMOD_Texture* o_pGBuffer2,
		RCMOD_Texture* o_pGBuffer3,
		RCMOD_Texture* o_pGBuffer4
	) = 0;

	/*
	*	shadow maps
	*/
	virtual bool	RenderShadowMap(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pDepth
	) = 0;

	virtual bool	RenderCloudShadowMap(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pDepth,
		RCMOD_Texture* i_CloudShadowMask,
		RCMOD_Texture* i_pGbufferA,
		RCMOD_Texture* i_pGbufferC
	) = 0;

	virtual bool	GetShadowMask(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_BackupTexture
	) = 0;

	/*
	*	render whole scene shadow mask
	*/
	virtual bool	RenderWholeSceneShadowMask(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pDepth,
		RCMOD_Texture* i_OutputTexture
	) = 0;

	virtual bool	RenderHistogram(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		RCMOD_Float4	i_HistogramParam
	) = 0;

	/*
	*	prepare for water rendering
	*/
	virtual bool	RenderWaterPreparation(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pDepth,
		RCMOD_Texture* o_pWaterMask,
		RCMOD_Texture* o_pWaterDepth,
		RCMOD_WaterData* o_pWaterUnifinedData
	) = 0;

	/*
	*	prepare for water rendering (RTX)
	*/
	virtual bool	RenderWaterPreparationRTX(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pDepth,
		RCMOD_Texture* i_pG0,
		RCMOD_Texture* i_pG2,
		RCMOD_Texture* o_pWaterMask,
		RCMOD_Texture* o_pWaterDepth,
		RCMOD_WaterData* o_pWaterUnifinedData
	) = 0;

	/*
	*	render water
	*/
	virtual bool	RenderWater(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pColor,
		RCMOD_Texture* i_pOriginalDepth,
		RCMOD_Texture* i_pWaterMask,
		RCMOD_Texture* i_pWaterDepth,
		RCMOD_Texture* i_pNormal,
		RCMOD_Texture* i_pAtmosphereTex
	) = 0;

	/*
	*	render water with SSR reflection texture
	*/
	virtual bool	RenderWaterSSR(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pColor,
		RCMOD_Texture* i_pOriginalDepth,
		RCMOD_Texture* i_pWaterMask,
		RCMOD_Texture* i_pWaterDepth,
		RCMOD_Texture* i_pNormal,
		RCMOD_Texture* i_pAtmosphereTex,
		RCMOD_Texture* i_pSSRTex
	) = 0;

	/*
	*	render water with RTX tech
	*/
	virtual bool	RenderWaterRTX(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pColor,
		RCMOD_Texture* i_pOriginalDepth,
		RCMOD_Texture* i_pWaterMask,
		RCMOD_Texture* i_pWaterDepth,
		RCMOD_Texture* i_pNormal,
		RCMOD_Texture* i_pAtmosphereTex,
		RCMOD_Texture* i_pSSRTex,
		float* i_pUnderWaterParam0,
		float* i_pUnderWaterParam1
	) = 0;

	/*
	*	gbuffers for decal
	*/
	virtual bool	RenderGBuffersForDecal(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pDepth,
		RCMOD_Texture* i_pDecalMask,
		RCMOD_Texture* io_pGBuffer0,
		RCMOD_Texture* io_pGBuffer1,
		RCMOD_Texture* io_pGBuffer2,
		RCMOD_Texture* io_pGBuffer3,
		RCMOD_Texture* io_pGBuffer4
	) = 0;

	/*
	*	gbuffers for terrain decal
	*/
	virtual bool	RenderGBuffersForTerrainDecal(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_pDepth,
		RCMOD_Texture* i_pDecalMask,
		RCMOD_Texture* io_pGBuffer0,
		RCMOD_Texture* io_pGBuffer1,
		RCMOD_Texture* io_pGBuffer2,
		RCMOD_Texture* io_pGBuffer3,
		RCMOD_Texture* io_pGBuffer4
	) = 0;

	/*
	*	render gi effect using vxgi
	*/
	virtual bool	RenderDefaultVXGI(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_CurrentFrameGBuffers,
		int i_CurrentFrameGBuffersCount,
		RCMOD_Float4x4 const& i_CurrentViewMatrix,
		RCMOD_Float4x4 const& i_CurrentProjMatrix,
		RCMOD_Texture** i_PreviousFrameGBuffers,
		int i_PreviousFrameGBuffersCount,
		RCMOD_Float4x4 const& i_PreviousViewMatrix,
		RCMOD_Float4x4 const& i_PreviousProjMatrix,
		RCMOD_Texture** i_OutputTextures,
		int i_OutputTextureCount,
		RCMOD_GIParameters const& i_GIParameters
	) = 0;

	virtual bool	RenderGIVoxelization(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* io_pColorTexture,
		RCMOD_Texture* io_pDepthTexture,
		RCMOD_Float4x4 const& i_DebugParams
	) = 0;

	virtual bool	RenderCustomVXGI(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_CurrentFrameGBuffers,
		int i_CurrentFrameGBuffersCount,
		RCMOD_Float4x4 const& i_CurrentViewMatrix,
		RCMOD_Float4x4 const& i_CurrentProjMatrix,
		RCMOD_Texture** i_PreviousFrameGBuffers,
		int i_PreviousFrameGBuffersCount,
		RCMOD_Float4x4 const& i_PreviousViewMatrix,
		RCMOD_Float4x4 const& i_PreviousProjMatrix,
		RCMOD_Texture** i_OutputTextures,
		int i_OutputTextureCount,
		RCMOD_GIParameters const& i_GIParameters
	) = 0;

	virtual bool RenderVolumetricHeightFog(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_ColorSrcTexture,
		RCMOD_Texture* i_DepthSrcTexure,
		RCMOD_Texture* i_OcclusionTexture,
		RCMOD_Float4 	const& l_ParamEyePos,
		RCMOD_Float3 	const& l_ParamLightDir,
		RCMOD_Float4x4 	const& l_ParamViewProjInv,
		RCMOD_Float4 	const& l_ParamColorFog,
		RCMOD_Float4 	const& l_ParamColorIns,
		RCMOD_Float4x4 	const& l_ParamViewFogMatrixData,
		RCMOD_Float4 	const& l_ParamFogAlbedo,
		RCMOD_Float4 	const& l_ParamFogEmissive
	) = 0;

	virtual void	ReleaseBokehResource() = 0;

	virtual void    RCP_CopyDepth(RCOSRendererData* i_RendererData, RCMOD_Texture* i_pSrc, RCMOD_Texture* o_pDest) = 0;

	virtual void    RCP_ClearDepth(RCOSRendererData* i_RendererData, RCMOD_Texture* i_pDepth, float i_Depth, float i_Stencil) = 0;

	virtual void    RCP_CopyTexture(RCOSRendererData* i_RendererData, RCMOD_Texture* i_pSrc, RCMOD_Texture* o_pDest) = 0;

	virtual void    RCP_ClearTexture(RCOSRendererData* i_RendererData, RCMOD_Texture* i_pDepth, float i_Depth, float i_Stencil, RCMOD_Float4 i_ClearColor) = 0;

	virtual bool	renderMainCamera_TXAA(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ColorTexture, RCMOD_Texture* i_DepthTexture, RCMOD_Texture* i_GBufferD, RCMOD_Texture* i_ReturnTex, RCMOD_Float4 i_ViewPort, BOOL bUpSample, RCMOD_Float4 i_Params) = 0;

	virtual bool	renderMainCamera_DLSS(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ColorTexture, RCMOD_Texture* i_DepthTexture, RCMOD_Texture* i_GBufferD, float i_ScaleFactor, RCMOD_Texture* i_OutputRT) = 0;

	virtual bool	renderMainCamera_DLSSUpScale(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ColorTexture, float i_ScaleFactor, RCMOD_Texture* i_OutputRT) = 0;

	virtual bool	renderMainCamera_DLSS2UpScale(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_MotionVectorTexture,
		RCMOD_Texture* i_ExposureTexture,
		float i_Sharpness,
		float i_QualityValue,
		RCMOD_Texture* i_OutputRT) = 0;

	virtual bool renderMainCamera_FSR2UpScale(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_MotionVectorTexture,
		RCMOD_Texture* i_ExposureTexture,
		RCMOD_Texture* i_OutputRT) = 0;

	virtual bool renderMoss(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_InputGBuffer0,
		RCMOD_Texture* i_InputGBuffer1,
		RCMOD_Texture* i_InputGBuffer2,
		RCMOD_Texture* i_InputDepth,
		RCMOD_Texture* l_AOMaskTexture,
		RCMOD_Texture* l_WeatherMask,
		RCMOD_Texture* i_OutputGBuffer1,
		RCMOD_Texture* i_OutputGBuffer2,
		RCMOD_Float4 const& i_WetParam,
		RCMOD_Float4x4 const& i_MossAParam,
		RCMOD_Float4x4 const& i_MossAParam_,
		RCMOD_Float4x4 const& i_MossBParam,
		RCMOD_Float4x4 const& i_MossCParam,
		RCMOD_String* i_PathList
	) = 0;

	virtual void renderCoverMap20(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_InputGBuffer0,
		RCMOD_Texture* i_InputGBuffer1,
		RCMOD_Texture* i_InputGBuffer2,
		RCMOD_Texture* i_InputDepth,
		RCMOD_Texture* l_NoiseTexture,
		RCMOD_Texture* l_WeatherMask,
		RCMOD_Texture* i_OutputGBuffer1,
		RCMOD_Float4x4 const& i_CoverMapParam0,
		RCMOD_Float4x4 const& i_CoverMapParam1
	) = 0;

	virtual bool renderWaterWetness(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_InputGBuffer0,
		RCMOD_Texture* i_InputGBuffer1,
        RCMOD_Texture* i_InputGBuffer2,
        RCMOD_Texture* i_InputGBuffer3,
		RCMOD_Texture* i_OutputGBuffer0,
		RCMOD_Texture* i_OutputGBuffer1,
        RCMOD_Texture* i_OutputGBuffer2,
        RCMOD_Texture* i_OutputGBuffer3
	) = 0;

	virtual bool renderScreenSpaceBevel(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_InputGBuffer0,
		RCMOD_Texture* i_InputDepthTexture,
		RCMOD_Texture* i_OutputGBuffer0,
		FLOAT i_BevelWidth,
		FLOAT i_Tolerance) = 0;

	virtual bool	RenderWetness(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_CurrentFrameGBuffers,
		int i_CurrentFrameGBuffersCount,
		RCMOD_Texture** i_OutputTextures,
		int i_OutputTextureCount) = 0;

	virtual bool    RCP_CausticPrepare(RCOSRendererData* i_RendererData, RCMOD_Texture* o_WorldPosTexture, RCMOD_Texture* o_WorldNormalTexture, float i_Width, float i_Height, float i_Radius, float* i_pLightDir, BOOL i_bFixedCam, float* i_pCamPos) = 0;
	virtual bool    RCP_CausticExecute(RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_WorldPosTexture,
		RCMOD_Texture* i_WorldNormalTexture,
		RCMOD_Texture* i_GBuffer0Texture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* o_ReflectionTexture,
		RCMOD_Texture* o_RefractionTexture,
		RCMOD_Float4x4 const& i_ViewMatrix,
		RCMOD_Float4x4 const& i_ProjMatrix,
		RCMOD_Float3 const& i_SunDir,

		float i_fCausticsBufferScaling,
		float i_RFL_fRayTMax,
		float i_IterationCount,
		float* i_CausticColor,
		float i_fDepthBias,
		float i_fIntensityScaling,
		float i_fSurfaceIntensityFac,
		float i_RFR_fRayTMax,
		float i_RFR_fTriangleArea,
		float* i_RFR_NormalScaling,
		float i_RFL_fTriangleArea,
		float* i_RFL_NormalScaling,
		float i_RFL_fCausticsIntensity,
		float i_fIntensityClamp,
		float i_RFR_fCausticsIntensity,
		float i_fUseDrawIndirect,
		float i_fLightIntensity,
		float i_fFresnelBaseReflectFraction
	) = 0;
	virtual bool    RCP_CausticCompose(RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_ReflectionTexture,
		RCMOD_Texture* i_RefractionTexture,
		RCMOD_Texture* i_Gbuffer0Texture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_Gbuffer1Texture,
		RCMOD_Texture* i_ResultTexture
	) = 0;


	virtual bool    RCP_RTShadow(RCOSRendererData* i_RendererData, RCMOD_Texture* i_ResultTexture, RCMOD_Texture* i_ResolvedSceneDepthTexture, RCMOD_Texture* i_GBuffer0Texture) = 0;

	virtual bool    RenderRainEffect(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ResultTexture,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_GBuffer0Texture,
		RCMOD_Texture* i_GBuffer2Texture,
		RCMOD_Texture* i_WeatherMask,
		RCMOD_Texture* i_RippleTex,
		RCMOD_Texture* i_FlowWaveTex,
		RCMOD_Texture* i_TerrainSlopeMask,
		RCMOD_Float4x4 const& i_Param,
		float i_DeltaTime,
		RCMOD_Texture* i_RainMask) = 0;

	virtual bool ForwardWeatherParam(RCOSRendererData* i_RendererData, RCMOD_String& i_pColorNoise, RCMOD_String& i_pMask, float i_BorderNoiseTiling, float i_BorderNoiseFactorTiling) = 0;

	virtual bool CustomizeOutput(RCOSRendererData* i_RendererData, RCMOD_Texture* l_ColorInput, RCMOD_Texture* l_ColorOutput, RCMOD_Float2 i_SrcUVPos, RCMOD_Float2 i_SrcUVSize, RCMOD_Float2 i_DstUVPos, RCMOD_Float2 i_DstUVSize, float i_BlendType) = 0;

	virtual bool RenderSSDMDisplace(RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount) = 0;

	virtual bool RenderPrePostUWater(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* l_RtOutput,
		RCMOD_Texture* l_ColorInput,
		RCMOD_Texture* l_DepthInput,
		RCMOD_Texture* l_NormalInput,
		RCMOD_Texture* l_WaterMaskInput,
		RCMOD_Texture* i_ColorGradInput,
		RCMOD_Float4* l_FogColor,
		RCMOD_Float4x4* l_Params,
		BOOL bPre
	) = 0;

	virtual bool ClusterOptions(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture*    i_ColorTexture,
		RCMOD_Texture*    i_DepthTexture,
		bool              i_bEnableClusterForwardPointLight,
		bool              i_bEnableClusterForwardDecalSphere,
		bool              i_bEnableClusterForwardSpotLight,
		bool              i_bEnableClusterForwardDirectionalLight,
		bool              i_bEnableClusterDeferredPointLight,
		bool              i_bEnableClusterDeferredDecalSphere,
		bool              i_bEnableClusterDeferredSpotLight,
		bool              i_bEnableClusterDeferredDirectionalLight,
		bool              i_bUseNewCluster,
		bool              i_bEnableClusterDebug
	) = 0;

	virtual bool ClusterForwardOptions(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		bool i_bEnableClusterForwardLutSphere,
		bool i_bEnableClusterForwardEnvProbe,
		bool i_bEnableClusterForwardDecalSphere) = 0;

	virtual bool ClusterDeferredOptions(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		bool l_bEnableClusterDeferredPointLight,
		bool l_bEnableClusterDeferredLUTSphere,
		bool l_bEnableClusterDeferredEnvProbe,
		bool l_bEnableClusterDeferredDecalSphere) = 0;

	virtual bool RenderMobileHBAO(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_GBuffer0Texture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Float4x4* i_Params
	) = 0;

	virtual bool RenderMainCamera_SceneDepthWithOIT(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_SceneDepthInput,
		RCMOD_Texture* i_OITAlphaInput,
		RCMOD_Texture* i_OITDepthTexture,
		RCMOD_Texture* i_ReturnDepthTexture
	) = 0;

	virtual bool RenderDynamicWeatherSnowMask(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* ReturnMaskTexture,
		RCMOD_Texture* i_GBuffer0Input,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_WaterDepthTexture,
		RCMOD_Texture* i_NoiseTexture,
		RCMOD_Float4x4* i_Param
	) = 0;
	
	virtual bool	RenderBlendSkybox(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorRtTexture,
		RCMOD_Texture* i_DepthInputTexture,
		RCMOD_Texture* i_SrcTexture,
		RCMOD_Float3 const& i_PosDir,
		float i_ScaleAngle,
		float i_RotAngle,
		RCMOD_Float4 const& i_CastColor,
		RCMOD_Float2 const& i_TextureDimensionParams,
		float i_FrameRate
	) = 0;

	virtual auto RenderFSR3(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_ColorTexture,
		RCMOD_Texture* i_DepthTexture,
		RCMOD_Texture* i_MotionVectorTexture,
		RCMOD_Texture* i_ExposureTexture,
		RCMOD_Texture* i_OutputRT
	)->HRESULT = 0;

    virtual void SetRCCommonParams() = 0;

	virtual bool RenderVolumeCloudRayMarch(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_SceneDepth,
		RCMOD_Texture*        i_SceneColor,
		RCMOD_Texture*        i_Perlin,
		RCMOD_Texture*        l_Worley,
		RCMOD_Texture*        l_Wispy,
		RCMOD_Texture*        l_CloudType,
		RCMOD_Texture*        l_CirrusType,
		RCMOD_Texture*        l_Blue,
		RCMOD_Float4x4 const& i_Vi,
		RCMOD_Float4x4 const& i_Pi,
		RCMOD_Texture*        o_CloudColor,
		RCMOD_Texture*        o_CloudDepth,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5,
		RCMOD_Float4x4 const& i_Param6,
		RCMOD_Float4x4 const& i_Param7,
		RCMOD_Float4x4 const& i_Param8,
		RCMOD_Float4x4 const& i_Param9
	) = 0;

	virtual bool RenderVolumeCloudShadowRayMarch(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_SceneDepth,
		RCMOD_Texture*        i_Perlin,
		RCMOD_Texture*        l_Worley,
		RCMOD_Texture*        l_Wispy,
		RCMOD_Texture*        l_CloudType,
		RCMOD_Texture*        l_CirrusType,
		RCMOD_Texture*        l_Blue,
		RCMOD_Texture*        o_CloudColor,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1,
		RCMOD_Float4x4 const& i_Param2,
		RCMOD_Float4x4 const& i_Param3,
		RCMOD_Float4x4 const& i_Param4,
		RCMOD_Float4x4 const& i_Param5,
		RCMOD_Float4x4 const& i_Param6,
		RCMOD_Float4x4 const& i_Param7,
		RCMOD_Float4x4 const& i_Param8,
		RCMOD_Float4x4 const& i_Param9
	) = 0;

	virtual bool RenderVolumetricRayMarch(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture*    i_SceneDepth,
		RCMOD_Texture*    i_SceneColor,
		RCMOD_Texture*    i_Perlin,
		RCMOD_Texture*    l_Worley,
		RCMOD_Texture*    l_Wispy,
		RCMOD_Texture*    l_CloudType,
		RCMOD_Texture*    l_Blue,
		RCMOD_Texture*    o_CloudColorTex,
		RCMOD_Texture*    o_CloudDepthTex
	) = 0;

	virtual bool RenderVolumeCloudToScene(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_SceneDepth,
		RCMOD_Texture*        i_SceneColor,
		RCMOD_Texture*        l_CloudColor,
		RCMOD_Texture*        l_CloudDepth,
		RCMOD_Texture*        l_VolumetricColor,
		RCMOD_Texture*        l_VolumetricDepth,
		RCMOD_Texture*        l_AtmosphereColor,
		RCMOD_Texture*        i_OitDepth,
		RCMOD_Texture*        i_OitColor,
		RCMOD_Texture*        o_SceneTex,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

		virtual bool RenderRainbow(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_SceneColor,
		RCMOD_Texture*        i_SceneDepth,
		RCMOD_Texture*        l_RainbowInof,
		RCMOD_Texture*        o_SceneTex,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool RenderAtmosphereVolumeLight(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_ColorOutTexture,
		RCMOD_Texture*        i_ColorInputTexture,
		RCMOD_Texture*        i_DepthTexture,
		RCMOD_Texture*        i_CloudDepthTexture,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1
	) = 0;

	virtual bool RenderCloudSea2(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        i_SceneDepth,
		RCMOD_Texture*        i_SceneColor,
		RCMOD_Texture*        i_Density,
		RCMOD_Texture*        l_Noise,
		RCMOD_Texture*        l_Blue,
		RCMOD_Texture*        l_OitDepth,
		RCMOD_Texture*        o_CloudTex,
		RCMOD_Float4x4 const& i_Param0,
		RCMOD_Float4x4 const& i_Param1
	) = 0;

	virtual bool RenderCompositeSkyCloudDayNight(
		RCOSRendererData*     i_RendererData,
		RCMOD_Texture*        l_SkyColor,
		RCMOD_Texture*        l_CloudColor,
		RCMOD_Float4x4 const& i_Vi,
		RCMOD_Float4x4 const& i_Pi,
		RCMOD_Texture*        o_Color,
		RCMOD_Float4x4 const& i_Param0
	) = 0;

	virtual bool ShaderCopyDepth(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_DstDepthTexture,
		RCMOD_Texture* i_SrcDepthTexture
	) = 0;

	virtual bool RenderWeatherRain(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* o_GBuffer0,
		RCMOD_Texture* o_GBuffer1,
		RCMOD_Texture* o_GBuffer2,
		RCMOD_Texture* i_GBuffer0,
		RCMOD_Texture* i_GBuffer1,
		RCMOD_Texture* i_GBuffer2,
		RCMOD_Texture* i_Depth,
		const RCMOD_Float4x4& i_MatVp,
		const RCMOD_Float4x4& i_MatVpI,
		RCMOD_Texture* i_RDTex,
		RCMOD_Texture* i_TDTex,
        float i_CurTime,
		const RCMOD_Float4x4& i_Param0,
		const RCMOD_Float4x4& i_Param1,
		RCMOD_Texture* i_WeatherMask,
        RCMOD_Texture* i_TATexture0,
        RCMOD_Texture* i_TATexture1,
        RCMOD_Texture* i_WaterMask
	) = 0;

    virtual bool RenderMatPostEffect(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_GBuffer0,
		RCMOD_Texture* i_GBuffer1,
		RCMOD_Texture* i_GBuffer2,
		const float& i_ParamF1,
		const RCMOD_Float4& i_ParamF4,
		RCMOD_Texture* o_Out
    ) = 0;

	virtual bool RenderTSR(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture** i_InputTexture,
		int i_InputTextureCount,
		RCMOD_Texture** i_OutputTexture,
		int i_OutputTextureCount,
		int i_RenderType
	) = 0;

    virtual bool RenderMatPostRainSnow(
		RCOSRendererData* i_RendererData,
		RCMOD_Texture* i_GBufferA,
		RCMOD_Texture* i_GBufferB,
		RCMOD_Texture* i_GBufferC,
        RCMOD_Texture* i_Depth,
        RCMOD_Texture* i_WeatherMask,
        const RCMOD_Float4& i_Tint,
        float i_BaseMapTiling,
        float i_BlendNormalIntensity,
        float i_BlendNormalTiling,
        float i_NormalMapIntensity,
        const RCMOD_Float4& i_SubsurfaceColor,
        float i_DistanceFactor,
        float i_DistancePower,
        float i_Roughness,
        const RCMOD_Float4& i_TracksColorMul,
        float i_TracksNormalIntensity,
        const RCMOD_Float2& i_UVoffsets,
        float i_RainSnowLevel,

        RCMOD_Texture* o_GBufferA,
        RCMOD_Texture* o_GBufferB,
        RCMOD_Texture* o_GBufferC
    ) = 0;
};
