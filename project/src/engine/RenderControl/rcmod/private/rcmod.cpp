#include "pch.h"
/*
    filename:       rcmod.cpp
    author:         Ming Dong
    date:           2016-MAY-13
    description:    
*/

#include "../public/rcmod.h"
#include <rc/public/rc.h>

#include "./rcnodes/rccolorinputnode.h"
#include "./rcnodes/rcgbuffer0node.h"
#include "./rcnodes/rcgbuffer1node.h"
#include "./rcnodes/rcgbuffer2node.h"
#include "./rcnodes/rcgbuffer3node.h"
#include "./rcnodes/rcgbuffer4node.h"
#include "./rcnodes/rcdepthinputnode.h"
#include "./rcnodes/rcresolveddepthinputnode.h"
#include "./rcnodes/rcgeneralinputnode.h"
#include "./rcnodes/rcoutputnode.h"
#include "./rcnodes/rcaddconstnode.h"
#include "./rcnodes/rcsubconstnode.h"
#include "./rcnodes/rcmulconstnode.h"
#include "./rcnodes/rcdivconstnode.h"
#include "./rcnodes/rcaddnode.h"
#include "./rcnodes/rcsubnode.h"
#include "./rcnodes/rcmulnode.h"
#include "./rcnodes/rcdivnode.h"
#include "./rcnodes/rcdotnode.h"
#include "./rcnodes/rcdotconstnode.h"
#include "./rcnodes/rcexpnode.h"
#include "./rcnodes/rcexp2node.h"
#include "./rcnodes/rclognode.h"
#include "./rcnodes/rclog2node.h"
#include "./rcnodes/rclog10node.h"
#include "./rcnodes/rcpownode.h"
#include "./rcnodes/rcclampnode.h"

#include "./rcnodes/rccopyfromnode.h"
#include "./rcnodes/rcpastetonode.h"
#include "./rcnodes/rccolorcurvesnode.h"
#include "./rcnodes/rcconsttexnode.h"
#include "./rcnodes/rcconsttex3dnode.h"
#include "./rcnodes/rcconsttexcubenode.h"
#include "./rcnodes/rcdownscalenode.h"
#include "./rcnodes/rcdeferedframenode.h"
#include "./rcnodes/rcmipmapgennode.h"
#include "./rcnodes/rcmipmapselnode.h"
#include "./rcnodes/rctonemappingnode.h"

#include "./rcnodes/rccolorgradenode.h"
#include "./rcnodes/rccontrastnode.h"
#include "./rcnodes/rcsaturationnode.h"
#include "./rcnodes/rcchannelbooleannode.h"
#include "./rcnodes/rccolorshiftnode.h"

#include "./rcnodes/rcsmaanode.h"

#include "./rcnodes/rctxaanode.h"
#include "./rcnodes/rcscenerendernode.h"
#include "./rcnodes/rcscenerendertonode.h"
#include "./rcnodes/rcscenerendertoforgrassnode.h"
#include "./rcnodes/rcoittransparentnode.h"
#include "./rcnodes/rcscenerenderdepthonlynode.h"
#include "./rcnodes/rcdistorationnode.h"
#include "./rcnodes/rcaccumulatornode.h"
#include "./rcnodes/rcrenderwaterpretonode.h"
#include "./rcnodes/rcbokehnode.h"
#include "./rcnodes/rceyeadaptationnode.h"
#include "./rcnodes/rcscenerendertotxaanode.h"
#include "./rcnodes/rcscenerendertotaaunode.h"

#include "./rcnodes/rceffectnodepy.h"
#include "./rcnodes/rcblendskyboxnode.h"
#include "./mdnodes/mdoperatoroutput.h"
#include "./mdnodes/mdoperatoroutput_customize.h"
#include "./mdnodes/mdoperatorcopyrect.h"
#include "./mdnodes/mdoperatorcopyrectto.h"
#include "./mdnodes/mdoperatorblendrectto.h"
#include "./mdnodes/mdoperatorlogicdepend.h"
#include "./mdnodes/mdoperatormipmapgen.h"
#include "./mdnodes/mdoperatormipmapsel.h"
#include "./mdnodes/mdoperatorcopyvalueto.h"
#include "./mdnodes/mdoperatorgpucopyto.h"
#include "./mdnodes/mdoperatormatrixinverse.h"
#include "./mdnodes/mdoperatorscenerender.h"
#include "./mdnodes/mdoperatorscenerenderto.h"
#include "./mdnodes/mdoperatorscenerendertoforgrass.h"
#include "./mdnodes/mdoperatoroittransparent.h"
#include "./mdnodes/mdoperatorscenerenderdepthonly.h"
#include "./mdnodes/mdoperatorarrayselector.h"
#include "./mdnodes/mdoperatorambientocclusion.h"
#include "./mdnodes/mdoperatorambientocclusionwithparam.h"
#include "./mdnodes/mdoperatorgpuclone.h"
#include "./mdnodes/mdoperatorrenderlights.h"
#include "./mdnodes/mdoperatorrtxrenderlights.h"
#include "./mdnodes/mdoperatorrenderlightsrtshadow.h"
#include "./mdnodes/mdoperatorrenderreflection.h"
#include "./mdnodes/mdoperatorrtxrenderreflection.h"
#include "./mdnodes/mdoperatorsss.h"
#include "./mdnodes/mdoperatorrtxsss.h"
#include "./mdnodes/mdoperatorssgi.h"
#include "./mdnodes/mdoperatorvolumegi.h"
#include "./mdnodes/mdoperatorvolumegistatic.h"
#include "./mdnodes/mdoperatorpathtracing.h"
#include "./mdnodes/mdoperatorcloudseatd.h"
#include "./mdnodes/mdoperatorrendercompositeresult.h"
#include "./mdnodes/mdoperatorssr.h"
#include "./mdnodes/mdoperatorgireflections.h"
#include "./mdnodes/mdoperatorbokeh.h"
#include "./mdnodes/mdoperatoraccumulator.h"
#include "./mdnodes/mdoperatorconvert2f1.h"
#include "./mdnodes/mdoperatorconvert2f2.h"
#include "./mdnodes/mdoperatorconvert2f3.h"
#include "./mdnodes/mdoperatorconvert2f4.h"
#include "./mdnodes/mdoperatorconvert2mat2x2.h"
#include "./mdnodes/mdoperatorconvert2mat3x3.h"
#include "./mdnodes/mdoperatorconvert2mat4x4.h"
#include "./mdnodes/mdoperatorconvert2texture.h"

#include "./mdnodes/mdoperatorfloatsin.h"
#include "./mdnodes/mdoperatorfloatminus.h"
#include "./mdnodes/mdoperatorfloatoneminus.h"
#include "./mdnodes/mdoperatorfloatfloor.h"
#include "./mdnodes/mdoperatorfloatceil.h"
#include "./mdnodes/mdoperatorfloatlerp.h"
#include "./mdnodes/mdoperatorfloatfrac.h"
#include "./mdnodes/mdoperatorfloatmod.h"

#include "./mdnodes/mdoperatoratmosphericscattering.h"
#include "./mdnodes/mdoperatorvolumetriclighting.h"
#include "./mdnodes/mdoperatorkgvolumetriccloud.h"
#include "./mdnodes/mdoperatorfogvolume.h"
#include "./mdnodes/mdoperatorbrunetonsimprovedatmosphere.h"
#include "./mdnodes/mdoperatorrendertotexture.h"
#include "./mdnodes/mdoperatorpostprocesstonemap.h"
#include "./mdnodes/mdoperatorpostprocesstonemapv2.h"
#include "./mdnodes/mdoperatormobilepostprocesstonemap.h"
#include "./mdnodes/mdoperatormoon.h"
#include "./mdnodes/mdoperatorpostprocesseyeadaptation.h"
#include "./mdnodes/mdoperatorpostprocesseyeadaptationV2.h"
#include "./mdnodes/mdoperatorfilllightfactor.h"
#include "./mdnodes/mdoperatorprolensflare.h"

#include "./mdnodes/mdoperatorrendergbuffers.h"
#include "./mdnodes/mdoperatorrtxrendergbuffers.h"
#include "./mdnodes/mdoperatorrendershadowmap.h"
#include "./mdnodes/mdoperatorrenderwaterpreto.h"

#include "./mdnodes/mdoperatorrtxforwardrender.h"
#include "./mdnodes/mdoperatorrtxreflectnonopaque.h"

#include "./mdnodes/mdoperatorrendershadowmask.h"

#include "./rcnodes/rcgrassspeedgennode.h"
#include "./mdnodes/mdoperatorgrassspeedgen.h"
#include "./mdnodes/mdoperatorrendergbuffersforgrass.h"
#include "./mdnodes/mdoperatorhistogram.h"
#include "./mdnodes/mdoperatordefaultvxgiwithouttrp.h"
#include "./mdnodes/mdoperatorcustomvxgiwithouttrp.h"
#include "./mdnodes/mdoperatorgivoxelizationdebug.h"

#include "./mdnodes/mdoperatorrenderwaterpreparation.h"
#include "./mdnodes/mdoperatorrenderwaterpreparationrtx.h"
#include "./mdnodes/mdoperatorscenerenderwater.h"
#include "./mdnodes/mdoperatorscenerenderwaterssr.h"
#include "./mdnodes/mdoperatorscenerenderwaterrtx.h"
#include "./mdnodes/mdoperatorbakewaterdata.h"
#include "./mdnodes/mdoperatorrendergbuffersfordecal.h"
#include "./mdnodes/mdoperatorrendergbuffersforterraindecal.h"
#include "./mdnodes/mdoperatoreyeadaptation.h"
#include "./mdnodes/mdoperatorcopydepth.h"
#include "./mdnodes/mdoperatorgeneralresizetex.h"
#include "./mdnodes/mdoperatordepthgen.h"
#include "./mdnodes/mdoperatorcleardepth.h"
#include "./mdnodes/mdoperatorscenerendertotxaa.h"
#include "./mdnodes/mdoperatorscenerendertotaau.h"
#include "./mdnodes/mdoperatorexp2f32.h"
#include "./mdnodes/mdoperatorconfigurewaterparam.h"

