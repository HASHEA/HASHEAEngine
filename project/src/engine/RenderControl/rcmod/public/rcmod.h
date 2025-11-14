/*
    filename:       rcmod.h
    author:         Ming Dong
    date:           2016-MAY-13
    description:    
*/
#pragma once

#include "rcmod_def.h"
#include "rcmod_string.h"
#include "rcmod_float2.h"
#include "rcmod_float3.h"
#include "rcmod_float4.h"
#include "rcmod_float2x2.h"
#include "rcmod_float3x3.h"
#include "rcmod_float4x4.h"
#include "rcmod_texture.h"
#include "rcmod_osdep.h"

//plugin interfaces
#include "rcpi_scene.h"

/*
    Key name
*/
static const char* k_KEY_ColorInput     = "EXTERN::TEXTURE::ColorInput";
static const char* k_KEY_ScreenCap      = "EXTERN::TEXTURE::ScreenCap";
static const char* k_KEY_GBuffer0       = "EXTERN::TEXTURE::GBuffer0";
static const char* k_KEY_GBuffer1       = "EXTERN::TEXTURE::GBuffer1";
static const char* k_KEY_GBuffer2       = "EXTERN::TEXTURE::GBuffer2";
static const char* k_KEY_GBuffer3       = "EXTERN::TEXTURE::GBuffer3";
static const char* k_KEY_GBuffer4       = "EXTERN::TEXTURE::GBuffer4";
static const char* k_KEY_DepthInput     = "EXTERN::TEXTURE::DepthInput";
static const char* k_KEY_ResolvedDepthInput = "EXTERN::TEXTURE::ResolvedDepthInput";
static const char* k_KEY_MiniDepthInput = "EXTERN::TEXTURE::MiniDepthInput";
static const char* k_KEY_ShadowMaskInput= "EXTERN::TEXTURE::ShadowMaskInput";
static const char* k_KEY_CoverMapInput  = "EXTERN::TEXTURE::CoverMapInput";
static const char* k_KEY_Caustic		= "EXTERN::TEXTURE::Caustic";
static const char* k_KEY_NWData			= "EXTERN::TEXTURE::NWData";
static const char* K_KEY_FootprintsMask = "EXTERN::TEXTURE::FootprintsMask";
static const char* k_KEY_Output         = "EXTERN::RT::Output";

static const char* k_KEY_FullScreen     = "EXTERN::PARAMETER::FullScreen";              // this is discarded, replaced by EXTERN::PARAMETER::GBufferSize and EXTERN::PARAMETER::OutputSize
static const char* k_KEY_GBufferSize    = "EXTERN::PARAMETER::GBufferSize";
static const char* k_KEY_OutputSize     = "EXTERN::PARAMETER::OutputSize";
static const char* k_KEY_MainCamInfo0   = "EXTERN::PARAMETER::MainCamInfo0";
static const char* k_KEY_MainCamInfo1   = "EXTERN::PARAMETER::MainCamInfo1";
static const char* k_KEY_ViewMat        = "EXTERN::PARAMETER::ViewMat";
static const char* k_KEY_ProjMat        = "EXTERN::PARAMETER::ProjMat";
static const char* k_KEY_ViewProjMat    = "EXTERN::PARAMETER::ViewProjMat";
static const char* k_KEY_LastViewProjMat= "EXTERN::PARAMETER::LastViewProjMat";
static const char* k_KEY_ViewMatInv     = "EXTERN::PARAMETER::ViewMatInv";
static const char* k_KEY_ProjMatInv     = "EXTERN::PARAMETER::ProjMatInv";
static const char* k_KEY_ViewProjMatInv = "EXTERN::PARAMETER::ViewProjMatInv";
static const char* k_KEY_MilkywayViewProjMatInv = "EXTERN::PARAMETER::MilkywayViewProjMatInv";
static const char* k_KEY_MilkywayBrightness = "EXTERN::PARAMETER::MilkywayBrightness";    // x: Exposure
static const char* k_KEY_DeviceZToLinearZParam = "EXTERN::PARAMETER::DeviceZToLinearZParam";
static const char* k_KEY_CurrentTime	= "EXTERN::PARAMETER::CurrentTime";
static const char* k_KEY_DeltaTime		= "EXTERN::PARAMETER::DeltaTime";
static const char* k_KEY_CurrentFrame   = "EXTERN::PARAMETER::CurrentFrame";
static const char* k_KEY_ScaleUpParam   = "EXTERN::PARAMETER::ScaleUpParam";
static const char* k_KEY_EyePosition    = "EXTERN::PARAMETER::EyePosition";
static const char* k_KEY_MainPlayerPos	= "EXTERN::PARAMETER::MainPlayerPos";
static const char* k_KEY_LightDir		= "EXTERN::PARAMETER::LightDir";
static const char* k_KEY_MoonLightDir	= "EXTERN::PARAMETER::MoonLightDir";
static const char* k_KEY_MainLightDir	= "EXTERN::PARAMETER::MainLightDir";
static const char* k_KEY_MainLightDiffuse	= "EXTERN::PARAMETER::MainLightDiffuse";
static const char* k_KEY_MainLightDIndensity	= "EXTERN::PARAMETER::MainLightDIndensity";
static const char* k_KEY_EvnMapParam	= "EXTERN::PARAMETER::EnvMapParam"; // x: EvnMapIntensity, y: EvnMapSaturation
static const char* k_KEY_Exposure	    = "EXTERN::PARAMETER::Exposure";    // x: Exposure
static const char* k_KEY_OutlineDepth   = "EXTERN::PARAMETER::OutlineDepth";

