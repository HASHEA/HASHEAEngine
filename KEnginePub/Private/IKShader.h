#ifndef IKSHADER_H
#define IKSHADER_H

#include "KBase/Public/KBasePub.h"
#include "KEnginePub/Public/IKHeader.h"
#include "KEngine/Public/IK3DTypes.h"
#include "Engine/KUniqueString.h"

// 0-SKIN_UBO_BIND_COUNT给skin_instance用

// For Core Engine
#define UI_SHADER_FILE                                                   "data/shader/glsl/ui.glsl"
#define SIMPLE_UI_SHADER_DEF                                             "simple"
#define TTFONT_SHADER_DEF                                                "ttfont"
#define UI_SAMPLER_SHADER_DEF                                            "ui_sampler"


#define GRAPHIC_SHADER_FILE                                              "data/shader/glsl/graphictool.glsl"
#define GRAPHIC_VERT_COLOR_DEF                                           "graphictool_vert_color"
#define GRAPHIC_VERT_COLOR_DEF2                                          "graphictool_vert_color2"
#define GRAPHIC_VERT_COLOR_INSTANCE_DEF                                  "graphictool_vert_color_instance"
#define GRAPHIC_SCREEN_RECT_TEX_DEF                                      "screen_rect_tex"
#define GRAPHIC_SCREEN_RECT_COLOR_DEF                                    "screen_rect_color"
#define GRAPHIC_ROTATE_CIRCLE_COLOR_DEF                                  "rotate_circle_color"


#define DEFERRED_SHADER_FILE                                             "data/shader/glsl/deferred.glsl"
#define DEFERRED_MESH_MODEL_DEF                                          "deferred_mesh_model"
#define DEFERRED_MESH_MODEL_ALPHATEST_DEF                                "deferred_mesh_model_alphatest"
#define DEFERRED_QUAD_DIRLIGHT_DEF                                       "deferred_quad_dirlight"
#define DEFERRED_POINTLIGHT_DEF                                          "deferred_pointlight"


#define TERRAIN_LD_SHADER_FILE                                           "data/shader/glsl/terrain.glsl"
#define TERRAIN_SHADER_FILE                                              "data/material/shader_mb/terrain.glsl"
#define TERRAIN_BLEND_DEFERRED_DEF                                       "terrain_blend_deferred"
#define TERRAIN_BLEND_FORWARD_DEF                                        "terrain_blend_forward"


#define MESHMODEL_SHADER_FILE                                            "data/shader/glsl/meshmodel.glsl"
#define MESHMODEL_DEF                                                    "deferred_mesh_model"
#define MESHMODEL_INSTANCE_DEF                                           "deferred_mesh_model_instance"
#define MESHMODEL_INSTANCE_DEF_OLD                                       "deferred_mesh_model_instance_old"
#define MESHMODEL_DETAIL_DEF                                             "deferred_detail_mesh_model"
#define MESHMODEL_DETAIL_INSTANCE_DEF                                    "deferred_detail_mesh_model_instance"
#define MESHMODEL_DETAIL_INSTANCE_DEF_OLD                                "deferred_detail_mesh_model_instance_old"

#define SKIN_SHADER_FILE                                                 "data/shader/glsl/skin.glsl"
// #define SKIN_DEF_512                          "deferred_skin_model_512"
#define SKIN_DEF_256                                                     "deferred_skin_model_256"
// #define SKIN_DETAIL_DEF_512                   "deferred_detail_skin_model_512"
#define SKIN_DETAIL_DEF_256                                              "deferred_detail_skin_model_256"
#define SKIN_DEF_INSTANCE                                                "deferred_skin_model_instance"
#define SKIN_DETAIL_DEF_INSTANCE                                         "deferred_detail_skin_model_instance"
#define SKIN_UBO_DEF                                                     "deferred_ubo_skin_model"
#define SKIN_UBO_DETAIL_DEF                                              "deferred_detail_ubo_skin_model"