#include "./mdnodes/mdoperatordlss.h"
#include "./mdnodes/mdoperatordlssupscale.h"
#include "./mdnodes/mdoperatorVolumetricHeightFog.h"
#include "./mdnodes/mdoperatorcausticprepare.h"
#include "./mdnodes/mdoperatorcausticexecute.h"
#include "./mdnodes/mdoperatorcausticcompose.h"
#include "./mdnodes/mdoperatorrtshadow.h"

#include "./mdnodes/mdoperatorwetnessbywater.h"

#include "./mdnodes/mdoperatorrenderdynamicweathermask.h"
#include "./mdnodes/mdoperatorraineffect.h"
#include "./mdnodes/mdoperatorforwardweatherparam.h"
#include "./mdnodes/mdoperatorssdmdisplace.h"
#include "./mdnodes/mdoperatorpreuwater.h"
#include "./mdnodes/mdoperatorpostuwater.h"

#include "./mdnodes/mdoperatorfsrupsample.h"
#include "./mdnodes/mdoperatorfsr2.h"
#include "./mdnodes/mdoperatorfsr3.h"
#include "./mdnodes/mdoperatorrendercloudshadowmask.h"
#include <rc/public/mdfilegpuoperator.h>
#include "./mdnodes/mdoperatornisupsample.h"
#include "./mdnodes/mdoperatordlss2upscale.h"
#include "./mdnodes/mdoperatorrealoit.h"
#include "./mdnodes/mdoperatorfograymarch.h"
#include "./mdnodes/mdoperatorclusterforwarddebug.h"

#include "./mdnodes/mdoperatorclusterforwardoptions.h"
#include "./mdnodes/mdoperatorclusterdeferredoptions.h"
#include "./dx11/rcrenderer_dx11.h"
#include "./mdnodes/mdoperatorairender.h"
#include "./mdnodes/mdoperatorcontactshadowoptions.h"
#include "mdnodes/mdoperatorgetshadowmask.h"
#include "./mdnodes/mdoperatordebugtexturebyrange.h"
#include "./mdnodes/mdoperatorscreenspacebevel.h"
#include "./mdnodes/mdoperatormoss.h"
#include "./mdnodes/mdoperatorwaterwetness.h"
#include "./mdnodes/mdoperatorrendergbuffersforgrassandmodelfusion.h"
#include "./mdnodes/mdoperatorrendergbuffersforgrassandmossmask.h"
#include "./mdnodes/mdoperatormobilehbao.h"
#include "./mdnodes/mdoperatorscenedepthwithoit.h"
#include "./mdnodes/mdoperatorblendskybox.h"
#include "mdnodes/mdoperatorlightshaftshadowoptions.h"
#include "./mdnodes/mdoperatorclusteroptions.h"
#include "./mdnodes/mdoperatorrendergbuffersforfluxwater.h"
#include "./mdnodes/mdoperatorcovermap20.h"

#include "mdnodes/mdoperatorvolumecloudraymarch.h"
#include "mdnodes/mdoperatorvolumetricraymarch.h"
#include "./mdnodes/mdoperatoratmospherevolumelight.h"
#include "mdnodes/mdoperatorcloudsea2.h"
#include "./mdnodes/mdoperatoratmospheredaynightcycle.h"
#include "mdnodes/mdoperatorvolumecloudtoscene.h"
#include "mdnodes/mdoperatorweatherrain.h"
#include "mdnodes/mdoperatormatposteffect.h"
#include "mdnodes/mdoperatormatpostrainsnow.h"
#include "mdnodes/mdoperatorvolumecloudshadowraymarch.h"
#include "mdnodes/mdoperatorcompositeskyclouddaynight.h"
#include "mdnodes/mdoperatortsr.h"
#include "mdnodes/mdoperatorrainbow.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

__declspec(dllimport) void *KG3D_AllocEx(HANDLE hAllocator, size_t uSize, size_t uAlignment, size_t uBeginAlignOffset);
__declspec(dllimport) BOOL  KG3D_Free(HANDLE hAllocator, void *pvBuffer);


struct RCModData
{
    DOME_NS::Bool                               m_bCreateServer;
    DOME_NS::tcp::DTcpServer*                   m_pTcpServer;
    DOME_NS::tcp::ITcpSocket*                   m_pPeerSocket;
    int                                         m_ActiveEffectMgrID;

    RC_NS::MDOperatorOutput                     m_MDOutput;
    RC_NS::MDOperatorOutput_Customize           m_MDOutput_Customize;
    RC_NS::MDOperatorCopyRect                   m_MDCopyRect;
    RC_NS::MDOperatorCopyRectTo                 m_MDCopyRectTo;
    RC_NS::MDOperatorBlendRectTo                m_MDBlendRectTo;
    RC_NS::MDOperatorLogicDepend                m_MDLogicDepend;
    RC_NS::MDOperatorMipmapGen                  m_MDMipmapGen;
    RC_NS::MDOperatorMipmapSel                  m_MDMipmapSel;
    RC_NS::MDOperatorCopyValueTo                m_MDCopyValueTo;
    RC_NS::MDOperatorGpuCopyTo                  m_MDGpuCopyTo;
    RC_NS::MDOperatorMatrixInverse              m_MDMatrixInverse;
    RC_NS::MDOperatorSceneRender                m_MDSceneRender;
	RC_NS::MDOperatorSceneRenderTo              m_MDSceneRenderTo;
    RC_NS::MDOperatorSceneRenderToForGrass      m_MDSceneRenderToForGrass;
    RC_NS::MDOperatorOITTransparent             m_MDOITTransparent;
	RC_NS::MDOperatorSceneRenderDepthOnly		m_MDSceneRenderDepthOnly;
    RC_NS::MDOperatorArraySelector              m_MDArraySelector;
	RC_NS::MDOperatorAmbientOcclusion			m_MDAmbientOcclusion;
	RC_NS::MDOperatorSSS						m_MDSSS;
    RC_NS::MDOperatorRTXSSS                     m_MDRTXSSS;
	RC_NS::MDOperatorSSGI						m_MDSSGI;
    RC_NS::MDOperatorVolumeGI       			m_MDVolumeGI;
    RC_NS::MDOperatorVolumeGIStatic    			m_MDVolumeGIStatic;
    RC_NS::MDOperatorPathTracing                m_MDPathTracing;
    RC_NS::MDOperatorCloudSeaTD     			m_MDCloudSeaTD;
    RC_NS::MDOperatorAmbientOcclusionWithParam  m_MDAmbientOcclusionWithParam;
    RC_NS::MDOperatorGpuClone                   m_MDGpuClone;
	RC_NS::MDOperatorRenderLights				m_MDRenderLights;
	RC_NS::MDOperatorRTXRenderLights			m_MDRTXRenderLights;
    RC_NS::MDOperatorRenderLightsRTShadow       m_MDRenderLightsRTShadow;
	RC_NS::MDOperatorRenderReflection			m_MDRenderReflection;
    RC_NS::MDOperatorRTXRenderReflection        m_MDRTXRenderReflection;
	RC_NS::MDOperatorRenderCompositeResult		m_MDRenderCompositeResult;
	RC_NS::MDOperatorSSR						m_MDSSR;
    RC_NS::MDOperatorGIReflections				m_MDGIReflections;
	RC_NS::MDOperatorBokeh						m_MDBokeh;
	RC_NS::MDOperatorAccumulator				m_MDAccumulator;
	RC_NS::MDOperatorConvert2F1					m_MDConvert2F1;
	RC_NS::MDOperatorConvert2F2					m_MDConvert2F2;
	RC_NS::MDOperatorConvert2F3					m_MDConvert2F3;
	RC_NS::MDOperatorConvert2F4					m_MDConvert2F4;
	RC_NS::MDOperatorConvert2Mat2x2				m_MDConvert2Mat2x2;
	RC_NS::MDOperatorConvert2Mat3x3				m_MDConvert2Mat3x3;
	RC_NS::MDOperatorConvert2Mat4x4				m_MDConvert2Mat4x4;
	RC_NS::MDOperatorConvert2Texture			m_MDConvert2Texture;
	RC_NS::MDOperatorFloatSin					m_MDFloatSin;
	RC_NS::MDOperatorFloatMinus					m_MDFloatMinus;
	RC_NS::MDOperatorFloatOneMinus				m_MDFloatOneMinus;
	RC_NS::MDOperatorFloatFloor					m_MDFloatFloor;
	RC_NS::MDOperatorFloatCeil					m_MDFloatCeil;
	RC_NS::MDOperatorFloatLerp					m_MDFloatLerp;
	RC_NS::MDOperatorFloatFrac					m_MDFloatFrac;
	RC_NS::MDOperatorFloatMod					m_MDFloatMod;
	RC_NS::MDOperatorAtmosphericScattering		m_MDAtmosphericScattering;
    RC_NS::MDOperatorVolumetricLighting 		m_MDVolumetricLighting;
	RC_NS::MDOperatorKGVolumetricCloud 			m_MDKGVolumetricCloud;
	RC_NS::MDOperatorFogVolume		 			m_MDFogVolume;
	RC_NS::MDOperatorBrunetonsImprovedAtmosphere m_MDBrunetonsImprovedAtmosphere;
	RC_NS::MDOperatorRenderToTexture			m_MDRenderToTexture;
	RC_NS::MDOperatorPostProcessTonemap			m_MDPostProcessTonemap;
    RC_NS::MDOperatorPostProcessTonemapV2		m_MDPostProcessTonemapV2;
    RC_NS::MDOperatorMobilePostProcessTonemap	m_MDMobilePostProcessTonemap;
	RC_NS::MDOperatorMoon						m_MDMoon;
	RC_NS::MDOperatorProLensFlare				m_MDProLensflare;
	RC_NS::MDOperatorPostProcessEyeAdaptation	m_MDPostProcessEyeAdaptation;
    RC_NS::MDOperatorPostProcessEyeAdaptationV2	m_MDPostProcessEyeAdaptationV2;
    RC_NS::MDOperatorFillLightFactor	m_MDOperatorFillLightFactor;
    RC_NS::MDOperatorRenderGBuffers             m_MDRenderGBuffers;
	RC_NS::MDOperatorRTXRenderGBuffers          m_MDRTXRenderGBuffers;
    RC_NS::MDOperatorRenderShadowMap            m_MDRenderShadowMap;
    RC_NS::MDOperatorRenderWaterPreTo           m_MDRenderWaterPreTo;
	RC_NS::MDOperatorRenderShadowMask			m_MDRenderShadowMask;
	RC_NS::MDOperatorRTXForwardRender			m_MDRTXForwardRender;
	RC_NS::MDOperatorRTXReflectNonOpaque		m_MDRTXReflectNonOpaque;
	RC_NS::MDOperatorGrassSpeedGen              m_MDGrassSpeedGen;
	RC_NS::MDOperatorRenderGBuffersForGrass     m_MDRenderGBuffersForGrass;
	RC_NS::MDOperatorHistogram					m_MDHistogram;
	RC_NS::MDOperatorDefaultVXGIWithoutTRP		m_MDDefaultVXGIWithoutTRP;
	RC_NS::MDOperatorCustomVXGIWithoutTRP		m_MDCustomVXGIWithoutTRP;
	RC_NS::MDOperatorGIVoxelizationDebug		m_MDGIVoxelizationDebug;
	RC_NS::MDOperatorRenderWaterPreparation		m_MDRenderWaterPreparation;
    RC_NS::MDOperatorRenderWaterPreparationRTX		m_MDRenderWaterPreparationRTX;
	RC_NS::MDOperatorSceneRenderWater			m_MDSceneRenderWater;
    RC_NS::MDOperatorSceneRenderWaterSSR		m_MDSceneRenderWaterSSR;
    RC_NS::MDOperatorSceneRenderWaterRTX		m_MDSceneRenderWaterRTX;
	RC_NS::MDOperatorBakeWaterData				m_MDBakeWaterData;
	RC_NS::MDOperatorRenderGBuffersForDecal     m_MDRenderGBuffersForDecal;
	RC_NS::MDOperatorRenderGBuffersForTerrainDecal     m_MDRenderGBuffersForTerrainDecal;
	RC_NS::MDOperatorEyeAdaptation				m_MDEyeAdaptation;
    RC_NS::MDOperatorCopyDepth				    m_MDCopyDepth;
    RC_NS::MDOperatorGeneralResizeTex           m_MDGeneralResizeTex;
	RC_NS::MDOperatorDepthGen				    m_MDDepthGen;
    RC_NS::MDOperatorClearDepth				    m_MDClearDepth;
	RC_NS::MDOperatorSceneRenderToTXAA			m_MDSceneRenderToTXAA;
	RC_NS::MDOperatorSceneRenderToTAAU			m_MDSceneRenderToTAAU;
	RC_NS::MDOperatorExp2F32					m_MDExp2F32;
	RC_NS::MDOperatorConfigureWaterParam		m_MDConfigureWaterParam;
	RC_NS::MDOperatorVolumetricHeightFog		m_MDVolumetricHeightFog;
	RC_NS::MDOperatorDLSS						m_MDDLSS;
	RC_NS::MDOperatorDLSSUpScale				m_MDDLSSUpScale;
    RC_NS::MDOperatorCausticPrepare             m_MDCausticPrepare;
    RC_NS::MDOperatorCausticExecute             m_MDCausticExecute;
    RC_NS::MDOperatorCausticCompose             m_MDCausticCompose;
    RC_NS::MDOperatorRTShadow                   m_MDRTShadow;

