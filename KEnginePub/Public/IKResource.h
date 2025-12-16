#ifndef IKRESOURCE_H
#define IKRESOURCE_H

#include "KGBaseDef/Public/core_base_macro.h"
#include <functional>
#include <atomic>
#include "KEnginePub/Public/IKHeader.h"
#include "KEnginePub/Public/IKLoadable.h"
#include "Engine/KUniqueString.h"

enum class EResourceOwnerModelFlag : uint32_t
{
    Unknown          = 0,
    Model            = 1,
    GamePlayModel    = 1 << 1,
    PssModel         = 1 << 2,
    MainPlayerModel  = 1 << 3,
    FoliageModel     = 1 << 4,
    FoliageBillboard = 1 << 5,
};

enum class ELoadOption : uint32_t
{
    // 0x10000开始，低四位预留给 RESOURCE_LOAD_TYPE 使用
    Deprecated               = 0x10000,
    ModelMaterial            = 0x20000,
    PssModelMaterial         = 0x40000,
    FaceModelMaterial        = 0x80000,
    GamePlayModelMaterial    = 0x100000,
    MainPlayerModelMaterial  = 0x200000,
    FoliageModelMaterial     = 0x400000,
    FoliageBillboardMaterial = 0x800000,
};

enum RESOURCE_LOAD_TYPE
{
    // 最大0x1111，高四位预留给 ELoadOption 使用
    RESOURCE_LOAD_IMMEDIATELY             = 0,
    RESOURCE_LOAD_MULTITHREAD             = 0x1,
    RESOURCE_LOAD_IMMEDIATELY_MULTITHREAD = 0x2, // If already in multi thread
    RESOURCE_LOAD_CACHEONLY               = 0x4,
    RESOURCE_LOAD_INVALID                 = 0xffffffff,
};

// 可共用的资源类型
enum KRESOURCETYPE : uint32_t
{
    UNKNOWN_RESOURCE,
    TEXTURE_TYPE,
    TEXTURE_VK_TYPE,
    TEXTURE_RAW_TYPE,
    TEXTURE_MASK_TYPE,
    MATERIAL_TYPE,
    CLIP_TYPE,
    SKELETON_TYPE,
    SOUND_TYPE,
    SPEEDTREE_TYPE,
    SFX_TRACK_TYPE,
    MDL_TYPE,
    SRT_TYPE,
    MEM_TEXTURE_TYPE,
    RENDER_TEXTURE_TYPE,

    MODEL_ASSET,
    MODELINST_ASSET,
    TEXTURESET_ASSET,
    ANIMODEL_ASSET,
    ANIMODELINST_ASSET,
    RUST_ASSET,
    ASSEMBLE_ASSET,
    SKELETON_ASSET,
    CLIP_ASSET,

    // common mesh
    KMESH_TYPE,
    KCOLLISION_MESH_TYPE,
    KAXIS_LINE_MESH_TYPE,
    KGRID_MESH_TYPE,
    KBOX_MESH_TYPE,
    KSPHERE_MESH_TYPE,
    KCAPSULE_MESH_TYPE,
    KRINGQUAD_MESH_TYPE,
    KCYLINDER_MESH_TYPE,
    KTORUS_MESH_TYPE,
    KUI_RECT_MESH_TYPE,
    KCIRCLE_MESH_TYPE,
    KQUAD_XZ_MESH_TYPE,
    KQUAD_XY_MESH_TYPE,
    KAXIS_SCALE_MESH_TYPE,
    KSFX_BILLBORD_MESH_TYPE,
    KSFX_RECTPATICAL_MESH_TYPE,
    KWATER_MESH_TYPE,
    KMOUNTAIN_MESH_TYPE,
    KCLOUD_MESH_TYPE,
    KMESH_TYPE_BOUND_END,

    // vulkan resource
    KAXISMESH_VK_TYPE,
    KGRIDMESH_VK_TYPE,
    SCREEN_RECT_MESH_VK_TYPE,
    XZ_QUAD_MESH_VK_TYPE,
    BOX_MESH_VK_TYPE,

