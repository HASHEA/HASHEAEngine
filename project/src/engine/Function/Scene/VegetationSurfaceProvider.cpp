#include "Function/Scene/VegetationSurfaceProvider.h"

namespace AshEngine
{
	namespace
	{
		VegetationSurfaceCaptureResult failed_capture(const char* detail)
		{
			VegetationSurfaceCaptureResult result{};
			result.status = VegetationSurfaceStatus::Failed;
			result.detail = detail;
			return result;
		}
	}

	VegetationSurfaceCaptureResult capture_vegetation_surface(
		const IVegetationSurfaceProvider* provider,
		const VegetationSurfaceBinding binding)
	{
		if (provider == nullptr)
		{
			return failed_capture("No vegetation surface provider is registered.");
		}
		if (binding.surface_entity_id == 0)
		{
			return failed_capture("Vegetation surface binding is invalid.");
		}

		VegetationSurfaceCaptureResult result{};
		try
		{
			result = provider->capture(binding);
		}
		catch (...)
		{
			return failed_capture("Vegetation surface capture failed.");
		}

		switch (result.status)
		{
		case VegetationSurfaceStatus::Ready:
			if (result.snapshot != nullptr)
			{
				return result;
			}
			break;
		case VegetationSurfaceStatus::Pending:
		case VegetationSurfaceStatus::Failed:
			if (result.snapshot == nullptr)
			{
				return result;
			}
			break;
		case VegetationSurfaceStatus::Outside:
		default:
			break;
		}
		return failed_capture("Vegetation surface capture result is invalid.");
	}
}