    RC_NS::MDOperatorRenderCloudShadowMask     m_MDCloudShadowMask;

	RC_NS::MDOperatorWetnessByWater				m_MDWetnessByWater;

    RC_NS::MDOperatorRenderDynamicWeatherMask   m_MDRenderDynamicWeatherMask;

    RC_NS::MDOperatorRainEffect   m_MDRenderRainEffect;
    RC_NS::MDOperatorForwardWeatherParam   m_MDForwardWeatherParam;
    RC_NS::MDOperatorSSDMDisplace   m_MDSSDMDisplace;
    RC_NS::MDOperatorPreUWater      m_MDPreUWater;
    RC_NS::MDOperatorPostUWater      m_MDPostUWater;

    RC_NS::MDOperatorFSRUpsample				m_MDFSR;
    RC_NS::MDOperatorFSR2				        m_MDFSR2;
    RC_NS::MDOperatorFSR3				        m_MDFSR3;
    RC_NS::MDOperatorNISUpsample				m_MDNIS;

    RC_NS::MDOperatorDLSS2UpScale				m_MDDLSS2;

    RC_NS::MDOperatorRealOIT					m_MDRealOIT;

    RC_NS::MDOperatorFogRayMarch				m_MDFogRayMarch;

    RC_NS::MDOperatorClusterForwardDebug        m_MDClusterForwardDebug;

    RC_NS::MDOperatorClusterForwardOptions      m_MDClusterForwardOptions;
    RC_NS::MDOperatorClusterDeferredOptions     m_MDClusterDeferredOptions;
	RC_NS::MDOperatorAIRender					m_MDAIRenderSceneColor;
	RC_NS::MDOperatorGetShadowMask				m_MDGetShadowMask;
    RC_NS::MDOperatorContactShadowOptions       m_MDContactShadowOptions;
    RC_NS::MDOperatorLightShaftShadowOptions    m_MDLightShaftShadowOptions;
    RC_NS::MDOperatorDebugTextureByRange        m_MDDebugTextureByRange;
    RC_NS::TArray<RC_NS::MDFileGpuOperator*>    m_ScriptOperators;

    RC_NS::MDOperatorScreenSpaceBevel           m_ScreenSpaceBevel;
    RC_NS::MDOperatorMoss                       m_Moss;
    RC_NS::MDOperatorWaterWetness               m_WaterWetness;
    RC_NS::MDOperatorRenderGBuffersForGrassAndModelFusion m_RenderGBufferForGrassAndModelFusion;
    RC_NS::MDOperatorRenderGBuffersForGrassAndMossMask m_RenderGBufferForGrassAndMossMask;

    RC_NS::MDOperatorMobileHBAO m_MobileHBAO;

    RC_NS::MDOperatorSceneDepthWithOIT m_SceneDepthWithOIT;

    RC_NS::Bool                                 m_bDoCapture;
    RC_NS::Int                                  m_CaptureMgrID;
    RC_NS::DString                              m_CapturePath;

    RC_NS::MDOperatorBlendSkybox                m_MDBlendSkyboxOptions;
	RC_NS::MDOperatorClusterOptions             m_MDClusterOptions;
	RC_NS::MDOperatorRenderGBuffersForFluxWater m_MDRenderGBufferForFluxWater;
	RC_NS::MDOperatorCoverMap20                 m_MDCoverMap20;

	RC_NS::MDOperatorVolumeCloudRayMarch   m_MDVolumeCloudRayMarch;
	RC_NS::MDOperatorVolumetricRayMarch    m_MDVolumetricRayMarch;
	RC_NS::MDOperatorAtmosphereVolumeLight m_MDAtmosphereVolumeLight;
	RC_NS::MDOperatorCloudSea2             m_MDOperatorCloudSea2;
	RC_NS::MDOperatorVolumeCloudToScene    m_MDOperatorCloud2Scene;
    RC_NS::MDOperatorAtmosphereDayNightCycle m_MDAtmosphereDayNightCycle;
    RC_NS::MDOperatorWeatherRain            m_MDOperatorWeatherRain;
    RC_NS::MDOperatorMatPostEffect              m_MDOperatorMatPostEffect;
    RC_NS::MDOperatorMatPostRainSnow            m_MDOperatorMatPostRainSnow;
	RC_NS::MDOperatorVolumeCloudShadowRayMarch  m_MDOperatorVolumeCloudShadowRayMarch;
	RC_NS::MDOperatorCompositeSkyCloudDayNight  m_MDOperatorCompositeSkyCloudDayNight;
    RC_NS::MDOperatorTSR m_MDOperatorTSR;
	RC_NS::MDOperatorRainbow                    m_Rainbow;

}* g_pRCModData;

/*RCMOD_API*/ bool RCMOD_CreateServer(int i_Port)
{
    if (!g_pRCModData->m_pTcpServer)
    {
        try 
        {
            g_pRCModData->m_pTcpServer = DOME_New(DOME_NS::tcp::DTcpServer)(i_Port);
        }
        catch (...)
        {
            // TODO:
        }
    }
    return true;
}

/*RCMOD_API*/ bool RCMOD_DestroyServer()
{
    if (g_pRCModData->m_pPeerSocket)
    {
        g_pRCModData->m_pPeerSocket->destroy();
        g_pRCModData->m_pPeerSocket = DM_NULL;
    }

    if (g_pRCModData->m_pTcpServer)
    {
        DOME_Del(g_pRCModData->m_pTcpServer);
        g_pRCModData->m_pTcpServer = DM_NULL;
    }
    return true;
}

