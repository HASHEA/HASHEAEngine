#pragma once

namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	// Runs before the first frame for --rhi-selftest-constant-buffer and logs PASS/FAIL.
	auto run_rhi_constant_buffer_self_test(RHI::GraphicsContext* context) -> bool;
}