#define SFX_SHADER_FILE                                                  "data/shader/glsl/sfx.glsl"
#define SFX_PARTICAL_DEF                                                 "sfx_partical"
#define SFX_BILLBORD_TEX0_DEF                                            "sfx_billbord_tex0"
#define SFX_BILLBORD_TEX1_DEF                                            "sfx_billbord_tex1"
#define SFX_MODEL_DEF                                                    "sfx_model"
// #define SFX_SKINMODEL_DEF_512                   "sfx_skinmodel_512"
#define SFX_SKINMODEL_DEF_256                                            "sfx_skinmodel_256"
#define SFX_SKINMODEL_UBO_DEF                                            "sfx_skinmodel_ubo"
#define SFX_MODEL_DETAIL_DEF                                             "sfx_model_detail"
// #define SFX_SKINMODEL_DETAIL_DEF_512            "sfx_skinmodel_detail_512"
#define SFX_SKINMODEL_DETAIL_DEF_256                                     "sfx_skinmodel_detail_256"
#define SFX_SKINMODEL_DETAIL_UBO_DEF                                     "sfx_skinmodel_detail_ubo"
#define SFX_GEOMETRY_NOBLEND                                             "sfx_geometry_noblend"
#define SFX_GEOMETRY_BLEND                                               "sfx_geometry_blend"

// 文件+Pass名称相同可能会造成生成二进制Shader冲突
#define POSTRENDER_FILE                                                  "data/shader/glsl/postrender.glsl"
#define SHOCKWAVE_DEF                                                    "shockwave"
#define GAUSSIAN_BLUR_DEF                                                "gaussian_blur"
#define DOF_DEF                                                          "dof"
#define BLOOM_DEF                                                        "bloom"
#define VIGNETTE_DEF                                                     "vignette"
#define MERGE_AO_SHADOWMAP_DEF                                           "merge_ao_shadowmap"
#define SIMPLE_SPHERE_SHADOW_DEF                                         "simple_sphere_shadow"
#define HSV_DEF                                                          "hsv"
#define TONEMAPPING_DEF                                                  "tonemapping"
#define TONEMAPPING_BLOOM_DEF                                            "tonemappingwithbloom"
#define COLOR_GRADE_DEF                                                  "colorgrade"

#define SPEEDTREE_PBR_SHADER_FILE                                        "data/material/shader_mb/speedtree.glsl"
#define SPEEDTREE_SHADER_FILE                                            "data/shader/glsl/speedtree.glsl"
#define SPEEDTREE_DEFFER_DEF                                             "speedtree_leaf"
#define SPEEDTREE_DEFFER_INSTANCE_DEF                                    "speedtree_leaf_instance"
#define SPEEDTREE_DEF                                                    "speedtree_leaf_forward"
#define SPEEDTREE_INSTANCE_DEF                                           "speedtree_leaf_instance_forward"
#define SPEEDTREE_INSTANCE_DEF_OLD                                       "speedtree_leaf_instance_old"

#define PLANT_PBR_SHADER_FILE                                            "data/material/shader_mb/plant_hd.glsl"
#define PLANT_SHADER_FILE                                                "data/shader/glsl/plant.glsl"
#define PLANT_DEF                                                        "plant"
#define PLANT_INSTANCE_DEF                                               "plant_instance"
#define PLANT_INSTANCE_DEF_OLD                                           "plant_instance_old"

#define WATER_SHADER_FILE                                                "data/shader/glsl/water.glsl"
#define WATER_INSTANCE_DEF                                               "water_instance"
#define WATER_INSTANCE_DEF_OLD                                           "water_instance_old"
#define WATER_INSTANCE_DEF_SSR_OLD                                       "water_instance_ssr_old"
#define WATER_INSTANCE_SHOCKWAVE_DEF                                     "water_instance_shockwave"
#define WATER_INSTANCE_SHOCKWAVE_DEF_OLD                                 "water_instance_shockwave_old"

#define SKYBOX_SHADER_FILE                                               "data/shader/glsl/skybox.glsl"
#define SKYBOX_DEF                                                       "skybox"
#define SKYBOX_TEX_DEF                                                   "skybox_tex"

#define CLOUD_SHADER_FILE                                                "data/shader/glsl/cloud.glsl"
#define CLOUD_DEF                                                        "cloud"
#define CLOUD_ILLUM_DEF                                                  "cloudWithIllum"