void MemTest()
{
    void* l_PtrList[10000];
    DWORD l_Tick0, l_Tick1;

    for(RC_NS::Int i = 0; i < 10000; ++i)
    {
        l_PtrList[i] = DM_NULL;
    }
    RC_NS::Math::SetRandomSeed(0);
    l_Tick0 = ::GetTickCount();
    for(RC_NS::Int i = 0; i < 1000000; ++i)
    {
        RC_NS::Int l_Index = RC_NS::Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
        {
            RC_NS::Int l_Size = RC_NS::Math::RandomInRange(1, 240);
            l_PtrList[l_Index] = DOME_Alloc(l_Size);
        }
    }
    for(RC_NS::Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }
    l_Tick0 = ::GetTickCount() - l_Tick0;


    for(RC_NS::Int i = 0; i < 10000; ++i)
    {
        l_PtrList[i] = DM_NULL;
    }
    RC_NS::Math::SetRandomSeed(0);
    l_Tick1 = ::GetTickCount();
    for(RC_NS::Int i = 0; i < 1000000; ++i)
    {
        RC_NS::Int l_Index = RC_NS::Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            KG3D_Free(NULL, l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
        {
            RC_NS::Int l_Size = RC_NS::Math::RandomInRange(1, 240);
            l_PtrList[l_Index] = KG3D_AllocEx(NULL, l_Size, 8, 0);
        }
    }
    for(RC_NS::Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            KG3D_Free(NULL, l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }
    l_Tick1 = ::GetTickCount() - l_Tick1;

    int a = 0;

}

bool RCMODInit(const char* i_pDataPath, bool i_bCreateServer, int i_Port)
{
    RC_NS::DResult l_Result = RC_NS::RCInit((const DOME_NS::Char*)i_pDataPath);

    // DDS pool init
    for (DOME_NS::Int i = 0; i < DOME_NS::RCRenderer_DX11::k_DDSPoolSize; ++i)
    {
        DOME_NS::RCRenderer_DX11::m_DDSPool[i].m_RefCount = 0;
    }


//    MemTest();

    g_pRCModData = DOME_New(RCModData);

#ifndef KG_PUBLISH
    g_pRCModData->m_bCreateServer = i_bCreateServer;
#else
    g_pRCModData->m_bCreateServer = false;
#endif

    g_pRCModData->m_ActiveEffectMgrID = -1;
    g_pRCModData->m_pTcpServer = DM_NULL;
    g_pRCModData->m_pPeerSocket = DM_NULL;

    g_pRCModData->m_bDoCapture = DM_FALSE;
    g_pRCModData->m_CaptureMgrID = -1;
    g_pRCModData->m_CapturePath = "";

    if (DM_SUCC(l_Result))
    {
        // register rc effect node
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCColorInputNode"), RC_NS::RCColorInputNode::Create, RC_NS::RCColorInputNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGBuffer0Node"), RC_NS::RCGBuffer0Node::Create, RC_NS::RCGBuffer0Node::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGBuffer1Node"), RC_NS::RCGBuffer1Node::Create, RC_NS::RCGBuffer1Node::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGBuffer2Node"), RC_NS::RCGBuffer2Node::Create, RC_NS::RCGBuffer2Node::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGBuffer3Node"), RC_NS::RCGBuffer3Node::Create, RC_NS::RCGBuffer3Node::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGBuffer4Node"), RC_NS::RCGBuffer4Node::Create, RC_NS::RCGBuffer4Node::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCDepthInputNode"), RC_NS::RCDepthInputNode::Create, RC_NS::RCDepthInputNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCResolvedDepthInputNode"), RC_NS::RCResolvedDepthInputNode::Create, RC_NS::RCResolvedDepthInputNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGeneralInputNode"), RC_NS::RCGeneralInputNode::Create, RC_NS::RCGeneralInputNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCOutputNode"), RC_NS::RCOutputNode::Create, RC_NS::RCOutputNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCAddConstNode"), RC_NS::RCAddConstNode::Create, RC_NS::RCAddConstNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCColorCurvesNode"), RC_NS::RCColorCurvesNode::Create, RC_NS::RCColorCurvesNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCConstTexNode"), RC_NS::RCConstTexNode::Create, RC_NS::RCConstTexNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCConstTex3DNode"), RC_NS::RCConstTex3DNode::Create, RC_NS::RCConstTex3DNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCConstTexCubeNode"), RC_NS::RCConstTexCubeNode::Create, RC_NS::RCConstTexCubeNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCDownScaleNode"), RC_NS::RCDownScaleNode::Create, RC_NS::RCDownScaleNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCDeferedFrameNode"), RC_NS::RCDeferedFrameNode::Create, RC_NS::RCDeferedFrameNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCMipmapGenNode"), RC_NS::RCMipmapGenNode::Create, RC_NS::RCMipmapGenNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCMipmapSelNode"), RC_NS::RCMipmapSelNode::Create, RC_NS::RCMipmapSelNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCToneMappingNode"), RC_NS::RCToneMappingNode::Create, RC_NS::RCToneMappingNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCCopyFromNode"), RC_NS::RCCopyFromNode::Create, RC_NS::RCCopyFromNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCPasteToNode"), RC_NS::RCPasteToNode::Create, RC_NS::RCPasteToNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCMulConstNode"), RC_NS::RCMulConstNode::Create, RC_NS::RCMulConstNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCAddNode"), RC_NS::RCAddNode::Create, RC_NS::RCAddNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCSubNode"), RC_NS::RCSubNode::Create, RC_NS::RCSubNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCMulNode"), RC_NS::RCMulNode::Create, RC_NS::RCMulNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCDivNode"), RC_NS::RCDivNode::Create, RC_NS::RCDivNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCTXAANode"), RC_NS::RCTXAANode::Create, RC_NS::RCTXAANode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCSceneRenderNode"), RC_NS::RCSceneRenderNode::Create, RC_NS::RCSceneRenderNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCDistortionNode"), RC_NS::RCDistorationNode::Create, RC_NS::RCDistorationNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCAccumulatorNode"), RC_NS::RCAccumulatorNode::Create, RC_NS::RCAccumulatorNode::Destroy);
        RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCRenderWaterPreToNode"), RC_NS::RCRenderWaterPreToNode::Create, RC_NS::RCRenderWaterPreToNode::Destroy);
