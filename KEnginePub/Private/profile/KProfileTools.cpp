#include "KEnginePub/Public/KProfileTools.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "KEnginePub/Private/vulkan/GFXVulkan.h"
#include "KEnginePub/Private/vulkan/KVulkanRenderContext.h"
#include "KEnginePub/Private/IGFX_Private.h"
#include "vulkan/vulkan.h"

#if MICROPROFILE_ENABLED
#include "KEnginePub/Private/profile/microprofile/microprofile.h"
#pragma push_macro("_aligned_malloc")
#pragma push_macro("_aligned_free")
#pragma push_macro("malloc")
#pragma push_macro("free")

#undef _aligned_malloc
#undef _aligned_free
#undef malloc
#undef free

#include "KEnginePub/Private/profile/microprofile/microprofile.cpp"

#pragma pop_macro("_aligned_malloc")
#pragma pop_macro("_aligned_free")
#pragma pop_macro("malloc")
#pragma pop_macro("free")
#endif


namespace gfx
{
	GpuProfileScop::GpuProfileScop(IKGFX_RenderContext* pRenderCtx, const char* strName, bool bOptick)
	{
        m_strName = strName;
        m_pRenderCtx = pRenderCtx;

        if (pRenderCtx)
        {
            pRenderCtx->BeginDebugLabel(m_strName);
            pRenderCtx->BeginOptickProfile();
            return;
        }

		if (pRenderCtx)
		{
			switch (DrvOption::GetRenderApi())
			{
            case GFX_API::GFX_VULKAN_API:
			{
			}
			break;
			case GFX_API::GFX_DX12_API:
			{
			}
			break;
			case GFX_API::GFX_METAL_API:
			{
			}
			break;
			default:
				break;
			}
		}
		ASSERT(FALSE);
	}

	GpuProfileScop::~GpuProfileScop()
	{
        if (m_pRenderCtx)
        {
            m_pRenderCtx->EndDebugLabel();
            m_pRenderCtx->EndOptickProfile();

            //KGPUTimestamps* pGPUTimer = dynamic_cast<KGPUTimestamps*>(GetGPUTimer());
            //pGPUTimer->GetTimeStamp(m_pCommandBuffer, nullptr, m_strName);

            return;
        }

		switch (DrvOption::GetRenderApi())
		{
		case GFX_API::GFX_VULKAN_API:
		{
		}
		break;
		case GFX_API::GFX_DX12_API:
		{
		}
		break;
		case GFX_API::GFX_METAL_API:
		{
		}
		break;
		default:
			break;
		}
		ASSERT(FALSE);
	}
}

#if MICROPROFILE_ENABLED
namespace KMicroProfile
{
	KMicroProfileVulkanFunctions GPUProfilerVulkan::s_VulkanFunctions{};
	MicroProfileThreadLogGpu* GPUProfilerVulkan::s_pGpuLog = nullptr;

	GPUProfilerVulkan::GPUProfilerVulkan(gfx::IKGFX_RenderContext* pRenderCtx, uint64_t uToken)
	{
		m_pCommandBuffer = dynamic_cast<gfx::KRenderContextVK*>(pRenderCtx)->GetVulkanCommandBuffer();
		if (m_pCommandBuffer->m_eLifecycleState == gfx::KCommandBufferStates::Recording)
		{
			 //m_ScopeVulkan = new MicroProfileScopeGpuHandler_Vulkan(uToken, m_pGpuLog);
			m_Scope = new MicroProfileScopeGpuHandler(uToken, s_pGpuLog);
			//m_ScopeVulkan = new MicroProfileScopeGpuHandler_Vulkan(m_Token, MicroProfileGetGlobalGpuThreadLog());
		}
	}

	GPUProfilerVulkan::~GPUProfilerVulkan()
	{
		SAFE_DELETE(m_ScopeVulkan);
		SAFE_DELETE(m_Scope);
	}

