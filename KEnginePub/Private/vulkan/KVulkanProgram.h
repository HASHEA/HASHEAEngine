#pragma once
#include <array>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "../IGFX_Private.h"
#include "KVulkanFunc.h"
#include "KBase/Public/KG3D_Base/KG3D_Vector.h"
#include "KVulkanTexture.h"
//////////////////////////////////////////////////////////////////////////
#include "optick.h"
#include "KVulkanDefine.h"
#include "KVulkanPrivate.h"
#include <KMemory/Public/KAutoRefPtr.h>
#include "robin_hood.h"

class KShaderResourceVK;

namespace gfx
{
    class KVulkanBuffer;

    class KGFX_ProgramBinderVK : public IKGFX_ProgramBinder
    {
        friend class KGFX_GraphicsProgramVK;
        friend class KGFX_ComputeProgramVK;

        struct KSamplerItem
        {
            const_pool_str pName;
            IKGFX_Sampler* pSampler = nullptr;
        };

        enum class BindType : uint32_t
        {
            SHARE_UBO_BIND_TYPE,
            UBO_BIND_TYPE,
            SSBO_BIND_TYPE,
            ACCELERATION_STRUCTURE_TYPE,
            // SAMPLER_BIND_TYPE,
            // COMBINDE_SAMPLER_TYPE,

            RWBUFFERVIEW_TYPE,
            SAMPLEBUFFERVIEW_TYPE,

            TEXTURE_SRV_TYPE,
            TEXTURE_UAV_TYPE,

            TEXTURE_SRV_ARRAY_TYPE, // 单寄存器的TextureUAV对象数组
            TEXTURE_UAV_ARRAY_TYPE, // 单寄存器的TextureSRV对象数组

            // 标量值类型,暂不考虑数组传值
            SCALAR_VALUE_COMP1_FLOAT,
            SCALAR_VALUE_COMP1_INT,
            SCALAR_VALUE_COMP1_UINT,

            SCALAR_VALUE_COMP2_FLOAT,
            SCALAR_VALUE_COMP2_INT,
            SCALAR_VALUE_COMP2_UINT,

            SCALAR_VALUE_COMP3_FLOAT,
            SCALAR_VALUE_COMP3_INT,
            SCALAR_VALUE_COMP3_UINT,

            SCALAR_VALUE_COMP4_FLOAT,
            SCALAR_VALUE_COMP4_INT,
            SCALAR_VALUE_COMP4_UINT,

            CONST_VALUE_TYPE
        };

        struct BindItem
        {
            BindType bindtype;
            const_pool_str pName;

            BindItem() {}
            ~BindItem() {}

            struct AccelerationStructureObject
            {
                gfx::KRayTracingScene* pAS = nullptr;
                const char* pcszBlockName = nullptr;
            };
            struct ShareUBO
            {
                gfx::KVulkanBuffer* pUBO{ nullptr };
                uint32_t            uSize{ 0 };
                uint32_t            uOffset{ 0 };
                const char*         pcszBlockName{ nullptr };
            };

            struct UBO
            {
                gfx::KVulkanBuffer* pUBO;
                const char*         pcszBlockName;
            };

            struct SSBO
            {
                gfx::KVulkanBuffer* pSSBO;
                uint32_t            uByteOffset;
                uint32_t            uByteSize;
                const char*         pcszBlockName = nullptr;
                BOOL                bUAV = true;  //false means uav;
            };

            struct RWBufferView
            {
                IKGFX_BufferView*   pBufView = nullptr;
            };

            struct SampleBufferView
            {
                IKGFX_BufferView*   pBufView = nullptr;
            };

            struct ScalarValueComponent1
            {
                union {
                    float           fValue;
                    int32_t         iValue;
                    uint32_t        uValue;
                };
            };

            struct ScalarValueComponent2
            {
                union {
                    NSKMath::KVec2  fVec2;
                    NSKMath::KInt2  iVec2;
                    NSKMath::KUint2 uVec2;
                };
            };

            struct ScalarValueComponent3
            {
                union {
                    NSKMath::KVec3  fVec3;
                    NSKMath::KInt3  iVec3;
                    NSKMath::KUint3 uVec3;
                };
            };

            struct ScalarValueComponent4
            {
                union {
                    NSKMath::KVec4  fVec4;
                    NSKMath::KInt4  iVec4;
                    NSKMath::KUint4 uVec4;
                };
            };

            struct TextureSRV
            {
                gfx::IKGFX_TextureView* pSRV = nullptr;
            };

            struct TextureUAV
            {
                gfx::IKGFX_TextureView* pUAV = nullptr;
            };

            struct TextureSRVArray
            {
                int      uSlotsOffset = -1;
                uint32_t uNum = 0;
            };

            struct TextureUAVArray
            {
                int      uSlotsOffset = -1;
                uint32_t uNum = 0;
            };