#ifdef DOME_USE_PYTHONSCRIPTNODE
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCEffectNodePy"), RC_NS::RCEffectNodePy::Create, RC_NS::RCEffectNodePy::Destroy);
#endif
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCGrassSpeedGenNode"), RC_NS::RCGrassSpeedGenNode::Create, RC_NS::RCGrassSpeedGenNode::Destroy);
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString("RCEyeAdaptationNode"), RC_NS::RCEyeAdaptationNode::Create, RC_NS::RCEyeAdaptationNode::Destroy);
#define RegisterEffectNode( NodeName ) \
		RC_NS::RCManager::Instance().registerRCEffectNode(RC_NS::DString( #NodeName ), RC_NS:: ## NodeName::Create, RC_NS::## NodeName::Destroy);

		RegisterEffectNode(RCSubConstNode)
		RegisterEffectNode(RCDivConstNode)
		RegisterEffectNode(RCDotNode)
		RegisterEffectNode(RCDotConstNode)
		RegisterEffectNode(RCExpNode)
		RegisterEffectNode(RCExp2Node)
		RegisterEffectNode(RCLogNode)
		RegisterEffectNode(RCLog2Node)
		RegisterEffectNode(RCLog10Node)
		RegisterEffectNode(RCPowNode)
		RegisterEffectNode(RCClampNode)
		RegisterEffectNode(RCColorGradeNode)
		RegisterEffectNode(RCContrastNode)
		RegisterEffectNode(RCSaturationNode)
		RegisterEffectNode(RCChannelBooleanNode)
		RegisterEffectNode(RCSceneRenderDepthOnlyNode)
		RegisterEffectNode(RCSceneRenderToNode);
        RegisterEffectNode(RCSceneRenderToForGrassNode);
        RegisterEffectNode(RCOITTransparentNode);
		RegisterEffectNode(RCSMAANode);
		RegisterEffectNode(RCBohekNode);
        RegisterEffectNode(RCColorShiftNode);
		RegisterEffectNode(RCSceneRenderToTXAANode);
		RegisterEffectNode(RCSceneRenderToTAAUNode);
        RegisterEffectNode(RCBlendSkyboxNode);
        
        // register md effect node
        RC_NS::RCManager::Instance().registerMDOperator("MDOutput", &g_pRCModData->m_MDOutput);
        RC_NS::RCManager::Instance().registerMDOperator("MDOutput_Customize", &g_pRCModData->m_MDOutput_Customize);
        RC_NS::RCManager::Instance().registerMDOperator("MDCopyRect", &g_pRCModData->m_MDCopyRect);
        RC_NS::RCManager::Instance().registerMDOperator("MDCopyRectTo", &g_pRCModData->m_MDCopyRectTo);
        RC_NS::RCManager::Instance().registerMDOperator("MDBlendRectTo", &g_pRCModData->m_MDBlendRectTo);
        RC_NS::RCManager::Instance().registerMDOperator("MDLogicDepend", &g_pRCModData->m_MDLogicDepend);
        RC_NS::RCManager::Instance().registerMDOperator("MDMipmapGen", &g_pRCModData->m_MDMipmapGen);
        RC_NS::RCManager::Instance().registerMDOperator("MDMipmapSel", &g_pRCModData->m_MDMipmapSel);
        RC_NS::RCManager::Instance().registerMDOperator("MDCopyValueTo", &g_pRCModData->m_MDCopyValueTo);
        RC_NS::RCManager::Instance().registerMDOperator("MDGpuCopyTo", &g_pRCModData->m_MDGpuCopyTo);
        RC_NS::RCManager::Instance().registerMDOperator("MDMatrixInverse", &g_pRCModData->m_MDMatrixInverse);
        RC_NS::RCManager::Instance().registerMDOperator("MDSceneRender", &g_pRCModData->m_MDSceneRender);
		RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderTo", &g_pRCModData->m_MDSceneRenderTo);
        RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderToForGrass", &g_pRCModData->m_MDSceneRenderToForGrass);
        RC_NS::RCManager::Instance().registerMDOperator("MDOITTransparent", &g_pRCModData->m_MDOITTransparent);
		RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderDepthOnly", &g_pRCModData->m_MDSceneRenderDepthOnly);
        RC_NS::RCManager::Instance().registerMDOperator("MDArraySelector", &g_pRCModData->m_MDArraySelector);
		RC_NS::RCManager::Instance().registerMDOperator("MDAmbientOcclusion", &g_pRCModData->m_MDAmbientOcclusion);
		RC_NS::RCManager::Instance().registerMDOperator("MDSSS", &g_pRCModData->m_MDSSS);
        RC_NS::RCManager::Instance().registerMDOperator("MDRTXSSS", &g_pRCModData->m_MDRTXSSS);
		RC_NS::RCManager::Instance().registerMDOperator("MDSSGI", &g_pRCModData->m_MDSSGI);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumeGI", &g_pRCModData->m_MDVolumeGI);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumeGIStatic", &g_pRCModData->m_MDVolumeGIStatic);
        RC_NS::RCManager::Instance().registerMDOperator("MDPathTracing", &g_pRCModData->m_MDPathTracing);
        RC_NS::RCManager::Instance().registerMDOperator("MDCloudSeaTD", &g_pRCModData->m_MDCloudSeaTD);
        RC_NS::RCManager::Instance().registerMDOperator("MDAmbientOcclusionWithParam", &g_pRCModData->m_MDAmbientOcclusionWithParam);
        RC_NS::RCManager::Instance().registerMDOperator("MDGpuClone", &g_pRCModData->m_MDGpuClone);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderLights", &g_pRCModData->m_MDRenderLights);
		RC_NS::RCManager::Instance().registerMDOperator("MDRTXRenderLights", &g_pRCModData->m_MDRTXRenderLights);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderLightsRTShadow", &g_pRCModData->m_MDRenderLightsRTShadow);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderReflection", &g_pRCModData->m_MDRenderReflection);
        RC_NS::RCManager::Instance().registerMDOperator("MDRTXRenderReflection", &g_pRCModData->m_MDRTXRenderReflection);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderCompositeResult", &g_pRCModData->m_MDRenderCompositeResult);
		RC_NS::RCManager::Instance().registerMDOperator("MDSSR", &g_pRCModData->m_MDSSR);
        RC_NS::RCManager::Instance().registerMDOperator("MDGIReflections", &g_pRCModData->m_MDGIReflections);
		RC_NS::RCManager::Instance().registerMDOperator("MDBokeh", &g_pRCModData->m_MDBokeh);
		RC_NS::RCManager::Instance().registerMDOperator("MDAccumulator", &g_pRCModData->m_MDAccumulator);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2F1", &g_pRCModData->m_MDConvert2F1);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2F2", &g_pRCModData->m_MDConvert2F2);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2F3", &g_pRCModData->m_MDConvert2F3);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2F4", &g_pRCModData->m_MDConvert2F4);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2Mat2x2", &g_pRCModData->m_MDConvert2Mat2x2);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2Mat3x3", &g_pRCModData->m_MDConvert2Mat3x3);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2Mat4x4", &g_pRCModData->m_MDConvert2Mat4x4);
		RC_NS::RCManager::Instance().registerMDOperator("MDConvert2Texture", &g_pRCModData->m_MDConvert2Texture);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatSin", &g_pRCModData->m_MDFloatSin);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatMinus", &g_pRCModData->m_MDFloatMinus);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatOneMinus", &g_pRCModData->m_MDFloatOneMinus);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatFloor", &g_pRCModData->m_MDFloatFloor);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatCeil", &g_pRCModData->m_MDFloatCeil);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatLerp", &g_pRCModData->m_MDFloatLerp);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatFrac", &g_pRCModData->m_MDFloatFrac);
		RC_NS::RCManager::Instance().registerMDOperator("MDFloatMod", &g_pRCModData->m_MDFloatMod);
		RC_NS::RCManager::Instance().registerMDOperator("MDAtmosphericScattering", &g_pRCModData->m_MDAtmosphericScattering);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumetricLighting", &g_pRCModData->m_MDVolumetricLighting);
		RC_NS::RCManager::Instance().registerMDOperator("MDKGVolumetricCloud", &g_pRCModData->m_MDKGVolumetricCloud);
		RC_NS::RCManager::Instance().registerMDOperator("MDFogVolume", &g_pRCModData->m_MDFogVolume);
		RC_NS::RCManager::Instance().registerMDOperator("MDBrunetonsImprovedAtmosphere", &g_pRCModData->m_MDBrunetonsImprovedAtmosphere);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderToTexture", &g_pRCModData->m_MDRenderToTexture);
		RC_NS::RCManager::Instance().registerMDOperator("MDPostProcessTonemap", &g_pRCModData->m_MDPostProcessTonemap);
        RC_NS::RCManager::Instance().registerMDOperator("MDPostProcessTonemapV2", &g_pRCModData->m_MDPostProcessTonemapV2);
        RC_NS::RCManager::Instance().registerMDOperator("MDMobilePostProcessTonemap", &g_pRCModData->m_MDMobilePostProcessTonemap);
		RC_NS::RCManager::Instance().registerMDOperator("MDMoon", &g_pRCModData->m_MDMoon);
		RC_NS::RCManager::Instance().registerMDOperator("MDPostProcessEyeAdaptation", &g_pRCModData->m_MDPostProcessEyeAdaptation);
        RC_NS::RCManager::Instance().registerMDOperator("MDPostProcessEyeAdaptationV2", &g_pRCModData->m_MDPostProcessEyeAdaptationV2);
        RC_NS::RCManager::Instance().registerMDOperator("MDFillLightFactor", &g_pRCModData->m_MDOperatorFillLightFactor);
		RC_NS::RCManager::Instance().registerMDOperator("MDProLensFlare", &g_pRCModData->m_MDProLensflare);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffers", &g_pRCModData->m_MDRenderGBuffers);
		RC_NS::RCManager::Instance().registerMDOperator("MDRTXRenderGBuffers", &g_pRCModData->m_MDRTXRenderGBuffers);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderShadowMap", &g_pRCModData->m_MDRenderShadowMap);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderWaterPreTo", &g_pRCModData->m_MDRenderWaterPreTo);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderShadowMask", &g_pRCModData->m_MDRenderShadowMask);
		RC_NS::RCManager::Instance().registerMDOperator("MDRTXForwardRenderNonOpaque", &g_pRCModData->m_MDRTXForwardRender);
		RC_NS::RCManager::Instance().registerMDOperator("MDRTXReflectNonOpaque", &g_pRCModData->m_MDRTXReflectNonOpaque);
		RC_NS::RCManager::Instance().registerMDOperator("MDGrassSpeedGen", &g_pRCModData->m_MDGrassSpeedGen);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForGrass", &g_pRCModData->m_MDRenderGBuffersForGrass);
		RC_NS::RCManager::Instance().registerMDOperator("MDHistogram", &g_pRCModData->m_MDHistogram);
		RC_NS::RCManager::Instance().registerMDOperator("MDDefaultVXGIWithoutTRP", &g_pRCModData->m_MDDefaultVXGIWithoutTRP);
		RC_NS::RCManager::Instance().registerMDOperator("MDCustomVXGIWithoutTRP", &g_pRCModData->m_MDCustomVXGIWithoutTRP);
		RC_NS::RCManager::Instance().registerMDOperator("MDGIVoxelizationDebug", &g_pRCModData->m_MDGIVoxelizationDebug);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderWaterPreparation", &g_pRCModData->m_MDRenderWaterPreparation);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderWaterPreparationRTX", &g_pRCModData->m_MDRenderWaterPreparationRTX);
		RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderWater", &g_pRCModData->m_MDSceneRenderWater);
        RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderWaterSSR", &g_pRCModData->m_MDSceneRenderWaterSSR);
        RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderWaterRTX", &g_pRCModData->m_MDSceneRenderWaterRTX);
		RC_NS::RCManager::Instance().registerMDOperator("MDBakeWaterData", &g_pRCModData->m_MDBakeWaterData);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForDecal", &g_pRCModData->m_MDRenderGBuffersForDecal);
		RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForTerrainDecal", &g_pRCModData->m_MDRenderGBuffersForTerrainDecal);
		RC_NS::RCManager::Instance().registerMDOperator("MDEyeAdaptation", &g_pRCModData->m_MDEyeAdaptation);
        RC_NS::RCManager::Instance().registerMDOperator("MDCopyDepth", &g_pRCModData->m_MDCopyDepth);
        RC_NS::RCManager::Instance().registerMDOperator("MDGeneralResizeTex", &g_pRCModData->m_MDGeneralResizeTex);
		RC_NS::RCManager::Instance().registerMDOperator("MDDepthGen", &g_pRCModData->m_MDDepthGen);
        RC_NS::RCManager::Instance().registerMDOperator("MDClearDepth", &g_pRCModData->m_MDClearDepth);
		RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderToTXAA", &g_pRCModData->m_MDSceneRenderToTXAA);
		RC_NS::RCManager::Instance().registerMDOperator("MDSceneRenderToTAAU", &g_pRCModData->m_MDSceneRenderToTAAU);
		RC_NS::RCManager::Instance().registerMDOperator("MDExp2F32", &g_pRCModData->m_MDExp2F32);
		RC_NS::RCManager::Instance().registerMDOperator("MDConfigureWaterParam", &g_pRCModData->m_MDConfigureWaterParam);
		RC_NS::RCManager::Instance().registerMDOperator("MDVolumetricHeightFog", &g_pRCModData->m_MDVolumetricHeightFog);

		RC_NS::RCManager::Instance().registerMDOperator("MDDLSS", &g_pRCModData->m_MDDLSS);
		RC_NS::RCManager::Instance().registerMDOperator("MDDLSSUPSCALE", &g_pRCModData->m_MDDLSSUpScale);
        RC_NS::RCManager::Instance().registerMDOperator("MDCausticPrepare", &g_pRCModData->m_MDCausticPrepare);
        RC_NS::RCManager::Instance().registerMDOperator("MDCausticExecute", &g_pRCModData->m_MDCausticExecute);
        RC_NS::RCManager::Instance().registerMDOperator("MDCausticCompose", &g_pRCModData->m_MDCausticCompose);
        RC_NS::RCManager::Instance().registerMDOperator("MDRTShadow", &g_pRCModData->m_MDRTShadow);

		RC_NS::RCManager::Instance().registerMDOperator("MDWetnessByWater", &g_pRCModData->m_MDWetnessByWater);

        RC_NS::RCManager::Instance().registerMDOperator("MDRenderDynamicWeatherMask", &g_pRCModData->m_MDRenderDynamicWeatherMask);
        RC_NS::RCManager::Instance().registerMDOperator("MDRainEffect", &g_pRCModData->m_MDRenderRainEffect);
        RC_NS::RCManager::Instance().registerMDOperator("MDForwardWeatherParam", &g_pRCModData->m_MDForwardWeatherParam);
        RC_NS::RCManager::Instance().registerMDOperator("MDSSDMDisplace", &g_pRCModData->m_MDSSDMDisplace);
        RC_NS::RCManager::Instance().registerMDOperator("MDPreUWater", &g_pRCModData->m_MDPreUWater);
        RC_NS::RCManager::Instance().registerMDOperator("MDPostUWater", &g_pRCModData->m_MDPostUWater);
        
        RC_NS::RCManager::Instance().registerMDOperator("MDFSR", &g_pRCModData->m_MDFSR);
        RC_NS::RCManager::Instance().registerMDOperator("MDFSR2", &g_pRCModData->m_MDFSR2);
        RC_NS::RCManager::Instance().registerMDOperator("MDFSR3", &g_pRCModData->m_MDFSR3);
        
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderCloudShadowMask", &g_pRCModData->m_MDCloudShadowMask);
        RC_NS::RCManager::Instance().registerMDOperator("MDNIS", &g_pRCModData->m_MDNIS);
        RC_NS::RCManager::Instance().registerMDOperator("MDDLSS2", &g_pRCModData->m_MDDLSS2);
        RC_NS::RCManager::Instance().registerMDOperator("MDRealOIT", &g_pRCModData->m_MDRealOIT);
        RC_NS::RCManager::Instance().registerMDOperator("MDFogRayMarch", &g_pRCModData->m_MDFogRayMarch);
        RC_NS::RCManager::Instance().registerMDOperator("MDClusterForwardDebug", &g_pRCModData->m_MDClusterForwardDebug);

        RC_NS::RCManager::Instance().registerMDOperator("MDClusterForwardOptions", &g_pRCModData->m_MDClusterForwardOptions);
        RC_NS::RCManager::Instance().registerMDOperator("MDClusterDeferredOptions", &g_pRCModData->m_MDClusterDeferredOptions);
		RC_NS::RCManager::Instance().registerMDOperator("MDAIRenderSceneColor", &g_pRCModData->m_MDAIRenderSceneColor);
		RC_NS::RCManager::Instance().registerMDOperator("MDGetShadowMask", &g_pRCModData->m_MDGetShadowMask);
        RC_NS::RCManager::Instance().registerMDOperator("MDContactShadowOptions", &g_pRCModData->m_MDContactShadowOptions);
        RC_NS::RCManager::Instance().registerMDOperator("MDLightShaftShadowOptions", &g_pRCModData->m_MDLightShaftShadowOptions);
        RC_NS::RCManager::Instance().registerMDOperator("MDDebugTextureByRange", &g_pRCModData->m_MDDebugTextureByRange);
        RC_NS::RCManager::Instance().registerMDOperator("MDScreenSpaceBevel", &g_pRCModData->m_ScreenSpaceBevel);
        RC_NS::RCManager::Instance().registerMDOperator("MDMoss", &g_pRCModData->m_Moss);
        RC_NS::RCManager::Instance().registerMDOperator("MDWaterWetness", &g_pRCModData->m_WaterWetness);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForGrassAndModelFusion", &g_pRCModData->m_RenderGBufferForGrassAndModelFusion);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForGrassAndMossMask", &g_pRCModData->m_RenderGBufferForGrassAndMossMask);

        RC_NS::RCManager::Instance().registerMDOperator("MDMobileHBAO", &g_pRCModData->m_MobileHBAO);
        RC_NS::RCManager::Instance().registerMDOperator("MDSceneDepthWithOIT", &g_pRCModData->m_SceneDepthWithOIT);
        RC_NS::RCManager::Instance().registerMDOperator("MDBlendSkyboxOptions", &g_pRCModData->m_MDBlendSkyboxOptions);
        RC_NS::RCManager::Instance().registerMDOperator("MDClusterOptions", &g_pRCModData->m_MDClusterOptions);
        RC_NS::RCManager::Instance().registerMDOperator("MDRenderGBuffersForFluxWater", &g_pRCModData->m_MDRenderGBufferForFluxWater);
        RC_NS::RCManager::Instance().registerMDOperator("MDCoverMap20", &g_pRCModData->m_MDCoverMap20);

        RC_NS::RCManager::Instance().registerMDOperator("MDVolumeCloudRayMarch", &g_pRCModData->m_MDVolumeCloudRayMarch);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumetricRayMarch", &g_pRCModData->m_MDVolumetricRayMarch);
        RC_NS::RCManager::Instance().registerMDOperator("MDAtmosphereVolumeLight", &g_pRCModData->m_MDAtmosphereVolumeLight);
        RC_NS::RCManager::Instance().registerMDOperator("MDCloudSea2", &g_pRCModData->m_MDOperatorCloudSea2);
        RC_NS::RCManager::Instance().registerMDOperator("MDAtmosphereDayNightCycle", &g_pRCModData->m_MDAtmosphereDayNightCycle);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumeCloudToScene", &g_pRCModData->m_MDOperatorCloud2Scene);
        RC_NS::RCManager::Instance().registerMDOperator("MDWeatherRain", &g_pRCModData->m_MDOperatorWeatherRain);
        RC_NS::RCManager::Instance().registerMDOperator("MDMatPostEffect", &g_pRCModData->m_MDOperatorMatPostEffect);
        RC_NS::RCManager::Instance().registerMDOperator("MDMatPostRainSnow", &g_pRCModData->m_MDOperatorMatPostRainSnow);
        RC_NS::RCManager::Instance().registerMDOperator("MDVolumeCloudShadowRayMarch", &g_pRCModData->m_MDOperatorVolumeCloudShadowRayMarch);
        RC_NS::RCManager::Instance().registerMDOperator("MDCompositeSkyCloudDayNight", &g_pRCModData->m_MDOperatorCompositeSkyCloudDayNight);
        RC_NS::RCManager::Instance().registerMDOperator("MDTSR", &g_pRCModData->m_MDOperatorTSR);
        RC_NS::RCManager::Instance().registerMDOperator("MDRainbow", &g_pRCModData->m_Rainbow);
        
        // load all script operators
        DOME_NS::DString l_ScriptFolderPath = DOME_NS::RCGlobal::Instance().m_RCDataRootPath + "mdos/";
        DOME_NS::DFolder l_ScriptFolder(l_ScriptFolderPath, DOME_NS::DOME_GetExternalFS());
        l_Result = l_ScriptFolder.refresh();
        DOME_ASSERT(DM_SUCC(l_Result));

        RC_NS::Int l_NumFile = l_ScriptFolder.getFileCount();
        for (RC_NS::Int i = 0; i < l_NumFile; ++i)
        {
            RC_NS::DString l_FileName = l_ScriptFolder.getFile(i);
            if (l_FileName.find(".mdo", l_FileName.size() - 4) >= 0)
            {
                RC_NS::DString l_Path = l_ScriptFolder.getFolderName() + l_FileName;
                RC_NS::MDFileGpuOperator* l_pScriptOperator = DOME_New(RC_NS::MDFileGpuOperator)(l_Path);
                g_pRCModData->m_ScriptOperators.push_back(l_pScriptOperator);
                RC_NS::RCManager::Instance().registerMDOperator(l_pScriptOperator->getOperatorName(), l_pScriptOperator);
            }
        }

        if (g_pRCModData->m_bCreateServer)
        {
            RCMOD_CreateServer(i_Port);
        }

