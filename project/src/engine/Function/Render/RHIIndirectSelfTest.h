#pragma once

namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	// SDD-2026-07-09-indirect-draw-substrate: one-shot validation chain for the indirect draw RHI
	// (compute writes args -> barrier to IndirectArgs -> dispatch/draw indirect -> readback check).
	// Triggered by --rhi-selftest-indirect; logs "[RHISelfTest] ... PASS/FAIL".
	auto run_rhi_indirect_self_test(RHI::GraphicsContext* context) -> bool;
}
