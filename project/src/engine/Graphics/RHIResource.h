#pragma once
#include "RHICommon.h"
#include "base/hassert.h"
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include <Base/hmemory.h>
#include <memory>
#include <utility>
namespace RHI
{

	class Texture;
	class TextureView;
	class Sampler;
	class Buffer;
	class BufferView;
	class CommandBuffer;
	class RenderPass;
	class Framebuffer;
	class Pipeline;
	class Shader;
	struct RenderPassCreation;
	struct FramebufferCreation;
	struct PipelineCreation;
	struct SamplerCreation;
	struct TextureCreation;
	struct TextureViewCreation;
	struct BufferCreation;
	struct DescriptorSetLayoutCreation;
	struct ShaderCreation;
	class RHIResource 
	{
	public:
		RHIResource() = default;
		virtual ~RHIResource() {};
	public:
		virtual auto get_native_handle() -> void* = 0;
		virtual auto get_name() -> const char* = 0;
	public:
		bool immediate_deletion = false;
	};
	class RHIView : public RHIResource
	{
	public:
		RHIView() = default;
		virtual ~RHIView() {};
	public:
	/*	virtual auto get_shader_resource_view() -> void* = 0;
		virtual auto get_unordered_access_view() -> void* = 0;*/

	};

	enum class AshResourceViewType : uint8_t
	{
		ASH_RESOURCE_VIEW_TYPE_UNKNOWN,
		ASH_RESOURCE_VIEW_TYPE_CBV,
		ASH_RESOURCE_VIEW_TYPE_SRV,
		ASH_RESOURCE_VIEW_TYPE_RTV,
		ASH_RESOURCE_VIEW_TYPE_DSV,
		ASH_RESOURCE_VIEW_TYPE_UAV,
		ASH_RESOURCE_VIEW_TYPE_SAMPLER,
		ASH_RESOURCE_VIEW_TYPE_AS,
	};

	struct AshSubresourceRange
	{
		enum EType : uint32_t
		{
			s_uDepthAspect = 0,
			s_uStencilAspect = 1,
			s_All = UINT32_MAX,
		};

		uint32_t uBaseMipLevel = 0;
		uint32_t uBaseArraySlice = 0;
		uint32_t uMipCount = s_All;
		uint32_t uArrayCount = s_All;

		AshSubresourceRange() = default;

		AshSubresourceRange(
			uint32_t _uMipIndex,
			uint32_t _uArraySlice,
			uint32_t _uMipCount,
			uint32_t _uArrayCount
		)
			: uBaseMipLevel(_uMipIndex)
			, uBaseArraySlice(_uArraySlice)
			, uMipCount(_uMipCount)
			, uArrayCount(_uArrayCount)
		{
		}

		inline bool IsWholeResource() const
		{
			return uBaseMipLevel == 0 && uBaseArraySlice == 0 && uMipCount == s_All && uArrayCount == s_All;
		}

		inline AshSubresourceRange resolve(uint32_t mipLevelCount, uint32_t arrayLayerCount) const
		{
			AshSubresourceRange resolved = *this;
			if (mipLevelCount == 0)
			{
				resolved.uBaseMipLevel = 0;
				resolved.uMipCount = 0;
			}
			else
			{
				resolved.uBaseMipLevel = resolved.uBaseMipLevel < mipLevelCount ? resolved.uBaseMipLevel : (mipLevelCount - 1);
				const uint32_t remainingMips = mipLevelCount - resolved.uBaseMipLevel;
				resolved.uMipCount = resolved.uMipCount == s_All || resolved.uMipCount > remainingMips ? remainingMips : resolved.uMipCount;
			}

			if (arrayLayerCount == 0)
			{
				resolved.uBaseArraySlice = 0;
				resolved.uArrayCount = 0;
			}
			else
			{
				resolved.uBaseArraySlice = resolved.uBaseArraySlice < arrayLayerCount ? resolved.uBaseArraySlice : (arrayLayerCount - 1);
				const uint32_t remainingLayers = arrayLayerCount - resolved.uBaseArraySlice;
				resolved.uArrayCount = resolved.uArrayCount == s_All || resolved.uArrayCount > remainingLayers ? remainingLayers : resolved.uArrayCount;
			}

			return resolved;
		}

		inline bool operator==(AshSubresourceRange const& RHS) const
		{
			return uBaseMipLevel == RHS.uBaseMipLevel && uBaseArraySlice == RHS.uBaseArraySlice && uMipCount == RHS.uMipCount && uArrayCount == RHS.uArrayCount;
		}