#ifdef DOME_USE_PYTHONSCRIPTNODE
		RC_NS::RCEffectNodePy::PythonInit();
#endif

        return DM_TRUE;
    }
    else
        return DM_FALSE;
}

bool RCMODUninit()
{
    // DDS pool init
    for (DOME_NS::Int i = 0; i < DOME_NS::RCRenderer_DX11::k_DDSPoolSize; ++i)
    {
        if (DOME_NS::RCRenderer_DX11::m_DDSPool[i].m_RefCount > 0)
        {
            DOME_ASSERT2(0, "Texture not unloaded.");
        }
    }


#ifdef DOME_USE_PYTHONSCRIPTNODE
	RC_NS::RCEffectNodePy::PythonDeinit();
#endif

    if (g_pRCModData->m_bCreateServer)
    {
        RCMOD_DestroyServer();
    }

    for (RC_NS::Int i = 0; i < g_pRCModData->m_ScriptOperators.size(); ++i)
    {
        DOME_Del(g_pRCModData->m_ScriptOperators[i]);
    }

    DOME_Del(g_pRCModData);

    RC_NS::DResult l_Result = RC_NS::RCUninit();
    if(DM_SUCC(l_Result))
        return DM_TRUE;
    else
        return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetDataPath(RCMOD_String& o_DataPath)
{
    o_DataPath = RC_NS::RCGlobal::Instance().m_RCDataRootPath.c_str();
    return DM_TRUE;
}

RCMOD_API bool  RCMOD_RegisterPlugin(const RCMOD_String& i_PluginName, RCPluginInterface* i_pPlugin)
{
    return DM_SUCC(RC_NS::RCManager::Instance().registerPlugin(i_PluginName.c_str(), i_pPlugin)) ? true : false;
}

RCMOD_API bool RCMOD_ReloadScriptOperator()
{
    // reload all script operator
    for (RC_NS::Int i = 0; i < g_pRCModData->m_ScriptOperators.size(); ++i)
    {
        DOME_ASSERT(g_pRCModData->m_ScriptOperators[i]);
        g_pRCModData->m_ScriptOperators[i]->reload();
    }

    // delete all exsiting MDExecuter from all effect managers
    for (RC_NS::Int i = 0; i < RC_MAXEFFECTMANAGER; ++i)
    {
        RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i);
        if(l_pEffectMgr)
            l_pEffectMgr->clearAllMDExecuter();
    }

    return DM_TRUE;
}