static const char* k_KEY_GrassMoveRadius= "EXTERN::PARAMETER::GrassMoveRadius";
static const char* k_KEY_MainPlayerPos2D= "EXTERN::PARAMETER::MainPlayerPos2D";
static const char* k_KEY_GrassPForceNum = "EXTERN::PARAMETER::GrassPForceNum";
static const char* k_KEY_GrassPForce0   = "EXTERN::PARAMETER::GrassPForce0";
static const char* k_KEY_GrassPForce1   = "EXTERN::PARAMETER::GrassPForce1";
static const char* k_KEY_GrassPForce2   = "EXTERN::PARAMETER::GrassPForce2";
static const char* k_KEY_GrassPForce3   = "EXTERN::PARAMETER::GrassPForce3";
static const char* k_KEY_GrassOffset	= "EXTERN::PARAMETER::GrassOffset";
static const char* k_KEY_CoverMapRect	= "EXTERN::PARAMETER::CoverMapRect";

static const char* k_KEY_AOCameraInfo	= "EXTERN::PARAMETER::AOCamInfo";
static const char* k_KEY_AOMapInput		= "EXTERN::PARAMETER::AOMapInput";

static const char* k_KEY_PlayerPosWaterHeight	= "EXTERN::PARAMETER::PlayerPosWaterHeight";

static const char* K_KEY_TopCameraDepthInput = "EXTERN::TEXTURE::TopCameraDepthInput";

static const char* k_KEY_TopCameraViewMat = "EXTERN::PARAMETER::TopCameraViewMat";
static const char* k_KEY_TopCameraProjMat = "EXTERN::PARAMETER::TopCameraProjMat";
static const char* k_KEY_TopCameraViewProjMat   = "EXTERN::PARAMETER::TopCameraViewProjMat";

static const char* k_KEY_UWCameraViewProjMat   = "EXTERN::PARAMETER::UWCameraViewProjMat";
static const char* k_KEY_UWMWPros0			   = "EXTERN::PARAMETER::MWPros0";
static const char* k_KEY_UWMWPros1			   = "EXTERN::PARAMETER::MWPros1";
static const char* k_KEY_UWMWPros2			   = "EXTERN::PARAMETER::MWPros2";

static const char* k_KEY_ScreenStretchRate	   = "EXTERN::PARAMETER::ScreenStretchRate";