#define FARMOUNTAIN_SHADER_FILE                                          "data/shader/glsl/farmountain.glsl"
#define FARMOUNTAIN_DEF                                                  "farmountain"

#define LENSFLARE_SHADER_FILE                                            "data/shader/glsl/lensflare.glsl"
#define LENSFLARE_DEF                                                    "lensflare"

#define SSAO_SHADER_FILE                                                 "data/shader/glsl/ssao.glsl"
#define SSAO_DEF                                                         "ssao"


#define SHADOWMAP_SHADER_FILE                                            "data/shader/glsl/shadowmap.glsl"
#define MODEL_SM_DEF                                                     "model_shadowmap"
#define MODEL_SM_INSTANCE_DEF                                            "model_shadowmap_instance"
#define MODEL_SM_INSTANCE_DEF_OLD                                        "model_shadowmap_instance_old"
#define MODEL_SM_TERRAIN_DEF                                             "terrain_shadowmap"
#define SPEEDTREE_LEAF_SM_DEF                                            "speedtree_leaf_shadowmap"
#define SPEEDTREE_LEAF_SM_INSTANCE_DEF                                   "speedtree_leaf_shadowmap_instance"
#define SPEEDTREE_LEAF_SM_INSTANCE_DEF_OLD                               "speedtree_leaf_shadowmap_instance_old"
// #define SKIN_SM_DEF_512                       "skin_shadowmap_512"
#define SKIN_SM_DEF_256                                                  "skin_shadowmap_256"
#define SKIN_SM_DEF_UBO                                                  "skin_shadowmap_ubo"
#define SKIN_SM_INSTANCE_DEF                                             "skin_shadowmap_instance"

#define WEATHER_SHADER_FILE                                              "data/shader/glsl/weather.glsl"
#define WEATHER_DEF                                                      "weather"
#define WEATHER_DEPTH_DEF                                                "weather_depth"


#define OCCLUDE_SHADER_FILE                                              "data/shader/glsl/occlude.glsl"
#define OCCLUDE_DEF                                                      "occlude"

// UI
#define UI_FONT_SHADER_FILE                                              "data/shader/glsl/ui_font.glsl"
#define UI_FONT_SHADER_DEF                                               "ui_font"

#define UI_IMAGE_SHADER_FILE                                             "data/shader/glsl/ui_image.glsl"
#define UI_IMAGE_COPY_DEF                                                "ui_image_copy"
#define UI_IMAGE_GRAY_DEF                                                "ui_image_gray"

#define UI_IMAGE_BUFFER_FILE                                             "data/shader/glsl/ui_buffer.glsl"
#define UI_IMAGE_BUFFER_COLOUR_DEF                                       "ui_buffer_colour"
#define UI_IMAGE_BUFFER_UV_DEF                                           "ui_buffer_uv"


#define UNIFORM_BUFFER_GLOBAL_MAT_BLOCK                                  "globalParam_mat"
#define UNIFORM_BUFFER_SKIN_BLOCK                                        "globalParam_skin"
#define UNIFORM_BUFFER_NEWMTL_FOG_BLOCK                                  "fogParam_uniform_block"
#define UNIFORM_BUFFER_NEWMTL_COMMON_PARAM_BLOCK                         "commonParam_uniform_block"
#define UNIFORM_BUFFER_NEWMTL_POINT_LIGHT_FWDPLUS_BLOCK                  "_AdditionalLights"
#define UNIFORM_BUFFER_NEWMTL_POINT_LIGHT_FWDPLUS_TILE_CLUSTER_IDS_BLOCK "globalParam_fwd_plus_clusterid_point_light"

#define OIT_SHADER_FILE                                                  "data/material/shader_mb/oit.glsl"
#define OIT_COMPISITE_FORWARD_DEF                                        "oit_composite"

#define EMPTY_INCLUDE_SHADER_FILE                                        "data/shader/glsl/empty.glsl"


#define TERRAIN_HD_FILE                                                  "data/material/shader_mb/terrain_hd.glsl"
#define TERRAIN_HD_BLEND_COPY_DEF                                        "TextureCopy"
#define TERRAIN_HD_BLEND_CLASSIC_DEFFER_COPY_DEF                         "TextureCopy_ClassicDeffer"