RCMOD_API bool RCMOD_Update()
{
    if (g_pRCModData->m_pTcpServer)
    {
        DOME_NS::tcp::ITcpSocket* l_pPeer = g_pRCModData->m_pTcpServer->getSocket();
        if (l_pPeer)
        {
            if (g_pRCModData->m_pPeerSocket)
            {
                l_pPeer->destroy();
                l_pPeer = DM_NULL;
            }
            else
            {
                g_pRCModData->m_pPeerSocket = l_pPeer;
            }
        }
    }

    if (g_pRCModData->m_pPeerSocket)
    {
        DOME_NS::TArray<DOME_NS::DSimpleMessage*> l_MessageOutPool;
        DOME_NS::DSimpleMessage* l_pMessage;
        while (l_pMessage = g_pRCModData->m_pPeerSocket->receiveMessage())
        {
            if (l_pMessage->getMessageName() == "CMD_UpdateEffect")
            {
                DOME_NS::DString l_EffectName = l_pMessage->getParameter(DOME_NS::DHashString("EffectName"))->getDString();
                DOME_NS::DString l_EffectContent = l_pMessage->getParameter(DOME_NS::DHashString("EffectContent"))->getDString();
                DOME_NS::DString l_EditorData = l_pMessage->getParameter(DOME_NS::DHashString("EditorData"))->getDString();

                if (g_pRCModData->m_ActiveEffectMgrID >= 0)
                {
                    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(g_pRCModData->m_ActiveEffectMgrID);
                    RCMOD_UpdateEffect(g_pRCModData->m_ActiveEffectMgrID, l_pEffectMgr->getCurrentEffectName().c_str(), l_EffectContent.c_str(), l_EditorData.c_str());
                }
            }
            else if (l_pMessage->getMessageName() == "CMD_LoadEffectFromServer")
            {
                if (g_pRCModData->m_ActiveEffectMgrID >= 0)
                {
                    RCMOD_String l_EditorData;
                    RCMOD_GetCurrentEffectEditorData(g_pRCModData->m_ActiveEffectMgrID, l_EditorData);
                    DOME_NS::DSimpleMessage* l_pMessageRet = DOME_New(DOME_NS::DSimpleMessage)(DOME_NS::DString("CMD_LoadEffectFromServer_ACK"));
                    l_pMessageRet->addParameter("EditorData", DOME_NS::DSimpleTypedValue(DOME_NS::DString(l_EditorData.c_str())));

                    l_MessageOutPool.push_back(l_pMessageRet);
                }
            }
            else if (l_pMessage->getMessageName() == "CMD_ReloadScriptShader")
            {
                RCMOD_ReloadScriptOperator();
            }
            else if (l_pMessage->getMessageName() == "CMD_SaveCurrentEffect")
            {
                RCMOD_SaveCurrentEffect(g_pRCModData->m_ActiveEffectMgrID);
            }
            else if (l_pMessage->getMessageName() == "CMD_DoCapture")
            {
                DOME_NS::DString l_Path = l_pMessage->getParameter(DOME_NS::DHashString("Path"))->getDString();
                g_pRCModData->m_bDoCapture = DM_TRUE;
                g_pRCModData->m_CaptureMgrID = g_pRCModData->m_ActiveEffectMgrID;
                g_pRCModData->m_CapturePath = l_Path;
            }

            DOME_Del(l_pMessage);
        }

        for (DOME_NS::Int i = 0; i < l_MessageOutPool.size(); ++i)
        {
            g_pRCModData->m_pPeerSocket->sendMessage(l_MessageOutPool[i]);
        }

        if (g_pRCModData->m_pPeerSocket->isClosed())
        {
            g_pRCModData->m_pPeerSocket->destroy();
            g_pRCModData->m_pPeerSocket = DM_NULL;
        }

    }

    return true;
}

RCMOD_API bool  RCMOD_GetShaderFromSignature(const char* i_pSignature, RCMOD_String& o_Shader)
{
    DOME_NS::DString l_ShaderCode;
    RC_NS::DResult l_Result = RC_NS::RCManager::Instance().getGpuShaderFromSignature(i_pSignature, l_ShaderCode);
    if (DM_SUCC(l_Result))
    {
        o_Shader = l_ShaderCode.c_str();
        return true;
    }
    else
        return false;
}

RCMOD_API bool  RCMOD_RegisterEffectPlugin(int i_EffectMgrID, const RCMOD_String& i_PluginName, RCPluginInterface* i_pPlugin)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->registerPlugin(i_PluginName.c_str(), i_pPlugin);
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetActiveEffectManager(int i_EffectMgrID)
{
    g_pRCModData->m_ActiveEffectMgrID = i_EffectMgrID;
    return DM_TRUE;
}

RCMOD_API int  RCMOD_GetActiveEffectManager()
{
    return g_pRCModData->m_ActiveEffectMgrID;
}

RCMOD_API bool  RCMOD_ClearAllEffects(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->clearAllEffects();
        return (DM_SUCC(l_Result) || l_Result == DOME_NS::R_ALREADYREGISTERED) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API void  RCMOD_ResetAllEffects(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        l_pEffectMgr->resetAllEffects();
    }
}

RCMOD_API bool  RCMOD_CreateEffect(int i_EffectMgrID, const char* i_pEffectName, const char* i_pEffectContent, const char* i_pEditorData)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->createEffect(RC_NS::DHashString(i_pEffectName), RC_NS::DString(i_pEffectContent), RC_NS::DString(i_pEditorData));
        return (DM_SUCC(l_Result) || l_Result == DOME_NS::R_ALREADYREGISTERED) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_LoadEffect(int i_EffectMgrID, const char* i_pEffectName)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->loadEffect(RC_NS::DHashString(i_pEffectName));
        return (DM_SUCC(l_Result) || l_Result == DOME_NS::R_ALREADYREGISTERED) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SaveCurrentEffect(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->saveCurrentEffect();
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_UpdateEffect(int i_EffectMgrID, const char* i_pEffectName, const char* i_pEffectContent, const char* i_pEditorData)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->updateEffect(RC_NS::DHashString(i_pEffectName), i_pEffectContent, i_pEditorData);
        if (l_Result == DOME_NS::R_NOTFOUND)
        {
            l_Result = l_pEffectMgr->createEffect(i_pEffectName, i_pEffectContent, i_pEditorData);
            return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
        }
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectEditorData(int i_EffectMgrID, RCMOD_String& o_EditorData)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCEffect* l_pCurEffect = l_pEffectMgr->getCurrentEffect();
        if (l_pCurEffect)
        {
            DOME_NS::DString l_EditorData;
            l_pCurEffect->getEditordata(l_EditorData);
            o_EditorData = l_EditorData.c_str();
            return DM_TRUE;
        }
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_SetCurrentEffect(int i_EffectMgrID, const char* i_pEffectName)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->setCurrentEffect(RC_NS::DHashString(i_pEffectName));
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_GetCurrentEffectName(int i_EffectMgrID, RCMOD_String& o_EffectName)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        o_EffectName = l_pEffectMgr->getCurrentEffectName().c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectGuid(int i_EffectMgrID, RCMOD_String& o_EffectGuid)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        o_EffectGuid = l_pEffectMgr->getCurrentEffect()->getGuid().c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API int RCMOD_GetCurrentEffectPropertyCount(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        return l_pEffectMgr->getCurrentEffect()->getEffectPropertyCount();
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyName(int i_EffectMgrID, int i_Index, RCMOD_String& o_PropName)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        o_PropName = l_pEffectMgr->getCurrentEffect()->getEffectPropertyRefKey(i_Index).c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyType(int i_EffectMgrID, int i_Index, RCMOD_String& o_PropType)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypeID l_TypeID = l_pEffectMgr->getCurrentEffect()->getEffectPropertyTypeID(i_Index);
        o_PropType = DOME_NS::DSimpleTypeManager::Instance().getTypeNameByID(l_TypeID).get().c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyEditCtrl(int i_EffectMgrID, int i_Index, RCMOD_String& o_EditCtrl)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        o_EditCtrl = l_pEffectMgr->getCurrentEffect()->getEffectPropertyEditCtrl(i_Index).c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValue(int i_EffectMgrID, int i_Index, RCMOD_String& o_StrValue)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DString l_StrValue;
        l_pEffectMgr->getCurrentEffect()->getEffectPropertyValueInString(i_Index, l_StrValue);
        o_StrValue = l_StrValue.c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValue(int i_EffectMgrID, int i_Index, const RCMOD_String& i_StrValue)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValueFromString(i_Index, i_StrValue.c_str());
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, RCMOD_String& o_StrValue)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        const DOME_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getCurrentEffect()->getEffectPropertyValue(i_Index);
        if(l_pValue && l_pValue->isDString())
        { 
            o_StrValue = l_pValue->getDString().c_str();
            return DM_TRUE;
        }
        return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, const RCMOD_String& i_StrValue)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypedValue l_Value(DOME_NS::DString(i_StrValue.c_str()));
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValue(i_Index, &l_Value);
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Value)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        const DOME_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getCurrentEffect()->getEffectPropertyValue(i_Index);
        if (l_pValue && l_pValue->isF32())
        {
            o_Value = l_pValue->getF32();
            return DM_TRUE;
        }
        return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Value)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypedValue l_Value(i_Value);
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValue(i_Index, &l_Value);
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        const DOME_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getCurrentEffect()->getEffectPropertyValue(i_Index);
        if (l_pValue && l_pValue->isDVector2f())
        {
            DOME_NS::DVector2f l_Value;
            l_Value = l_pValue->getDVector2f();
            o_Val0 = l_Value.x;
            o_Val1 = l_Value.y;
            return DM_TRUE;
        }
        return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypedValue l_Value(DOME_NS::DVector2f(i_Val0, i_Val1));
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValue(i_Index, &l_Value);
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1, float& o_Val2)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        const DOME_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getCurrentEffect()->getEffectPropertyValue(i_Index);
        if (l_pValue && l_pValue->isDVector3f())
        {
            DOME_NS::DVector3f l_Value;
            l_Value = l_pValue->getDVector3f();
            o_Val0 = l_Value.x;
            o_Val1 = l_Value.y;
            o_Val2 = l_Value.z;
            return DM_TRUE;
        }
        return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1, float i_Val2)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypedValue l_Value(DOME_NS::DVector3f(i_Val0, i_Val1, i_Val2));
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValue(i_Index, &l_Value);
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1, float& o_Val2, float& o_Val3)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        const DOME_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getCurrentEffect()->getEffectPropertyValue(i_Index);
        if (l_pValue && l_pValue->isDVector4f())
        {
            DOME_NS::DVector4f l_Value;
            l_Value = l_pValue->getDVector4f();
            o_Val0 = l_Value.x;
            o_Val1 = l_Value.y;
            o_Val2 = l_Value.z;
            o_Val3 = l_Value.w;
            return DM_TRUE;
        }
        return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1, float i_Val2, float i_Val3)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr && l_pEffectMgr->getCurrentEffect())
    {
        DOME_NS::DSimpleTypedValue l_Value(DOME_NS::DVector4f(i_Val0, i_Val1, i_Val2, i_Val3));
        l_pEffectMgr->getCurrentEffect()->setEffectPropertyValue(i_Index, &l_Value);
        return DM_TRUE;
    }
    return DM_FALSE;
}