    STANDARD_SCREEN_COLOR_MTL_VK_TYPE,
    STANDARD_ROTATECIRCLE_MTL_VK_TYPE,
    STANDARD_PC_MTL_VK_TYPE,
    STANDARD_SCREEN_RECT_MTL_VK_TYPE,
    STANDARD_OFFSETSCREEN_RECT_MTL_VK_TYPE,
    STANDARD_OFFSETSCREEN_POSTRENDER_RECT_MTL_VK_TYPE,
    STANDARD_GEOM_COLOR_MTL_VK_TYPE,
    PARTICLE_QUOTE_MESH,
    PARTICLE_TYPE,

    RESOURCE_COUNT,
};

inline const char* GetResourceTypeName(KRESOURCETYPE type)
{
    const char* pResourceName = "UNKNOWN_RESOURCE";
    switch (type)
    {
    case UNKNOWN_RESOURCE:
        pResourceName = "UNKNOWN_RESOURCE";
        break;
    case TEXTURE_TYPE:
        pResourceName = "TEXTURE_TYPE";
        break;
    case TEXTURE_VK_TYPE:
        pResourceName = "TEXTURE_VK_TYPE";
        break;
    case TEXTURE_RAW_TYPE:
        pResourceName = "TEXTURE_RAW_TYPE";
        break;
    case TEXTURE_MASK_TYPE:
        pResourceName = "TEXTURE_MASK_TYPE";
        break;
    case MATERIAL_TYPE:
        pResourceName = "MATERIAL_TYPE";
        break;
    case CLIP_TYPE:
        pResourceName = "CLIP_TYPE";
        break;
    case SKELETON_TYPE:
        pResourceName = "SKELETON_TYPE";
        break;
    case SOUND_TYPE:
        pResourceName = "SOUND_TYPE";
        break;
    case SPEEDTREE_TYPE:
        pResourceName = "SPEEDTREE_TYPE";
        break;
    case SFX_TRACK_TYPE:
        pResourceName = "SFX_TRACK_TYPE";
        break;
    case MDL_TYPE:
        pResourceName = "MDL_TYPE";
        break;
    case SRT_TYPE:
        pResourceName = "SRT_TYPE";
        break;
    case MEM_TEXTURE_TYPE:
        pResourceName = "MEM_TEXTURE_TYPE";
        break;
    case RENDER_TEXTURE_TYPE:
        pResourceName = "RENDER_TEXTURE_TYPE";
        break;
    case KMESH_TYPE:
        pResourceName = "KMESH_TYPE";
        break;
    case KAXIS_LINE_MESH_TYPE:
        pResourceName = "KAXIS_LINE_MESH_TYPE";
        break;
    case KGRID_MESH_TYPE:
        pResourceName = "KGRID_MESH_TYPE";
        break;
    case KBOX_MESH_TYPE:
        pResourceName = "KBOX_MESH_TYPE";
        break;
    case KSPHERE_MESH_TYPE:
        pResourceName = "KSPHERE_MESH_TYPE";
        break;
    case KCAPSULE_MESH_TYPE:
        pResourceName = "KCAPSULE_MESH_TYPE";
        break;
    case KRINGQUAD_MESH_TYPE:
        pResourceName = "KRINGQUAD_MESH_TYPE";
        break;
    case KCYLINDER_MESH_TYPE:
        pResourceName = "KCYLINDER_MESH_TYPE";
        break;
    case KTORUS_MESH_TYPE:
        pResourceName = "KTORUS_MESH_TYPE";
        break;
    case KUI_RECT_MESH_TYPE:
        pResourceName = "KUI_RECT_MESH_TYPE";
        break;
    case KCIRCLE_MESH_TYPE:
        pResourceName = "KCIRCLE_MESH_TYPE";
        break;
    case KQUAD_XZ_MESH_TYPE:
        pResourceName = "KQUAD_XZ_MESH_TYPE";
        break;
    case KQUAD_XY_MESH_TYPE:
        pResourceName = "KQUAD_XY_MESH_TYPE";
        break;
    case KAXIS_SCALE_MESH_TYPE:
        pResourceName = "KAXIS_SCALE_MESH_TYPE";
        break;
    case KSFX_BILLBORD_MESH_TYPE:
        pResourceName = "KSFX_BILLBORD_MESH_TYPE";
        break;
    case KSFX_RECTPATICAL_MESH_TYPE:
        pResourceName = "KSFX_RECTPATICAL_MESH_TYPE";
        break;
    case KWATER_MESH_TYPE:
        pResourceName = "KWATER_MESH_TYPE";
        break;
    case KMOUNTAIN_MESH_TYPE:
        pResourceName = "KMOUNTAIN_MESH_TYPE";
        break;
    case KCLOUD_MESH_TYPE:
        pResourceName = "KCLOUD_MESH_TYPE";
        break;
    case KMESH_TYPE_BOUND_END:
        pResourceName = "KMESH_TYPE_BOUND_END";
        break;
    case KAXISMESH_VK_TYPE:
        pResourceName = "KAXISMESH_VK_TYPE";
        break;
    case KGRIDMESH_VK_TYPE:
        pResourceName = "KGRIDMESH_VK_TYPE";
        break;
    case SCREEN_RECT_MESH_VK_TYPE:
        pResourceName = "SCREEN_RECT_MESH_VK_TYPE";
        break;
    case XZ_QUAD_MESH_VK_TYPE:
        pResourceName = "XZ_QUAD_MESH_VK_TYPE";
        break;
    case BOX_MESH_VK_TYPE:
        pResourceName = "BOX_MESH_VK_TYPE";
        break;
    case STANDARD_SCREEN_COLOR_MTL_VK_TYPE:
        pResourceName = "STANDARD_SCREEN_COLOR_MTL_VK_TYPE";
        break;
    case STANDARD_ROTATECIRCLE_MTL_VK_TYPE:
        pResourceName = "STANDARD_ROTATECIRCLE_MTL_VK_TYPE";
        break;
    case STANDARD_PC_MTL_VK_TYPE:
        pResourceName = "STANDARD_PC_MTL_VK_TYPE";
        break;
    case STANDARD_SCREEN_RECT_MTL_VK_TYPE:
        pResourceName = "STANDARD_SCREEN_RECT_MTL_VK_TYPE";
        break;
    case STANDARD_OFFSETSCREEN_RECT_MTL_VK_TYPE:
        pResourceName = "STANDARD_OFFSETSCREEN_RECT_MTL_VK_TYPE";
        break;
    case STANDARD_OFFSETSCREEN_POSTRENDER_RECT_MTL_VK_TYPE:
        pResourceName = "STANDARD_OFFSETSCREEN_POSTRENDER_RECT_MTL_VK_TYPE";
        break;
    case STANDARD_GEOM_COLOR_MTL_VK_TYPE:
        pResourceName = "STANDARD_GEOM_COLOR_MTL_VK_TYPE";
        break;
    case RESOURCE_COUNT:
        break;
    default:
        break;
    }
    return pResourceName;
}

