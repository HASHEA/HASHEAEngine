#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSurface.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	struct VegetationWorldMillimeterPoint
	{
		int64_t x = 0;
		int64_t z = 0;

		friend constexpr bool operator==(
			const VegetationWorldMillimeterPoint& lhs,
			const VegetationWorldMillimeterPoint& rhs) noexcept
		{
			return lhs.x == rhs.x && lhs.z == rhs.z;
		}

		friend constexpr bool operator!=(
			const VegetationWorldMillimeterPoint& lhs,
			const VegetationWorldMillimeterPoint& rhs) noexcept
		{
			return !(lhs == rhs);
		}
	};

	struct VegetationSafeStrokeSegment
	{
		VegetationWorldMillimeterPoint begin{};
		VegetationWorldMillimeterPoint end{};

		friend constexpr bool operator==(
			const VegetationSafeStrokeSegment& lhs,
			const VegetationSafeStrokeSegment& rhs) noexcept
		{
			return lhs.begin == rhs.begin && lhs.end == rhs.end;
		}

		friend constexpr bool operator!=(
			const VegetationSafeStrokeSegment& lhs,
			const VegetationSafeStrokeSegment& rhs) noexcept
		{
			return !(lhs == rhs);
		}
	};

	struct VegetationStrokeCanonicalizationResult
	{
		bool succeeded = false;
		std::vector<VegetationSafeStrokeSegment> safe_segments{};
		std::string error{};
	};

	struct VegetationStrokeResampleResult
	{
		bool succeeded = false;
		std::vector<VegetationWorldMillimeterPoint> dabs{};
		std::string error{};
	};

	enum class VegetationBrushMode : uint8_t
	{
		Paint = 0,
		Erase
	};

	struct VegetationBrushStroke
	{
		VegetationBrushMode mode = VegetationBrushMode::Paint;
		VegetationId selected_species{};
		uint32_t radius_mm = 250;
		uint8_t strength = 1;
		uint8_t falloff = 0;
		uint32_t spacing_mm = 1;
		uint64_t stroke_seed = 0;
		std::vector<VegetationSurfaceSampleRequest> path{};
	};

	enum class VegetationPaletteEditMode : uint8_t
	{
		Add = 0,
		Replace,
		Remove
	};

	struct VegetationPaletteEdit
	{
		VegetationPaletteEditMode mode = VegetationPaletteEditMode::Add;
		VegetationId target_species_id{};
		VegetationPaletteRecord replacement{};
		bool clear_weights = false;
	};

	using VegetationBrushApplyResult = VegetationMutationResult;
	using VegetationPaletteApplyResult = VegetationMutationResult;

	ASH_API bool vegetation_surface_request_to_world_millimeter(
		const VegetationSurfaceSampleRequest& request,
		VegetationWorldMillimeterPoint& out_point);
	ASH_API VegetationStrokeCanonicalizationResult canonicalize_vegetation_stroke(
		const std::vector<VegetationWorldMillimeterPoint>& raw_points);
	ASH_API VegetationStrokeResampleResult resample_vegetation_stroke(
		const std::vector<VegetationWorldMillimeterPoint>& raw_points,
		uint32_t spacing_mm);
	ASH_API uint8_t vegetation_brush_amount(
		uint64_t distance_mm,
		uint32_t radius_mm,
		uint8_t strength,
		uint8_t falloff);
	ASH_API VegetationBrushApplyResult apply_vegetation_brush_stroke(
		VegetationLayerWorkingSet& working_set,
		const VegetationBrushStroke& stroke);
	ASH_API VegetationPaletteApplyResult apply_vegetation_palette_edit(
		VegetationLayerWorkingSet& working_set,
		const VegetationPaletteEdit& edit);
}