RCMOD_API bool RCMOD_ResizeWindow(int i_EffectMgrID, int width, int height)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        return DM_SUCC(l_pEffectMgr->getRenderer()->clearTexturePool());
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_ResetParameter(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->getParamSys().clearAllParameters();
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_RegisterParameter(int i_EffectMgrID, const char* i_ParamName, const char* i_pParamType, RCParamKey& o_Key)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypeID l_TypeID(RC_NS::DStringHash((RC_NS::Char*)i_pParamType));
        RC_NS::DResult l_Result = l_pEffectMgr->getParamSys().registerParameter(i_ParamName, l_TypeID);
        RC_NS::DStringHash l_NameHash(i_ParamName);
        o_Key = *((unsigned __int64*)&l_NameHash);
        return DM_SUCC(l_Result) ? DM_TRUE : DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_CreateTexture2D(int i_EffectMgrID, const RCOSTextureData* i_pTextureData, RCMOD_Texture& o_TexID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
        RC_NS::OSTexture2D l_TexHandle;
        RC_NS::DResult l_Result = l_pRenderer->createTexture2DExternal(l_TexHandle, i_pTextureData);
        if (DM_SUCC(l_Result))
        {
            *((RC_NS::OSTexture2D*)o_TexID.getPtr()) = l_TexHandle;
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_LoadTexture2D(int i_EffectMgrID, const char* i_pFileName, RCMOD_Texture& o_TexID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
        RC_NS::OSTexture2D l_TexHandle;
        RC_NS::DResult l_Result = l_pRenderer->createTexture2DFromFile(l_TexHandle, i_pFileName);
        if (DM_SUCC(l_Result))
        {
            *((RC_NS::OSTexture2D*)o_TexID.getPtr()) = l_TexHandle;
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_CreateFrameTexture(int i_EffectMgrID, int i_Width, int i_Height, int i_Mipmap, int i_Format, int i_Usage, RCMOD_Texture& o_TexID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
        RC_NS::OSTexture2D l_TexHandle;
        RC_NS::DResult l_Result = l_pRenderer->createTexture2D(l_TexHandle, i_Width, i_Height, i_Mipmap, (RC_NS::RCGPUDATAFORMAT)i_Format, (RC_NS::RCBUFFUSAGE)i_Usage, DM_TRUE);
        if (DM_SUCC(l_Result))
        {
            *((RC_NS::OSTexture2D*)o_TexID.getPtr()) = l_TexHandle;
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_DestroyTexture2D(int i_EffectMgrID, const RCMOD_Texture& i_TexID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
        RC_NS::DResult l_Result = l_pRenderer->destroyTexture2D(*((RC_NS::OSTexture2D*)i_TexID.getPtr()));
        if (DM_SUCC(l_Result))
            return DM_TRUE;
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, const RCMOD_Texture& i_TexID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
        RC_NS::DResult l_Result;
        l_Result = l_pEffectMgr->getParamSys().setOSTexture2D(RC_NS::DStringHash(i_Key), *((RC_NS::OSTexture2D*)i_TexID.getPtr()));
        if(DM_SUCC(l_Result))
            return DM_TRUE;
        else
            return DM_FALSE;
    }
    return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setF32(i_Val);
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector2f(RC_NS::DVector2f(i_Val0, i_Val1));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1, float i_Val2)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector3f(RC_NS::DVector3f(i_Val0, i_Val1, i_Val2));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1, float i_Val2, float i_Val3)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector4f(RC_NS::DVector4f(i_Val0, i_Val1, i_Val2, i_Val3));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setInt(i_Val);
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector2i(RC_NS::DVector2i(i_Val0, i_Val1));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1, int i_Val2)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector3i(RC_NS::DVector3i(i_Val0, i_Val1, i_Val2));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1, int i_Val2, int i_Val3)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            l_pValue->setDVector4i(RC_NS::DVector4i(i_Val0, i_Val1, i_Val2, i_Val3));
            return DM_TRUE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float* i_pMatrix, int i_MatrixSize)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DSimpleTypedValue* l_pValue = l_pEffectMgr->getParamSys().getParameter(RC_NS::DStringHash(i_Key));
        if (l_pValue)
        {
            if (i_MatrixSize == 16)
            {
                l_pValue->setDMatrix4x4f(*((RC_NS::DMatrix4x4f*)i_pMatrix));
                return DM_TRUE;
            }
            else if (i_MatrixSize == 9)
            {
                l_pValue->setDMatrix3x3f(*((RC_NS::DMatrix3x3f*)i_pMatrix));
                return DM_TRUE;
            }
            else if (i_MatrixSize == 4)
            {
                l_pValue->setDMatrix2x2f(*((RC_NS::DMatrix2x2f*)i_pMatrix));
                return DM_TRUE;
            }
            else
                return DM_FALSE;
        }
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool  RCMOD_Build(int i_EffectMgrID)
{
	bool bRet = DM_FALSE;
	
	FRAMETIMER_BEGIN(FTT_RC_CAL_TOTALEXECUTE, FTT_RC_EXECUTE);
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->build();
        if(DM_SUCC(l_Result))
            bRet =  DM_TRUE;
        else
            bRet =  DM_FALSE;
    }
    else
        bRet =  DM_FALSE;
	FRAMETIMER_END(FTT_RC_CAL_TOTALEXECUTE);

	return bRet;
}

RCMOD_API bool RCMOD_Execute(int i_EffectMgrID)
{
	bool bRet = DM_FALSE;
	
	FRAMETIMER_BEGIN(FTT_RC_CAL_TOTALEXECUTE, FTT_RC_EXECUTE);
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->execute();
        if(DM_SUCC(l_Result))
            bRet =  DM_TRUE;
        else
            bRet =  DM_FALSE;
    }
    else
        bRet =  DM_FALSE;
	FRAMETIMER_END(FTT_RC_CAL_TOTALEXECUTE);

	return bRet;
}

RCMOD_API bool  RCMOD_NeedDoCapture(int i_EffectMgrID)
{
    if (i_EffectMgrID == g_pRCModData->m_CaptureMgrID)
    {
        return g_pRCModData->m_bDoCapture;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_GetCapturePath(int i_EffectMgrID, RCMOD_String& o_Path)
{
    if (RCMOD_NeedDoCapture(i_EffectMgrID))
    {
        o_Path = g_pRCModData->m_CapturePath.c_str();
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_FinishCapture(int i_EffectMgrID)
{
    if (RCMOD_NeedDoCapture(i_EffectMgrID))
    {
        g_pRCModData->m_bDoCapture = DM_FALSE;
        return DM_TRUE;
    }
    return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetExtreamOptimizationMode(int i_EffectMgrID, bool i_bEnable)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::DResult l_Result = l_pEffectMgr->setExtreamOptimizationMode(i_bEnable);
        if(DM_SUCC(l_Result))
            return DM_TRUE;
        else
            return DM_FALSE;
    }
    else
        return DM_FALSE;
}

RCMOD_API bool  RCMOD_SetSafeModifyMode(int i_EffectMgrID, bool i_bSafeMode)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        l_pEffectMgr->setSafeModifyMode(i_bSafeMode);
        return DM_TRUE;
    }
    else
        return DM_FALSE;
}