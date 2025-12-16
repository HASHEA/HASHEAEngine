#pragma once

#include <vector>
#include <unordered_map>
#include <atomic>
#include "KBase/Public/KBasePub.h"
#include "KEnginePub/Public/IKShaderVK.h"
#include "../IGFX_Private.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/io/KByteStream.h"
#include <unordered_set>

#include "Engine/KGLog.h"
#include "../vulkan/KGraphicDevice.h"
#include "../../Public/IKGFX_LoadThread.h"
#include "robin_hood.h"

#define ENABLE_BIND_RTT_ID           1
#define ExternalRenderPassPipeLineGC false
#define MAX_SHADER_STAGE_COUNT       5
namespace gfx
{
    class IKSpecializationConstantContainer;
    class KGFX_CombinedShaderResultVK_HLSL;
    class KGFX_ProgramBinderVK;
} // namespace gfx

enum class enumPipelineLoadState
{
    PIPLE_PROCESSING,
    PIPLE_CREATE_SUCCESS,
    PIPLE_CREATE_FAILED
};

struct PipeLine:public IKPipeLineData
{
    gfx::KVulkanVertexDescriptor*           pVertDescriptor;
    gfx::KRenderState                       renderState;
    gfx::KVulkanRenderPass*                 pRenderPass;
    gfx::KPipeline*                         pPipeline;
    std::atomic<enumPipelineLoadState>      processState;
    int32_t                                 nFrameCountLastQuery;
    uint64_t                                uSpecializationConstItemHash;
    void                                    CreateSpecializationConstantContainer();
    gfx::IKSpecializationConstantContainer* GetSpecializationConstantContainer();
    gfx::IKSpecializationConstantContainer* m_pSpeicalizationConstantContainer;
    PipeLine();
    ~PipeLine();
};

enum class KEnumMtlTaskLevel;


struct KVsTmpData
{
    std::string iopath;
    std::string szShaderName;
    std::string vsString;
    uint32_t    stage;
    uint32_t    shaderType;
    uint32_t    shaderTypeBit;
};


class KShaderResourceVK : public gfx::IShaderReflector
{
public:
    KShaderResourceVK();
    virtual ~KShaderResourceVK();

    virtual int32_t AddRef();
    virtual int32_t Release();
    virtual int32_t GetRef();

    BOOL LoadFromFileVSFS(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, BOOL bByBuildToolCmd = false, int nPlatform = 0);
    BOOL LoadFromFileCS(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, BOOL bByBuildToolCmd = false, int nPlatform = 0);

    BOOL IsLoaded();
    BOOL Create();
    BOOL IsCreated();
    BOOL IsOrphan();