#define WATER_FILE                                                       "data/material/shader_mb/classicwater_hd.glsl"
#define WATER_HOLE_DEF                                                   "WaterHoleStencil"

#define UNIFORM_SKIN_INSTANCE_BLOCK_ID_COUNT                             10
#define UNIFORM_BUFFER_USER_ID_COUNT                                     5
#define INTERNAL_TEX_SLOT_START                                          16

int32_t GetUniformBufferBegin();

#define UNIFORM_BUFFER_ID_BEGIN GetUniformBufferBegin()

// ubo binding port
enum UNIFORM_BUFFE_BINDINGPORT
{
    UNIFORM_BUFFER_GLOBAL_MAT_BLOCK_ID,
    UNIFORM_BUFFER_GLOBAL_LIGHT_BLOCK_ID,
    UNIFORM_BUFFER_GLOBAL_FOG_BLOCK_ID,
    UNIFORM_BUFFER_GLOBAL_FOGCOLOR_BLOCK_ID,

    // NewMtl
    UNIFORM_BUFFER_NEWMTL_SUN_LIGHT_BLOCK_ID,    // 场景或角色直射光信息
    UNIFORM_BUFFER_NEWMTL_CAMERA_LIGHT_BLOCK_ID, // 镜头光信息
    UNIFORM_BUFFER_ENV_LIGHT,                    // 场景或角色环境光信息

    UNIFORM_BUFFER_NEWMTL_FOG_BLOCK_ID,
    UNIFORM_BUFFER_NEWMTL_COMMON_PARAM_BLOCK_ID,
    UNIFORM_BUFFER_SKIN_BLOCK_ID,

    UNIFORM_BUFFER_PREDRAW,                                              // model存放world worldI worldIT信息
    UNIFORM_BUFFER_NEWMTL_POINT_LIGHT_FWDPLUS_BLOCK_ID,                  // 存放forward+镜头裁剪的点光源信息
#ifndef MOBILE_DYNAMIC_LIGHT
    UNIFORM_BUFFER_NEWMTL_POINT_LIGHT_FWDPLUS_TILE_CLUSTER_IDS_BLOCK_ID, // 存放forward+的tile裁剪点光源id
#endif

    // shader local uniform buffer, 下面id是复用的, 个数为UNIFORM_BUFFER_USER_ID_COUNT
    UNIFORM_BUFFER_USER_ID0,
    UNIFORM_BUFFER_USER_ID1,
    UNIFORM_BUFFER_USER_ID2,
    UNIFORM_BUFFER_USER_ID3,
    UNIFORM_BUFFER_USER_ID4,
    // end
    UNIFORM_BUFFER_BLOCK_COUNT,
};

// #define UNIFORM_BUFFER_GLOBAL_MAT_BLOCK_ID_0                10
// #define UNIFORM_BUFFER_GLOBAL_LIGHT_BLOCK_ID_0              UNIFORM_BUFFER_GLOBAL_MAT_BLOCK_ID_0 + 1
// #define UNIFORM_BUFFER_GLOBAL_FOG_BLOCK_ID_0                UNIFORM_BUFFER_GLOBAL_MAT_BLOCK_ID_0 + 2
// #define UNIFORM_BUFFER_GLOBAL_FOGCOLOR_BLOCK_ID_0           UNIFORM_BUFFER_GLOBAL_MAT_BLOCK_ID_0 + 3
//


#define PER_SKIN_UBO_VEC4_COUNT 1024

enum KSHADER_TYPE
{
    // For Core Engine
    SIMPLE_SCREEN_UIRECT_SHADER,
    TTFONT_SHADER,
    GRAPHIC_VERT_COLOR_SHADER,
    DEFERRED_SHADER,
    TERRAIN_SHADER,
    MESHMODE_SHADER,
    SKIN_SHADER,
    SFX_SHADER,
    POSTRENDER_SHADER,
    SPEEDTREE_HD_SHADER,
    SPEEDTREE_LD_SHADER,
    PLANT_HD_SHADER,
    PLANT_LD_SHADER, // KPlantShaderLD
    WATER_SHADER,
    SKYBOX_SHADER,
    CLOUD_SHADER,
    FARMOUNTAIN_SHADER,
    LENSFLARE_SHADER,
    SSAO_SHADER, //"data/shader/glsl/ssao.glsl"
    SHADOWMAP_SHADER,
    WEATHER_SHADER,
    OCCLUDE_SHADER,
    OIT_SHADER,