static const char* K_KEY_FootprintsMaskViewProjMat   = "EXTERN::PARAMETER::FootprintsMaskViewProjMat";

static const char* K_KEY_OceanHeight = "EXTERN::PARAMETER::OceanHeight";

static const char* K_KEY_GlobalWindDir = "EXTERN::PARAMETER::GlobalWindDir";

static const char* K_KEY_OverLookDepthRange = "EXTERN::PARAMETER::OverLookDepthRange";
static const char* K_KEY_OverLookDepthPos = "EXTERN::PARAMETER::OverLookDepthPos";
/*
    interface functions
*/
//RCMOD_API bool  RCMOD_CreateServer(int i_EffectMgr, int i_Port = 19677);
//RCMOD_API bool  RCMOD_DestroyServer();
RCMOD_API bool  RCMODInit(const char* i_pDataPath, bool i_bCreateServer, int i_Port = 19677);
RCMOD_API bool  RCMODUninit();

RCMOD_API bool  RCMOD_GetDataPath(RCMOD_String& o_DataPath);

RCMOD_API bool  RCMOD_RegisterPlugin(const RCMOD_String& i_PluginName, RCPluginInterface* i_pPlugin);

RCMOD_API bool  RCMOD_ReloadScriptOperator();

RCMOD_API bool  RCMOD_Update();

RCMOD_API bool  RCMOD_GetShaderFromSignature(const char* i_pSignature, RCMOD_String& o_Shader);

RCMOD_API int   RCMOD_CreateEffectManager(const RCOSRendererData* i_pParam);                           // sys dependent
RCMOD_API void  RCMOD_DestroyEffectManager(int i_EffectMgrID);                                           // sys dependent

RCMOD_API bool  RCMOD_RegisterEffectPlugin(int i_EffectMgrID, const RCMOD_String& i_PluginName, RCPluginInterface* i_pPlugin);

RCMOD_API bool  RCMOD_SetActiveEffectManager(int i_EffectMgrID);
RCMOD_API int   RCMOD_GetActiveEffectManager();
RCMOD_API bool  RCMOD_ClearAllEffects(int i_EffectMgrID);
RCMOD_API void  RCMOD_ResetAllEffects(int i_EffectMgrID);
RCMOD_API bool  RCMOD_CreateEffect(int i_EffectMgrID, const char* i_pEffectName, const char* i_pEffectContent, const char* i_pEditorData);
RCMOD_API bool  RCMOD_LoadEffect(int i_EffectMgrID, const char* i_pEffectName);
RCMOD_API bool  RCMOD_SaveCurrentEffect(int i_EffectMgrID);
RCMOD_API bool  RCMOD_UpdateEffect(int i_EffectMgrID, const char* i_pEffectName, const char* i_pEffectContent, const char* i_pEditorData);
RCMOD_API bool  RCMOD_GetCurrentEffectEditorData(int i_EffectMgrID, RCMOD_String& o_EditorData);
RCMOD_API bool  RCMOD_SetCurrentEffect(int i_EffectMgrID, const char* i_pEffectName);
RCMOD_API bool  RCMOD_GetCurrentEffectName(int i_EffectMgrID, RCMOD_String& o_EffectName);
RCMOD_API bool  RCMOD_GetCurrentEffectGuid(int i_EffectMgrID, RCMOD_String& o_EffectGuid);
RCMOD_API int   RCMOD_GetCurrentEffectPropertyCount(int i_EffectMgrID);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyName(int i_EffectMgrID, int i_Index, RCMOD_String& o_PropName);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyType(int i_EffectMgrID, int i_Index, RCMOD_String& o_PropType);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyEditCtrl(int i_EffectMgrID, int i_Index, RCMOD_String& o_EditCtrl);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValue(int i_EffectMgrID, int i_Index, RCMOD_String& o_StrValue);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValue(int i_EffectMgrID, int i_Index, const RCMOD_String& i_StrValue);

RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, RCMOD_String& o_StrValue);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, const RCMOD_String& i_StrValue);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Value);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Value);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1, float& o_Val2);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1, float i_Val2);
RCMOD_API bool  RCMOD_GetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float& o_Val0, float& o_Val1, float& o_Val2, float& o_Val3);
RCMOD_API bool  RCMOD_SetCurrentEffectPropertyValueFast(int i_EffectMgrID, int i_Index, float i_Val0, float i_Val1, float i_Val2, float i_Val3);

RCMOD_API bool  RCMOD_ResizeWindow(int i_EffectMgrID, int width, int height);

RCMOD_API bool  RCMOD_ResetParameter(int i_EffectMgrID);
RCMOD_API bool  RCMOD_RegisterParameter(int i_EffectMgrID, const char* i_ParamName, const char* i_pParamType, RCParamKey& o_Key);
RCMOD_API bool  RCMOD_CreateTexture2D(int i_EffectMgrID, const RCOSTextureData* i_pTextureData, RCMOD_Texture& o_TexID);
RCMOD_API bool  RCMOD_LoadTexture2D(int i_EffectMgrID, const char* i_pFileName, RCMOD_Texture& o_TexID);
/*
    Create a texture which live only in one frame.
    The texture creator should guarantee the texture is destroied by calling 
    function 'RCMOD_DestroyTexture2D' before the end of the current frame.
    i_Mipmap: 
            < 0 -> generate mipmaps
            = 0 -> create all the mipmaps untill 1X1
            > 0 -> number of mipmaps
    i_Format:
            1  -> DXGI_FORMAT_R8G8B8A8_UNORM
            2  -> DXGI_FORMAT_B8G8R8A8_UNORM
            3  -> DXGI_FORMAT_R16G16B16A16_FLOAT
            4  -> DXGI_FORMAT_D24_UNORM_S8_UINT
            5  -> DXGI_FORMAT_D32_FLOAT
    i_Usage:
            0  -> RBU_DEFAULT
            1  -> RBU_IMMUTABLE
            2  -> RBU_DYNAMIC
            3  -> RBU_STAGE
*/
RCMOD_API bool  RCMOD_CreateFrameTexture(int i_EffectMgrID, int i_Width, int i_Height, int i_Mipmap, int i_Format, int i_Usage, RCMOD_Texture& o_TexID);
RCMOD_API bool  RCMOD_DestroyTexture2D(int i_EffectMgrID, const RCMOD_Texture& i_TexID);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, const RCMOD_Texture& i_TexID);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1, float i_Val2);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float i_Val0, float i_Val1, float i_Val2, float i_Val3);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1, int i_Val2);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, int i_Val0, int i_Val1, int i_Val2, int i_Val3);
RCMOD_API bool  RCMOD_SetParameter(int i_EffectMgrID, RCParamKey i_Key, float* i_pMatrix, int i_MatrixSize);

RCMOD_API bool  RCMOD_Build(int i_EffectMgrID);
RCMOD_API bool  RCMOD_Execute(int i_EffectMgrID);

RCMOD_API bool  RCMOD_NeedDoCapture(int i_EffectMgrID);
RCMOD_API bool  RCMOD_GetCapturePath(int i_EffectMgrID, RCMOD_String& o_Path);
RCMOD_API bool  RCMOD_FinishCapture(int i_EffectMgrID);

// rendering info functions
RCMOD_API bool  RCMOD_ExecuteBegin(int i_EffectMgrID);
RCMOD_API bool  RCMOD_ExecuteEnd(int i_EffectMgrID);
RCMOD_API bool  RCMOD_GetTextureUsedInfo(int i_EffectMgrID, double& o_TexLoaded, double& o_TexFromPool, double& o_TexCreated, double& o_TexImported);

RCMOD_API bool  RCMOD_SetExtreamOptimizationMode(int i_EffectMgrID, bool i_bEnable);
RCMOD_API bool  RCMOD_SetSafeModifyMode(int i_EffectMgrID, bool i_bSafeMode);