            union {
                ShareUBO              shareUBO;
                UBO                   ubo;
                SSBO                  ssbo;
                RWBufferView          rw_buffer_view;
                SampleBufferView      sample_buffer_view;
                TextureSRV            texture_srv;
                TextureUAV            texture_uav;
                TextureSRVArray       texture_srv_array;
                TextureUAVArray       texture_uav_array;
                ScalarValueComponent1 scalarComp1;
                ScalarValueComponent2 scalarComp2;
                ScalarValueComponent3 scalarComp3;
                ScalarValueComponent4 scalarComp4;
                AccelerationStructureObject aso;
            };
        };

    public:
        KGFX_ProgramBinderVK();
        virtual ~KGFX_ProgramBinderVK();

        IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;
        BOOL EndBind() override;

        IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView) override;
        IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView) override;
        IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufView) override;
        IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufView) override;

        // 当前接口用于绑定单寄存器的BufferUAV对象数组
        IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews) override;

        // 当前接口用于绑定单寄存器的BufferUAV对象数组
        IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews) override;

        // 当前接口用于绑定单寄存器的TextureUAV对象数组
        IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) override;

        // 当前接口用于绑定单寄存器的TextureSRV对象数组
        IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) override;

        IKGFX_ProgramBinder& SetImmutableConstValueInt(const_pool_str pName, int32_t value) override;
        IKGFX_ProgramBinder& SetImmutableConstValueUInt(const_pool_str pName, uint32_t value) override;
        IKGFX_ProgramBinder& SetImmutableConstValueFloat(const_pool_str pName, float value) override;
        IKGFX_ProgramBinder& AddBindCBV(const char* pcszName, IKGFX_BufferView* pBufView) override;
        IKGFX_ProgramBinder& AddBindSampler(const_pool_str pcszName, gfx::IKGFX_Sampler* pSampler) override;
        IKGFX_ProgramBinder& AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* accelerationStructure) override;

        BOOL IsTextureBinded(const_pool_str pName) override;
        void SwapBinderData(KGFX_ProgramBinderVK* pBinder);
        BOOL IsBinding() override;
        BOOL IsDynamicBlock(const_pool_str szName);

    protected:
        BOOL ComputeBindCode();
        int  AllocTextureArrayBindingSlots(uint32_t uNum);
        void ClearTextureArrayBindingSlots();

        BOOL SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize) override;
        BOOL UpdateMtlData() override;
        TextureType GetTextureType(const_pool_str szName) override;

    protected:
        KShaderResourceVK* m_pShaderResource{ nullptr };

    private:
        bool                                                    m_bBinding{ false };
        bool                                                    m_bBindError = false;
        std::vector<BindItem>                                   m_vecBindItem;
        //std::unordered_set<const_pool_str>                      m_setBindedTextureRegisterName;
        robin_hood::unordered_map<const_pool_str, bool>                m_mapBindedTextureRegisterName;

        std::vector<KSpecializationConstItem>                   m_vecBindSpecializationConstItem;
        robin_hood::unordered_map<const_pool_str, gfx::IKGFX_Buffer*>  m_mapUbo;
        uint64_t                                                m_uBindCheckCode = 0;
        robin_hood::unordered_map<const_pool_str, KSamplerItem>        m_vecBindSamplerState;
        KG3D_Vector<IKGFX_TextureView*, 1>                      m_vecTextureViewArraySlots;
        NSEngine::KGlobalUBO*                                        m_pGlobaUBO = nullptr;

        uint32_t                                                m_uSceneRenderID;
        const gfx::KSharedPreBinder*                            m_pSharedPreBinder = nullptr;
        std::vector<gfx::KGfxBarrier>                           m_Barriers = {};

        robin_hood::unordered_map<uint64_t, gfx::KVulkanDescriptorSet*> m_mapDescriptorSet;
        gfx::KVulkanDescriptorSet*                              m_pDescriptorSet{ nullptr };
        std::vector<const_pool_str>                             m_vecBindedUniformBlock;
        std::vector<const_pool_str>                             m_vecBindedTexture;

        IKGFX_Buffer*                                           m_pMtlBuffer;
        uint32_t                                                m_uMtlCPUBufferSize;
        uint8_t*                                                m_pMtlCPUBuffer;
        BOOL                                                    m_bMtlCPUBufferValueChanged;
    };