    TERRAIN_HD_COPY_SHADER,
    WATER_HOLE_SHADER,

    // For 3DUI
    UI_FONT_SHADER,
    UI_IMAGE_COPY_SHADER,
    UI_IMAGE_GRAY_SHADER,
    UI_IMAGE_BUFFER_COLOUR_SHADER,
    UI_IMAGE_BUFFER_UV_SHADER,


    // INSPACK
    THE_MAX_SHADER_NUM
};

/*typedef unsigned int MacroMasks;*/
typedef uint32_t MarcoKeyType;

struct MacroMasks
{
    // for KG3DDynamicRunTimeMacro reference
    union {
        MarcoKeyType mask; // 32位
                           // #ifdef _WIN32
        struct
        {
            // 调试看内存用
            unsigned _NORMALMAP : 1;
            unsigned _SHADOWMAP : 1;
            unsigned _FOG       : 1;
            unsigned _TWOSIDE   : 1;

            unsigned _BONES_PREVERT  : 1;
            unsigned _RENDERING_PATH : 1;
            unsigned _SPEEDTREE      : 1;
            unsigned _LIGHTPROBE_SH9 : 1;

            unsigned _MAINPLAYER     : 1;
            unsigned _ALPHATEST      : 1;
            unsigned _UBO_SKIN       : 1;
            unsigned _LIGHTING_POINT : 1;

            unsigned _SHADOW_LEVEL    : 1;
            unsigned _PLATFORM        : 1;
            unsigned _BONECOUNT_LEVEL : 1;
            unsigned _POSTEFFECT_MASK : 1;

            unsigned _SPLINE_MESH  : 1;
            unsigned _SHADER_FLAG  : 1;
            unsigned _CAMERA_LIGHT : 1;
            unsigned _SHOCKWAVE    : 1;

            unsigned _DEBUG_PBR      : 1;
            unsigned _VARYINGS_MASK  : 1;
            unsigned _VIEWPROBE_MASK : 1;
            unsigned _MRT_MASK       : 1;

            unsigned _BILLBOARD : 1;
            unsigned _LOD_LEVEL : 1;
            unsigned _26        : 1;
            unsigned _27        : 1;

            unsigned _28 : 1;
            unsigned _29 : 1;
            unsigned _30 : 1;
            unsigned _31 : 1;
        };
        // #endif
    };
    union {
        uint8_t  value[sizeof(MarcoKeyType) * 8]; // 32位对应值
        uint64_t value64[4];
    };


    MacroMasks()
    {
        mask = 0;
        memset(value, 0, sizeof(value));
    }

    MacroMasks(int key)
    {
        mask = key;
        memset(value, 0, sizeof(value));
    }

    MacroMasks(const MacroMasks& m)
    {
        mask = m.mask;
        memcpy(value, m.value, sizeof(value));
    }

    BOOL operator&(int n) const
    {
        return mask & n;
    }

    BOOL operator|(int n) const
    {
        return mask | n;
    }

    void operator|=(int n)
    {
        mask |= n;
    }

    void operator&=(int n)
    {
        mask &= n;
    }

    int operator<(const MacroMasks& m) const
    {
        if (mask != m.mask)
        {
            return mask < m.mask;
        }

        for (int i = 0; i < 4; i++)
        {
            if (value64[i] != m.value64[i])
            {
                return value64[i] < m.value64[i];
            }
        }
        return 0;
    }

    BOOL operator==(const MacroMasks& m) const
    {
        if (mask != m.mask)
        {
            return FALSE;
        }

        return memcmp(value, m.value, sizeof(value)) == 0;
    }

    BOOL operator!=(const MacroMasks& m) const
    {
        if (mask != m.mask)
        {
            return TRUE;
        }

        return memcmp(value, m.value, sizeof(value)) != 0;
    }

    int _Log2(uint32_t n)
    {
        int count = 0;
        while (n >>= 1)
            ++count;
        return count;
    }

