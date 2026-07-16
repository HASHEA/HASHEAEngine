#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationSurface.h"

#include <cstdint>
#include <memory>
#include <string>

namespace AshEngine
{
	struct VegetationSurfaceBinding
	{
		uint64_t surface_entity_id = 0;
	};

	struct VegetationSurfaceCaptureResult
	{
		VegetationSurfaceStatus status = VegetationSurfaceStatus::Failed;
		std::shared_ptr<const IVegetationSurfaceSnapshot> snapshot{};
		std::string detail{};
	};

	class ASH_API IVegetationSurfaceProvider
	{
	public:
		virtual ~IVegetationSurfaceProvider() = default;
		virtual VegetationSurfaceCaptureResult capture(VegetationSurfaceBinding binding) const = 0;
	};

	ASH_API VegetationSurfaceCaptureResult capture_vegetation_surface(
		const IVegetationSurfaceProvider* provider,
		VegetationSurfaceBinding binding);
}