    BOOL                             IsReflected(gfx::ShaderStageType shaderType) override;
    // BOOL BuildReflection(void* pProgram, gfx::ShaderStageType shaderType) override;
    BOOL                             BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType) override;
    gfx::IKGFX_CombinedShaderResult* GetCombindShaderResult() override;
    void                             DebugPrintSaveReflectorBegin(const char* pFileName) override;
    void                             DebugPrintSaveReflectorEnd(const char* pFileName) override;
    BOOL                             IsEnableSpirvCrossReflector() override;
    uint32_t                         GetPushContentAlign16BytesBlockSize() override;

    uint32_t GetVsPushConstantSize() override;
    void     SetVsPushContantSize(uint32_t uVsPushContantSize) override;

    int32_t GetMaxBinding() override
    {
        return m_nMaxBinding;
    }

    BOOL RequestVertDescriptor(gfx::KVertexDecl* pDecls[], uint32_t uStreamCount, gfx::KVulkanVertexDescriptor** pDescriptor);
    BOOL RequestPipeline(const gfx::KRenderState& renderstate, const gfx::KVulkanRenderPass* pExternalRenderPass, gfx::KVulkanVertexDescriptor* pVertDescriptor, gfx::KPipeline** ppPipeline, KEnumMtlTaskLevel uThreadLevel, gfx::KSpecializationConstItem* pItems, uint32_t uItemCount, uint64_t uSpecializationConstItemHash);

    BOOL RequestPipeline(const gfx::KRenderState& sRenderstate, gfx::KVulkanRenderPass* pRenderPass, gfx::KVulkanVertexDescriptor* pVertDescriptor, gfx::KPipeline** ppPipeline, KEnumMtlTaskLevel uThreadLevel, gfx::KSpecializationConstItem* pItems, uint32_t uItemCount, uint64_t uSpecializationConstItemHash);

    uint64_t      GetHashCode();
    void          SetHashCode(uint64_t uHash);
    BOOL          IsGpuInstance();
    gfx::KVulkanLayout* GetLayout();

    BOOL ApplyPushConstDataDirectly(gfx::IKGFX_RenderContext* pRenderCtx, gfx::ShaderStageType shaderType, uint32_t uSize, void* pData);

    BOOL     UpdateShader(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludedShaderLoc, const char* szShaderDef, const char* szMacro);
    uint32_t GetUpdateCheckCode();

    BOOL        HasVsShaderTmpData() override;
    void        SetupVsShaderTmpData(const char* iopath, const char* szShaderName, const char* pString, uint32_t stage, uint32_t shadertype, uint32_t shaderTypeBit) override;
    const char* GetVsTmp_iopath() override;
    const char* GetVsTmp_szShaderName() override;
    const char* GetVsTmp_vsString() override;
    uint32_t    GetVsTmp_stage() override;
    uint32_t    GetVsTmp_shaderType() override;
    uint32_t    GetVsTmp_shaderTypeBit() override;
    void        ClearVsTmpData() override;
    BOOL        FindBindingForFixShaderContent(const char* szBlockName, int32_t& binding) override;
    void        InsertBindingForFixShaderContent(const char* szBlockName, int32_t binding) override;

    void                     SetupVsShaderTmpData(uint32_t* pVsSpirv, uint32_t uIntCount) override;
    uint32_t*                GetVsTmpSpirv() override;
    uint32_t                 GetVsShaderTmpSpirvUintCount() override;
    BOOL                     HasPerMtlUBO();
    std::string              m_strShaderSource;
    NSKBase::tagFileLocation m_sIncludedShaderLoc;
    std::string              m_strShaderDef;
    std::string              m_strMacro;
    uint32_t                 m_uUpdateCheckCode;
    BOOL                     IsActiveBlock(const_pool_str pcszName);
    void                     AddSamplerState(const_pool_str pName, gfx::KSamplerState& samplerState);

public:
    void SetForceStaticUBO(BOOL bStatic) { m_bForceStaticUBO = bStatic; }
    BOOL IsForceStaticUBO() { return m_bForceStaticUBO; }
    BOOL SetupLayout(gfx::KGFX_ProgramBinderVK* pBinderVK);


    BOOL     SetupLayout(); // 老接口，要删除的
    uint32_t GetId();

    gfx::KShaderStage* GetVSStage();
    gfx::KShaderStage* GetFsStage();

    gfx::KSamplerState* GetSamplerState(const char* pSamplerName);

    void SetShaderFileName(const char* pName, gfx::ShaderStageType shaderType) override
    {
        uint32_t shaderid          = GetGraphicAndComputeShaderId(shaderType);
        m_ShaderFileName[shaderid] = pName;
    }
    const char* GetShaderFileName(gfx::ShaderStageType shaderType) override
    {
        uint32_t shaderid = GetGraphicAndComputeShaderId(shaderType);
        return m_ShaderFileName[shaderid].c_str();
    }
    BOOL                           IsLogShader() override;
    gfx::KDescriptorPoolContainer* GetDescriptorPoolContainer();

    void AddSpecializationConstDefine(uint32_t uStageType, uint32_t const_id, const char* pName) override;

private:
    std::atomic<int32_t> m_nRef;
    std::atomic<BOOL>    m_bLoaded;
    std::atomic<BOOL>    m_bCreated;
    std::atomic<BOOL>    m_bOrphan;
    BOOL                 m_bForceStaticUBO{FALSE};