    BOOL AddMacro(uint32_t eMacro, uint8_t uValue)
    {
        mask      |= eMacro;
        int index  = _Log2(eMacro);
        if (value[index] != uValue)
        {
            value[index] = uValue;
            return true;
        }
        else
        {
            return false;
        }
    }

    uint8_t GetMarco(uint32_t eMacro)
    {
        mask      |= eMacro;
        int index  = _Log2(eMacro);
        return value[index];
    }
};

class IKShaderEffect
{
public:
    virtual const std::string& GetEffectName() = 0;
    virtual const std::string& GetVsName()     = 0;
    virtual const std::string& GetFsName()     = 0;
    // virtual const char* GetVsVersion() = 0;
    // virtual const char* GetFsVersion() = 0;
};

class IKShaderFile
{
public:
    virtual IKShaderEffect* GetShaderEffectDefine(int i)      = 0;
    virtual int             GetShaderEffectCount()            = 0;
    virtual void            SetIsMtlInsPack(BOOL bMtlInspack) = 0;
    virtual BOOL            IsMtlInsPack()                    = 0;
};

class KPreLinkShaderProgram
{
public:
    virtual void PreLinkProcess(uint32_t programeId) = 0;
    virtual ~KPreLinkShaderProgram() {}
};

class KGLSampler;

class IKShaderProgram
{
public:
    virtual uint32_t    GetProgramId()                                            = 0;
    virtual BOOL        IsLinkError()                                             = 0;
    virtual void        SetLinkError(BOOL bError)                                 = 0;
    virtual int         AddRef()                                                  = 0;
    virtual int         Release()                                                 = 0;
    virtual KGLSampler* GetTextureSamplerState(const std::string& cszSamplerName) = 0;
    virtual BOOL        Reload()                                                  = 0;
    virtual BOOL        IsBindUBO()                                               = 0;
    virtual void        SetIsBindUBO(BOOL bBindUBO)                               = 0;
    virtual BOOL        IsBindLocalUBO()                                          = 0;
    virtual void        SetBindLocalUBO(BOOL bBind)                               = 0;
    virtual void        SetIsMtlInsPack(BOOL bMtlInspack)                         = 0;
    virtual BOOL        IsMtlInsPack()                                            = 0;
    virtual void        SetProgramObjectName()                                    = 0;
    virtual int         GetMaterialID()                                           = 0;
    virtual ~IKShaderProgram() {}
};

class IKShader;

class IKShaderLoader
{
public:
    virtual IKShaderProgram* GetShaderProgram(BOOL bMtlInsPack, const char* pcszShaderFileName, const char* pcszEffectName, const char* pcszUserShaderName = nullptr, const char* pcszMacro = nullptr, const MacroMasks* mask = nullptr, KPreLinkShaderProgram* pPreLink = nullptr) = 0;
    virtual ~IKShaderLoader() {}
    virtual IKShaderFile* GetShaderFile(BOOL bMtlInsPack, const std::string& pcszShaderFileName) = 0;
    virtual void          ClearUserShaderSource(const std::string& fileName)                     = 0;
};

class IKShader
{
public:
    virtual BOOL LoadShader(IKShaderLoader* pShaderLoader) = 0;
    virtual ~IKShader() {}
};

struct KEffectMacro
{
    KUniqueStr ustrName;
    KUniqueStr ustrDefinition;

    KEffectMacro()
    {
    }

    KEffectMacro(const KUniqueStr& _ustrName, const KUniqueStr& _ustrValue)
    {
        ustrName       = _ustrName;
        ustrDefinition = _ustrValue;
    }

    // or = default
    KEffectMacro(KEffectMacro&& marco)
    {
        ustrName       = marco.ustrName;
        ustrDefinition = marco.ustrDefinition;
    }
};

/*对应 g_KG3DRenderPassName */
enum EKG3DRenderPass
{
    RENDERPASS_EARLYZ,
    RENDERPASS_COLOR,
    RENDERPASS_SHOCKWAVE,

