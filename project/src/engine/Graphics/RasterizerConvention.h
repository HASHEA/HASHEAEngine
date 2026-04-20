#pragma once
#include "RHICommon.h"

#if defined(ASH_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace RHI
{
	/// Readable alias: engine always reasons about *mesh* winding, not D3D/VK rasterizer literals.
	using MeshExteriorWinding = AshFrontFace;

	// -------------------------------------------------------------------------
	// Mesh winding vs GPU rasterizer state
	//
	// `RasterizationCreation::front` / `AshFrontFace` here means: which triangle winding
	// (CCW or CW) is the *exterior* of the mesh in model/object space — the same idea as
	// glTF "front face" / artist export, NOT the literal D3D12 or Vulkan API flag.
	//
	// Shared HLSL is compiled to:
	//   - DXIL: no clip-space Y inversion.
	//   - SPIR-V (Vulkan): DXC `-fvk-invert-y` negates clip Y, which flips screen-space winding
	//     relative to DXIL for the same draw.
	//
	// These helpers are the *single* place that maps that one engine knob to each API so
	// back-face culling matches between D3D12 and Vulkan without scattering inversions.
	// -------------------------------------------------------------------------

	/// Value for `D3D12_RASTERIZER_DESC::FrontCounterClockwise` (TRUE = screen CCW is front).
	inline constexpr bool mesh_winding_to_d3d12_front_counter_clockwise_bool(AshFrontFace mesh_exterior_winding)
	{
		return mesh_exterior_winding != ASH_FRONT_FACE_COUNTER_CLOCKWISE;
	}

#if defined(ASH_HAS_VULKAN)
	/// `VkPipelineRasterizationStateCreateInfo::frontFace` for pipelines built from HLSL→SPIR-V with `-fvk-invert-y`.
	inline constexpr VkFrontFace mesh_winding_to_vulkan_front_face(AshFrontFace mesh_exterior_winding)
	{
		switch (mesh_exterior_winding)
		{
		case ASH_FRONT_FACE_COUNTER_CLOCKWISE:
			return VK_FRONT_FACE_CLOCKWISE;
		case ASH_FRONT_FACE_CLOCKWISE:
			return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		default:
			return VK_FRONT_FACE_CLOCKWISE;
		}
	}
#endif

	static_assert(!mesh_winding_to_d3d12_front_counter_clockwise_bool(ASH_FRONT_FACE_COUNTER_CLOCKWISE));
	static_assert(mesh_winding_to_d3d12_front_counter_clockwise_bool(ASH_FRONT_FACE_CLOCKWISE));
#if defined(ASH_HAS_VULKAN)
	static_assert(mesh_winding_to_vulkan_front_face(ASH_FRONT_FACE_COUNTER_CLOCKWISE) == VK_FRONT_FACE_CLOCKWISE);
	static_assert(mesh_winding_to_vulkan_front_face(ASH_FRONT_FACE_CLOCKWISE) == VK_FRONT_FACE_COUNTER_CLOCKWISE);
#endif
} // namespace RHI