		inline bool operator!=(AshSubresourceRange const& RHS) const
		{
			return !(*this == RHS);
		}
	};
	enum class AshResourceState : uint32_t
	{
		Unknown = 0,
		// Read states
		CPURead = 1 << 0,
		Present = 1 << 1,
		IndirectArgs = 1 << 2,
		VertexBuffer = 1 << 3,
		IndexBuffer = 1 << 4,
		ConstBuffer = 1 << 5,
		SRVCompute = 1 << 6,
		SRVGraphicsPixel = 1 << 7,
		SRVGraphicsNonPixel = 1 << 8,
		CopySrc = 1 << 9,
		ResolveSrc = 1 << 10,
		DSVRead = 1 << 11,
		// Read-write states
		UAVCompute = 1 << 12,
		UAVGraphics = 1 << 13,
		RTV = 1 << 14,
		CopyDst = 1 << 15,
		ResolveDst = 1 << 16,
		DSVWrite = 1 << 17,
		// Ray tracing acceleration structure states.
		// Buffer that contains an AS must always be in either of these states.
		// BVHRead -- required for AS inputs to build/update/copy/trace commands.
		// BVHWrite -- required for AS outputs of build/update/copy commands.
		BVHRead = 1 << 18,
		BVHWrite = 1 << 19,
		// Invalid released state (transient resources)
		Discard = 1 << 20,
		// Shading Rate Source
		ShadingRateSource = 1 << 21,
		// Shader Binding Table Read
		SBTRead = 1 << 22,
		Last = ShadingRateSource,
		None = Unknown,
		Mask = (Last << 1) - 1,
		// Graphics is a combination of pixel and non-pixel
		SRVGraphics = SRVGraphicsPixel | SRVGraphicsNonPixel,
		// A mask of the two possible SRV states
		SRVMask = SRVCompute | SRVGraphics,
		// A mask of the two possible UAV states
		UAVMask = UAVCompute | UAVGraphics,
	};
	RHI_ENUM_CLASS_OPERATORS(AshResourceState);
	struct AshBarrier : public AshSubresourceRange
	{
		std::shared_ptr<Texture> pTexture{};
		std::shared_ptr<Buffer> pBuffer{};

		enum class EType : uint8_t
		{
			Unknown,
			Texture,
			Buffer,
		} eType = EType::Unknown;

		AshResourceState eSRCAccess = AshResourceState::Unknown;
		AshResourceState eDSTAccess = AshResourceState::Unknown;

		AshBarrier() = default;
		AshBarrier(const AshBarrier& other) = default;
		AshBarrier& operator=(const AshBarrier& other) = default;
		AshBarrier(AshBarrier&& other) noexcept = default;
		AshBarrier& operator=(AshBarrier&& other) noexcept = default;
		~AshBarrier() = default;
		AshBarrier(
			std::shared_ptr<Texture> InTexture,
			AshResourceState                   InPreviousState,
			AshResourceState                   InNewState
		)
			: AshSubresourceRange()
			, pTexture(InTexture)
			, eType(EType::Texture)
			, eSRCAccess(InPreviousState)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InTexture);
		}
		AshBarrier(
			std::shared_ptr<Texture> InTexture,
			AshResourceState                   InPreviousState,
			AshResourceState                   InNewState,
			AshSubresourceRange& range
		)
			: AshSubresourceRange(range)
			, pTexture(InTexture)
			, eType(EType::Texture)
			, eSRCAccess(InPreviousState)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InTexture);
		}
		AshBarrier(
			std::shared_ptr<Texture> InTexture,
			AshResourceState                   InNewState
		)
			: AshSubresourceRange()
			, pTexture(InTexture)
			, eType(EType::Texture)
			, eSRCAccess(AshResourceState::Unknown)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InTexture);
		}
		AshBarrier(
			std::shared_ptr<Texture> InTexture,
			AshResourceState                   InNewState,
			AshSubresourceRange& range
		)
			: AshSubresourceRange(range)
			, pTexture(InTexture)
			, eType(EType::Texture)
			, eSRCAccess(AshResourceState::Unknown)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InTexture);
		}

		AshBarrier(std::shared_ptr<Buffer> InRHIBuffer, AshResourceState InPreviousState, AshResourceState InNewState)
			: pBuffer(InRHIBuffer)
			, eType(EType::Buffer)
			, eSRCAccess(InPreviousState)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InRHIBuffer);
		}
		AshBarrier(std::shared_ptr<Buffer> InRHIBuffer, AshResourceState InNewState)
			: pBuffer(InRHIBuffer)
			, eType(EType::Buffer)
			, eSRCAccess(AshResourceState::Unknown)
			, eDSTAccess(InNewState)
		{
			H_ASSERT(InRHIBuffer);
		}

		inline bool operator==(AshBarrier const& RHS) const
		{
			return pTexture == RHS.pTexture && pBuffer == RHS.pBuffer && eType == RHS.eType && eSRCAccess == RHS.eSRCAccess && eDSTAccess == RHS.eDSTAccess && AshSubresourceRange::operator==(RHS);
		}

		inline bool operator!=(AshBarrier const& RHS) const
		{
			return !(*this == RHS);
		}
	};
	
}