    RENDERPASS_COLORSOFTMASK,
    RENDERPASS_PREVIEW,
    RENDERPASS_EARLYZ_INSTANCE,
    RENDERPASS_COLOR_INSTANCE,
    RENDERPASS_3SDIFFUSE,
    RENDERPASS_3SBLUR,
    RENDERPASS_3SCOMBINE,
    RENDERPASS_OIT_COLOR0,
    RENDERPASS_OIT_COLOR1,
    RENDERPASS_OIT_COLOR0_INSTANCE,
    RENDERPASS_TERRAIN_HI_SUM_ALPHA,
    RENDERPASS_TERRAIN_HI_BLEND,
    RENDERPASS_TERRAIN_HI_FINAL_3X3,
    RENDERPASS_TERRAIN_HI_FINAL_5X5,
    RENDERPASS_TERRAIN_LOW_FINAL,
    RENDERPASS_TERRAIN_COPY_CACHE,
    RENDERPASS_GBUFFER_WRITING,
    RENDERPASS_DEFERRED_POINT_LIGHT,
    RENDERPASS_DEFERRED_SPOT_LIGHT,
    RENDERPASS_DEFERRED_DIRECTIONAL_LIGHT,
    RENDERPASS_DEFERRED_SUN_LIGHT,

    RENDERPASS_DEPTH_NO_PS,

    RENDERPASS_RASTERIZATION_SW,
    RENDERPASS_RASTERIZATION_HW,
    RENDERPASS_RASTERIZATION_HW_RTV_OUTPUT,

    RENDERPASS_PASS0,
    RENDERPASS_PASS1,
    RENDERPASS_PASS2,
    RENDERPASS_PASS3,
    RENDERPASS_PASS4,
    RENDERPASS_PASS5,
    RENDERPASS_PASS6,
    RENDERPASS_PASS7,
    RENDERPASS_PASS8,
    RENDERPASS_PASS9,
    RENDERPASS_PASS10,
    RENDERPASS_PASS11,
    RENDERPASS_PASS12,
    RENDERPASS_PASS13,
    RENDERPASS_PASS14,
    RENDERPASS_PASS15,
    RENDERPASS_PASS16,
    RENDERPASS_PASS17,
    RENDERPASS_PASS18,
    RENDERPASS_PASS19,
    RENDERPASS_PASS20,
    RENDERPASS_PASS21,
    RENDERPASS_PASS22,
    RENDERPASS_PASS23,
    RENDERPASS_PASS24,
    RENDERPASS_PASS25,
    RENDERPASS_PASS26,
    RENDERPASS_PASS27,
    RENDERPASS_PASS28,
    RENDERPASS_PASS29,
    RENDERPASS_PASS30,

    RENDERPASS_COUNT,
};