#define KGFX_ProgramVSFSVK_MEM_LEAK 0
    class KGFX_GraphicsProgramVK : public IKGFX_GraphicsProgram
    {
    public:
        KGFX_GraphicsProgramVK();
        virtual ~KGFX_GraphicsProgramVK();
        BOOL LoadGraphicsShader(
            const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc,
            const char* szShaderDef, const char* szMacro,
            BOOL bReCreate, BOOL bByBuildToolCmd = false, int nPlatform = 0,
            KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD
        ) override;
        BOOL LoadFromFile() override;
        BOOL PostLoad() override;
        BOOL IsLoaded() override;
        BOOL IsLoadFailed() override;
        BOOL IsLoading() override;
        void                   ClearVertDesc() override;
        IKGFX_GraphicsProgram& BeginVertDesc() override;
        IKGFX_GraphicsProgram& AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate) override;
        IKGFX_GraphicsProgram& AddAttribute(gfx::KVertexDecl* pDesc, uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset) override;
        BOOL                   EndVertDesc() override;
        BOOL                   BindVertAttr(gfx::KVertexDecl* pDecls[], uint32_t uDeclCount) override;


        BOOL ApplyRenderState(const std::function<void(gfx::KRenderState*)>& fnRenderStateDefineCall, gfx::KRenderState* pRenderState = nullptr) override;
        BOOL IsReady() override;
        BOOL SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize) override;

        BOOL                      PreparePipeline() override;
        BOOL                      ApplyPipeline() override;
        BOOL                      UpdateMtlData() override;
        gfx::IKGFX_ProgramBinder* GetProgramBinder() override;
        IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;
        BOOL                      EndBind() override;
        void                      SwapBindData(IKGFX_GraphicsProgram* pProgram) override;
        BOOL                      IsTextureBinded(const_pool_str pName) override;
        const gfx::KRenderState& GetSrcRenderState() override;
        gfx::KRenderState* GetRenderState() override;

        BOOL IsBeginBind() override;
        void SetBeginBind(BOOL bBeginBind) override;
        BOOL IsActiveBlock(const_pool_str pcszName) override;
        void AddSamplerState(const char* pName, gfx::KSamplerState& samplerState) override;
        BOOL SetConstDataBlock(uint32_t uSize, void* pData) override;
        void GetUserShaderDetail(int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask) override;
        uint32_t GetCurrentPipelineCode() override;
    private:
        BOOL                    m_bVertDescriptorAutoCreated;
        BOOL                    m_bPostLoaded;
        gfx::KVulkanVertexDescriptor* m_pVertDescriptor;
        KRenderState            m_RenderState;
        KRenderState            m_RenderStateTo;

        BOOL m_bCreatingVertScriptor = false;

        const gfx::RenderCommonParam* m_pCurrentCommonParam = nullptr;
        struct _pipeline
        {
            KRenderState         renderState;
            gfx::KVulkanRenderPass* m_pRenderPass;
            gfx::KPipeline* m_pPipline;
            uint64_t             uSpecializationConstHash = 0;
            uint64_t             uVertDescriptorAttributeHash = 0;
        };
        gfx::KPipeline* m_pCurrentPipeline = nullptr;
        std::vector<_pipeline> m_vecPipline;
        KGFX_ProgramBinderVK* m_pProgramBinder;
#if KGFX_ProgramVSFSVK_MEM_LEAK
        char* m_pMemLeak = nullptr;
#endif

        gfx::IKGFX_Program::KGFX_ProgramLoadParam m_LoadParam;
        std::atomic<BOOL>                                 m_bLoaded;
        std::map<const_pool_str, gfx::KSamplerState>         m_mapSamplerState;
    };

    class KGFX_ComputeProgramVK : public IKGFX_ComputeProgram
    {
    public:
        KGFX_ComputeProgramVK();
        virtual ~KGFX_ComputeProgramVK();

        virtual BOOL LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd = false, int nPlatform = 0, KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD) override;

        IKGFX_ProgramBinder& BeginBind() override;
        IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;
        BOOL                 EndBind() override;
        void                 SetBeginBind(BOOL bBeginBind) override;
        virtual BOOL         Apply(IKGFX_RenderContext* pRenderCtx) override;

        virtual BOOL         SetConstDataBlock(gfx::IKGFX_RenderContext* pRenderCtx, uint32_t uSize, void* pData) override;
        BOOL                 IsReady() override;
        IKGFX_ProgramBinder* GetProgramBinder() override;
        BOOL LoadFromFile() override;
        BOOL IsLoaded() override;
        BOOL IsLoadFailed() override;
        BOOL IsLoading() override;

        uint32_t GetCurrentPipelineCode() override;
    private:
        uint32_t m_uPushConstDataSize{ 0 };

        struct _pipeline
        {
            gfx::KPipeline* m_pPipline;
            uint64_t        uSpecializationConstHash = 0;
        };
        std::vector<_pipeline> m_vecPipline;
        KGFX_ProgramBinderVK* m_pProgramBinder;
        std::atomic<BOOL> m_bLoaded;
        IKGFX_Program::KGFX_ProgramLoadParam m_LoadParam = {};
        uint32_t m_uPipelineCode = 0;
    };
}