public:
    std::vector<gfx::KProgramAttribute*>    m_vecAttribute;
    // UBO blocks
    std::vector<gfx::KProgramUniformBlock*> m_vecUniformBlock;
    std::unordered_set<const_pool_str>      m_setActiveBlock;

    // push_contants block only one for a shader
    gfx::KProgramUniformBlock* m_pPushConstantBlock;

    std::vector<gfx::KProgramUniformTexture*> m_vecUniformTexture;
    // samplers
    std::vector<gfx::KProgramUniformSampler*> m_vecUniformSampler;

    std::vector<gfx::KSpecializationConstDefine> m_vecSpecializationConstDefine;

    robin_hood::unordered_flat_map<const_param_name, gfx::KProgramUniform*> m_mapMtlParamItem;

private:
    void Clear();

private:
    struct VertDescriptor
    {
        gfx::KVulkanVertexDescriptor* pVertDescriptor;
        // these are global vertices declares, so store the pointer is safety
        std::vector<gfx::KVertexDecl*> m_vecDecls;
        VertDescriptor();
        ~VertDescriptor();
    };
    std::vector<VertDescriptor*> m_vecVertDescriptorCache;
    std::vector<PipeLine*>       m_vecPipelineCache;
    std::vector<PipeLine*>       m_vecPipelineCache_ExternalPass;

#if ExternalRenderPassPipeLineGC
    int m_nLastGCFrameCount;
#endif

public:
    gfx::KShaderStage* m_pShaderStage[MAX_SHADER_STAGE_COUNT];
    uint32_t           m_uStageCount;

private:
    gfx::KDescriptorPoolContainer m_DescriptorPoolContainer;
    gfx::KVulkanLayout*                 m_pLayout;
    uint64_t                      m_uHashCode;
    BOOL                          m_bGpuInstance;
    uint32_t                      m_id;
    BOOL                          m_bParamInited;
    BOOL                          m_buildedReflect0    = false;
    BOOL                          m_buildedReflect1    = false;
    BOOL                          m_bLoadCachedReflect = false;
    int32_t                       m_nMaxBinding;
    
    uint32_t                       m_vsPushConstantSize;
    std::string                    m_ShaderFileName[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];
    KVsTmpData*                    m_pVsTmpData;
    std::map<std::string, int32_t> m_blockNameBindingMapForFixShaderContent;
    std::atomic<BOOL>              m_bRefelcted[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];

    uint32_t*                              m_pTempVsSpirv;
    uint32_t                               m_uTempVsSpirvIntCount;
    // std::vector<uint32_t> m_vecActiveUBORangeId;
    gfx::KGFX_CombinedShaderResultVK_HLSL* m_pCombinedShaderResultVK_HLSL;
    BOOL                                   m_bHasPerMtlUBO;

    std::map<const_pool_str, gfx::KSamplerState> m_mapSamplerState;

public:
    std::mutex m_lock;
};

class KPipelineLoadThread : public IKGFX_PipelineLoadThread
{
    struct _Task
    {
        KShaderResourceVK* pShaderResource;
        PipeLine* pPipeLine;
        gfx::GraphicsPipelineDesc graphicDesc;

        int      nPriority;
        uint32_t uID;
    };

    struct _GreaterCmp
    {
        bool operator()(const _Task& left, const _Task& right)
        {
            if (left.nPriority == right.nPriority)
                return left.uID > right.uID;
            return left.nPriority < right.nPriority;
        }
    };

public:
    void EngineThreadCall() override;
    KPipelineLoadThread();
    ~KPipelineLoadThread();    
    BOOL IsAllowToPush();
    BOOL PushLoadTask(IKShaderResource* pShaderResource, IKPipeLineData* pPipeLine, IKGraphicsPipelineDesc* pDesc, KEnumMtlTaskLevel uThreadLevel) override;
    void FrameMove() override;

private:
    std::priority_queue<_Task, std::vector<_Task>, _GreaterCmp> m_LoadTask;
    std::mutex                                                  m_lck;
    std::atomic<int32_t>                                        m_nMaxPushCountPerFrame;
};