	void GPUProfilerVulkan::Init(VkDevice* devices, VkPhysicalDevice* physicalDevices, VkQueue* cmdQueues, uint32_t* cmdQueuesFamily, uint32_t nodeCount, const KMicroProfileVulkanFunctions* functions)
	{
		switch (DrvOption::GetRenderApi())
		{
		case GFX_VULKAN_API:
		{
			ASSERT(functions);
			s_VulkanFunctions = *functions;
			MicroProfileGpuInitVulkan(devices, physicalDevices, cmdQueues, cmdQueuesFamily, nodeCount, functions);
		}
		break;
		case GFX_DX12_API:
		{

		}
		break;
		default:
			ASSERT(FALSE);
			break;
		}
	}

	void GPUProfilerVulkan::GPUShutdown()
	{
		switch (DrvOption::GetRenderApi())
		{
		case GFX_VULKAN_API:
		{
			MicroProfileGpuShutdown_Vulkan();
			//MicroProfileShutdown();
		}
		break;
		case GFX_DX12_API:
		{

		}
		break;
		default:
			ASSERT(FALSE);
			break;
		}
	}

	void GPUProfilerVulkan::Flip(gfx::IKGFX_RenderContext* pCommand)
	{
		switch (DrvOption::GetRenderApi())
		{
		case GFX_VULKAN_API:
		{
			gfx::KVulkanCommandBuffer* pKVulkanCommandBuffer = dynamic_cast<gfx::KVulkanCommandBuffer*>(pCommand);
            ASSERT(pKVulkanCommandBuffer);
            VkCommandBuffer vkCommandBuffer = pKVulkanCommandBuffer->GetCommandBuffer();
			//MicroProfileFlip_Vulkan(vkCommandBuffer);
			MicroProfileFlip(vkCommandBuffer);
		}
		break;
		case GFX_DX12_API:
		{

		}
		break;
		default:
			ASSERT(FALSE);
			break;
		}
	}

	void GPUProfilerVulkan::EnableAllGroup()
	{
		MicroProfileSetEnableAllGroups(true);
	}

	uint64_t GPUProfilerVulkan::GetToken(const char* name)
	{
		return MicroProfileGetToken("GPU", name, 0, MicroProfileTokenTypeGpu);;
	}

	void GPUProfilerVulkan::MicroProfileGPUBegin(gfx::IKGFX_RenderContext* pCommand)
	{
		gfx::KVulkanCommandBuffer* pVKCommandBuffer = dynamic_cast<gfx::KVulkanCommandBuffer*>(pCommand);
		ASSERT(pVKCommandBuffer);
		MicroProfileThreadLogGpu* pLog = MicroProfileThreadLogGpuAlloc();
		s_pGpuLog = pLog;
		MICROPROFILE_GPU_BEGIN(pVKCommandBuffer->GetCommandBuffer(), s_pGpuLog);
	}

	void GPUProfilerVulkan::MicroProfileGPUEnd()
	{
		uint64_t uNumLogs = MICROPROFILE_GPU_END(s_pGpuLog);
		MicroProfileThreadLogGpuFree(s_pGpuLog);
		s_pGpuLog = nullptr;

		MICROPROFILE_GPU_SUBMIT(MicroProfileGetGlobalGpuQueue(), uNumLogs);
	}

	CPUMicroProfile::CPUMicroProfile(uint64_t uToken)
	{
		//if (!IsMainThread() && !IsLogicThread())
		{
			m_Scope = new MicroProfileScopeHandler(uToken);
		}
		
	}

	CPUMicroProfile::~CPUMicroProfile()
	{
		SAFE_DELETE(m_Scope);
	}

	uint64_t CPUMicroProfile::GetToken(const char* funcName, const char* name /*= nullptr*/)
	{
		const char* szName = name ? name : funcName;

		return MicroProfileGetToken("CPU", szName, 0, MicroProfileTokenTypeCpu);
	}

	void MicroProfile::Shutdown()
	{
		MicroProfileShutdown();
	}

	void MicroProfile::OnThreadCreate(const char* name)
	{
		MicroProfileOnThreadCreate(name);
	}
}
#endif