// 资源接口
class IKResource : public IKLoadAble
{
private:
    bool     m_bIsInGC         = false;
    bool     m_bProcedural     = false; // 是否程序贴图
    uint32_t m_uResourceOption = 0;

public:
    void SetIsGC(bool bGC)
    {
        ASSERT(m_bIsInGC != bGC); // 不认为有场景 应该反复设置同一个值
        m_bIsInGC = bGC;
    }
    bool IsInGC() { return m_bIsInGC; }

    void SetIsProcedural(bool bProcedural)
    {
        m_bProcedural = bProcedural;
    }
    bool IsProcedural() { return m_bProcedural; }

    void SetResourceOption(uint32_t uOption)
    {
        m_uResourceOption = uOption;
    }
    uint32_t GetResourceOption() { return m_uResourceOption; }

public:
    virtual ~IKResource() {};

    virtual KUniqueStr    GetResourceName() = 0;
    virtual KRESOURCETYPE GetResourceType() = 0;
    virtual uint64_t      GetResourceSize() = 0;

    virtual float GetGCTime()              = 0;
    virtual void  AddGCTime(float fGCTime) = 0;
    virtual void  ClearGCTime()            = 0;

public:
    virtual KEmuLoadAbleType GetLoadAbleType() override // IKLoadAble
    {
        return LOADABLE_RESOURCE;
    }
    virtual BOOL IsPostLoadInLogicThread() const override
    {
        return TRUE;
    }
};

struct KResourceGCData;

class KResourceGC
{
private:
    KResourceGCData* m_pGCData{nullptr};

public:
    KResourceGC();
    virtual ~KResourceGC();

public:
    void        GC(float fDeltaTime);
    void        DelayDelete(IKResource* pResource);
    IKResource* TryGetOut(KUniqueStr ustrResourceName);
};

#endif