static const char* g_strRenderPassName[] =
    {
        "RENDERPASS_EARLYZ",
        "RENDERPASS_COLOR",
        "RENDERPASS_SHOCKWAVE",

        "RENDERPASS_COLORSOFTMASK",
        "RENDERPASS_PREVIEW",
        "RENDERPASS_EARLYZ_INSTANCE",
        "RENDERPASS_COLOR_INSTANCE",
        "RENDERPASS_3SDIFFUSE",
        "RENDERPASS_3SBLUR",
        "RENDERPASS_3SCOMBINE",
        "RENDERPASS_OIT_COLOR0",
        "RENDERPASS_OIT_COLOR1",
        "RENDERPASS_OIT_COLOR0_INSTANCE",
        "RENDERPASS_TERRAIN_HI_SUM_ALPHA",
        "RENDERPASS_TERRAIN_HI_BLEND",
        "RENDERPASS_TERRAIN_HI_FINAL_3X3",
        "RENDERPASS_TERRAIN_HI_FINAL_5X5",
        "RENDERPASS_TERRAIN_LOW_FINAL",
        "RENDERPASS_TERRAIN_COPY_CACHE",
        "RENDERPASS_GBUFFER_WRITING",
        "RENDERPASS_DEFERRED_POINT_LIGHT",
        "RENDERPASS_DEFERRED_SPOT_LIGHT",
        "RENDERPASS_DEFERRED_DIRECTIONAL_LIGHT",
        "RENDERPASS_DEFERRED_SUN_LIGHT",

        "RENDERPASS_DEPTH_NO_PS",

        "RENDERPASS_RASTERIZATION_SW",
        "RENDERPASS_RASTERIZATION_HW",
        "RENDERPASS_RASTERIZATION_HW_RTV_OUTPUT",

        "RENDERPASS_PASS0",
        "RENDERPASS_PASS1",
        "RENDERPASS_PASS2",
        "RENDERPASS_PASS3",
        "RENDERPASS_PASS4",
        "RENDERPASS_PASS5",
        "RENDERPASS_PASS6",
        "RENDERPASS_PASS7",
        "RENDERPASS_PASS8",
        "RENDERPASS_PASS9",
        "RENDERPASS_PASS10",
        "RENDERPASS_PASS11",
        "RENDERPASS_PASS12",
        "RENDERPASS_PASS13",
        "RENDERPASS_PASS14",
        "RENDERPASS_PASS15",
        "RENDERPASS_PASS16",
        "RENDERPASS_PASS17",
        "RENDERPASS_PASS18",
        "RENDERPASS_PASS19",
        "RENDERPASS_PASS20",
        "RENDERPASS_PASS21",
        "RENDERPASS_PASS22",
        "RENDERPASS_PASS23",
        "RENDERPASS_PASS24",
        "RENDERPASS_PASS25",
        "RENDERPASS_PASS26",
        "RENDERPASS_PASS27",
        "RENDERPASS_PASS28",
        "RENDERPASS_PASS29",
        "RENDERPASS_PASS30",
};
static_assert(
    sizeof(g_strRenderPassName) / sizeof(g_strRenderPassName[0]) == RENDERPASS_COUNT,
    "EKG3DRenderPass g_strRenderPassName must have same size"
);

static const char* g_KG3DRenderPassName[] =
    {
        "EARLYZ",
        "Color",
        "ShockWave",
        "ColorSoftMask",
        "Preview",
        "tecEarlyZInst",
        "tecColorInst",
        "SSSDiffuse",
        "SSSBlur",
        "SSSCombine",
        "OITColor0",
        "OITColor1",
        "OITColor0Inst",
        "TerrainHISumAlpha",
        "TerainHIBlend",
        "TerrainHIFinal_3X3",
        "TerrainHIFinal_5X5",
        "TerrainLowFinal",
        "TextureCopy",
        "RENDERPASS_GBUFFER_WRITING",
        "DeferredPointLight",
        "DeferredSpotLight",
        "DeferredDirectionalLight",
        "DeferredSunLight",

        "DepthNoPS",

        "RasterizationSW",
        "RasterizationHW",
        "RasterizationHW_RTVOutput",

        "Pass0",
        "Pass1",
        "Pass2",
        "Pass3",
        "Pass4",
        "Pass5",
        "Pass6",
        "Pass7",
        "Pass8",
        "Pass9",
        "Pass10",
        "Pass11",
        "Pass12",
        "Pass13",
        "Pass14",
        "Pass15",
        "Pass16",
        "Pass17",
        "Pass18",
        "Pass19",
        "Pass20",
        "Pass21",
        "Pass22",
        "Pass23",
        "Pass24",
        "Pass25",
        "Pass26",
        "Pass27",
        "Pass28",
        "Pass29",
        "Pass30",
};

static_assert(
    sizeof(g_KG3DRenderPassName) / sizeof(g_KG3DRenderPassName[0]) == RENDERPASS_COUNT,
    "EKG3DRenderPass g_strRenderPassName must have same size"
);

EKG3DRenderPass GetPassId(const std::string& szName);

enum UniformType
{
    UT_INT,
    UT_BOOL,
    UT_SAMPLER2D,
    UT_SAMPLER_CUBEMAP,
    UT_SAMPLER_2DARRAY,

    UT_FLOAT,
    UT_FLOAT2,
    UT_FLOAT3,
    UT_FLOAT4,

    UT_MATRIX4X4,
    UT_UNKNOWN
};

struct UniformDef
{
    UniformType  type;
    unsigned int uArraySize;
    unsigned int uIndex;
    const char*  name;

    UniformDef()
    {
        type       = UT_UNKNOWN;
        uArraySize = 0;
        uIndex     = 0;
        name       = nullptr;
    }
};

class IKTexture;

#endif
