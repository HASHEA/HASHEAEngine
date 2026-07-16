#include "Function/Asset/VegetationCodec.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Scene/VegetationSurfaceProvider.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	constexpr double kVegetationChunkSizeMeters = 256.0;

	bool IsZeroPayload(const AshEngine::VegetationSurfaceSample& sample)
	{
		if (sample.world_height_meters != 0.0 || sample.world_normal != glm::dvec3(0.0))
		{
			return false;
		}
		for (const uint8_t weight : sample.material_slot_weights)
		{
			if (weight != 0)
			{
				return false;
			}
		}
		return true;
	}

	void CheckFailedWithoutSamples(const AshEngine::VegetationSurfaceBatchResult& result)
	{
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK(result.samples.empty());
	}
}

TEST_CASE("Vegetation core hashes are canonical across SHA padding and CRC vectors")
{
	const std::vector<uint8_t> abc{ 'a', 'b', 'c' };
	const AshEngine::VegetationSha256 zero_digest{};
	const std::string boundary_message =
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

	CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(nullptr, 0)) ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(abc.data(), abc.size())) ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(
		reinterpret_cast<const uint8_t*>(boundary_message.data()), boundary_message.size())) ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	CHECK(AshEngine::vegetation_crc32(
		reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
	CHECK(AshEngine::vegetation_sha256(nullptr, 1) == zero_digest);
	CHECK(AshEngine::vegetation_crc32(nullptr, 1) == 0u);
}

TEST_CASE("Vegetation core negative chunk coordinates use floor division")
{
	AshEngine::VegetationChunkCoord chunk{};
	glm::dvec2 local{};
	REQUIRE(AshEngine::split_vegetation_world_xz(
		glm::dvec2(-0.001, -256.001), chunk, local));
	CHECK(chunk.x == -1);
	CHECK(chunk.z == -2);
	CHECK(local.x == doctest::Approx(255.999));
	CHECK(local.y == doctest::Approx(255.999));

	REQUIRE(AshEngine::split_vegetation_world_xz(
		glm::dvec2(-256.0, 256.0), chunk, local));
	CHECK(chunk.x == -1);
	CHECK(chunk.z == 1);
	CHECK(local.x == doctest::Approx(0.0));
	CHECK(local.y == doctest::Approx(0.0));
}

TEST_CASE("Vegetation core ties-even u16 rounding is mechanically exact")
{
	struct RoundingCase
	{
		double input = 0.0;
		uint16_t expected = 0;
	};
	const std::array<RoundingCase, 6> cases{
		RoundingCase{ 0.5, 0 },
		RoundingCase{ 1.5, 2 },
		RoundingCase{ 2.5, 2 },
		RoundingCase{ 3.5, 4 },
		RoundingCase{ 65534.5, 65534 },
		RoundingCase{ 65535.0, 65535 }
	};
	for (const RoundingCase& test_case : cases)
	{
		uint16_t rounded = 123;
		REQUIRE(AshEngine::vegetation_round_ties_even_u16(test_case.input, rounded));
		CHECK(rounded == test_case.expected);
	}

	const std::array<double, 4> invalid{
		-0.5,
		65535.5,
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity()
	};
	for (const double value : invalid)
	{
		uint16_t rounded = 456;
		CHECK_FALSE(AshEngine::vegetation_round_ties_even_u16(value, rounded));
		CHECK(rounded == 456);
	}
}

TEST_CASE("Vegetation core world split round trips boundaries and fails closed on int64 overflow")
{
	const std::array<double, 5> coordinates{ 0.0, 255.999, 256.0, -0.001, -256.0 };
	for (const double coordinate : coordinates)
	{
		AshEngine::VegetationChunkCoord chunk{};
		glm::dvec2 local{};
		REQUIRE(AshEngine::split_vegetation_world_xz(
			glm::dvec2(coordinate, coordinate), chunk, local));
		CHECK(local.x >= 0.0);
		CHECK(local.x < kVegetationChunkSizeMeters);
		CHECK(local.y >= 0.0);
		CHECK(local.y < kVegetationChunkSizeMeters);
		CHECK(static_cast<double>(chunk.x) * kVegetationChunkSizeMeters + local.x ==
			doctest::Approx(coordinate));
		CHECK(static_cast<double>(chunk.z) * kVegetationChunkSizeMeters + local.y ==
			doctest::Approx(coordinate));
	}

	AshEngine::VegetationChunkCoord unchanged_chunk{ 17, -23 };
	glm::dvec2 unchanged_local(7.0, 9.0);
	const double positive_overflow = std::ldexp(1.0, 71);
	CHECK_FALSE(AshEngine::split_vegetation_world_xz(
		glm::dvec2(positive_overflow, 0.0), unchanged_chunk, unchanged_local));
	CHECK(unchanged_chunk.x == 17);
	CHECK(unchanged_chunk.z == -23);
	CHECK(unchanged_local == glm::dvec2(7.0, 9.0));

	const double negative_overflow = std::nextafter(-positive_overflow,
		-std::numeric_limits<double>::infinity());
	CHECK_FALSE(AshEngine::split_vegetation_world_xz(
		glm::dvec2(0.0, negative_overflow), unchanged_chunk, unchanged_local));
	CHECK(unchanged_chunk.x == 17);
	CHECK(unchanged_chunk.z == -23);
	CHECK(unchanged_local == glm::dvec2(7.0, 9.0));
}

TEST_CASE("Vegetation surface wrapper rejects a partial batch without publishing residue")
{
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 12.0, glm::dvec3(0.0, 2.0, 0.0))
	};
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5),
		VegetationTest::SurfaceRequest(1.5, 1.5)
	};
	const AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::milliseconds(50));

	const AshEngine::VegetationSurfaceBatchResult result =
		AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);
	CheckFailedWithoutSamples(result);
}

TEST_CASE("Vegetation surface wrapper recomputes aggregate status with fixed priority")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5),
		VegetationTest::SurfaceRequest(1.5, 1.5)
	};

	SUBCASE("Ready plus Outside is Ready")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 2.0, glm::dvec3(0.0, 2.0, 0.0)),
			VegetationTest::NonReadySurfaceSample(1, AshEngine::VegetationSurfaceStatus::Outside)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Ready);
		REQUIRE(result.samples.size() == 2);
		CHECK(result.samples[0].world_normal == glm::dvec3(0.0, 1.0, 0.0));
		CHECK(result.samples[1].status == AshEngine::VegetationSurfaceStatus::Outside);
		CHECK(IsZeroPayload(result.samples[1]));
	}

	SUBCASE("Ready plus Pending is Pending")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Pending;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 2.0, glm::dvec3(0.0, 1.0, 0.0)),
			VegetationTest::NonReadySurfaceSample(1, AshEngine::VegetationSurfaceStatus::Pending)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Pending);
		REQUIRE(result.samples.size() == 2);
		CHECK(IsZeroPayload(result.samples[1]));
	}

	SUBCASE("all Outside samples still aggregate to Ready")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::NonReadySurfaceSample(0, AshEngine::VegetationSurfaceStatus::Outside),
			VegetationTest::NonReadySurfaceSample(1, AshEngine::VegetationSurfaceStatus::Outside)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Ready);
		REQUIRE(result.samples.size() == 2);
		CHECK(result.samples[0].status == AshEngine::VegetationSurfaceStatus::Outside);
		CHECK(result.samples[1].status == AshEngine::VegetationSurfaceStatus::Outside);
	}

	SUBCASE("Pending plus Failed is Failed")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Failed;
		snapshot.result.samples = {
			VegetationTest::NonReadySurfaceSample(0, AshEngine::VegetationSurfaceStatus::Pending),
			VegetationTest::NonReadySurfaceSample(1, AshEngine::VegetationSurfaceStatus::Failed)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		REQUIRE(result.samples.size() == 2);
		CHECK(IsZeroPayload(result.samples[0]));
		CHECK(IsZeroPayload(result.samples[1]));
	}

	SUBCASE("Failed plus Pending remains Failed")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Failed;
		snapshot.result.samples = {
			VegetationTest::NonReadySurfaceSample(0, AshEngine::VegetationSurfaceStatus::Failed),
			VegetationTest::NonReadySurfaceSample(1, AshEngine::VegetationSurfaceStatus::Pending)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		REQUIRE(result.samples.size() == 2);
		CHECK(IsZeroPayload(result.samples[0]));
		CHECK(IsZeroPayload(result.samples[1]));
	}
}

TEST_CASE("Vegetation surface wrapper accepts the exact batch limit and forwards operation control")
{
	std::vector<AshEngine::VegetationSurfaceSampleRequest> requests;
	std::vector<AshEngine::VegetationSurfaceSample> samples;
	requests.reserve(4096);
	samples.reserve(4096);
	for (uint32_t index = 0; index < 4096; ++index)
	{
		requests.push_back(VegetationTest::SurfaceRequest(0.5, 0.5));
		samples.push_back(VegetationTest::ReadySurfaceSample(
			index, 1.0, glm::dvec3(0.0, 1.0, 0.0)));
	}

	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = samples;
	const AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(5));
	const auto result = AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);

	CHECK(result.status == AshEngine::VegetationSurfaceStatus::Ready);
	CHECK(result.samples.size() == 4096);
	CHECK(snapshot.sample_call_count == 1);
	CHECK(snapshot.identity_call_count == 2);
	CHECK(snapshot.last_cancel_requested == control.cancel_requested);
	CHECK(snapshot.last_deadline == control.deadline);
}

TEST_CASE("Vegetation surface wrapper rejects malformed batch shape and declared aggregate")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5),
		VegetationTest::SurfaceRequest(1.5, 1.5)
	};
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0)),
		VegetationTest::ReadySurfaceSample(1, 2.0, glm::dvec3(0.0, 1.0, 0.0))
	};

	SUBCASE("too many samples")
	{
		snapshot.result.samples.push_back(
			VegetationTest::ReadySurfaceSample(2, 3.0, glm::dvec3(0.0, 1.0, 0.0)));
	}
	SUBCASE("duplicate indices")
	{
		snapshot.result.samples[1].request_index = 0;
	}
	SUBCASE("out of order indices")
	{
		snapshot.result.samples[0].request_index = 1;
		snapshot.result.samples[1].request_index = 0;
	}
	SUBCASE("declared aggregate mismatch")
	{
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Pending;
	}
	SUBCASE("unknown sample status")
	{
		snapshot.result.samples[1].status =
			static_cast<AshEngine::VegetationSurfaceStatus>(255);
	}
	SUBCASE("unknown aggregate status")
	{
		snapshot.result.status = static_cast<AshEngine::VegetationSurfaceStatus>(255);
	}

	const auto result = AshEngine::sample_vegetation_surface_batch(
		snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
	CheckFailedWithoutSamples(result);
}

TEST_CASE("Vegetation surface wrapper rejects residue on every non-ready status")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};
	const std::array<AshEngine::VegetationSurfaceStatus, 3> statuses{
		AshEngine::VegetationSurfaceStatus::Outside,
		AshEngine::VegetationSurfaceStatus::Pending,
		AshEngine::VegetationSurfaceStatus::Failed
	};
	for (const AshEngine::VegetationSurfaceStatus status : statuses)
	{
		for (uint32_t residue_kind = 0; residue_kind < 3; ++residue_kind)
		{
			CAPTURE(static_cast<uint32_t>(status));
			CAPTURE(residue_kind);
			VegetationTest::ScriptedSurfaceSnapshot snapshot{};
			snapshot.result.status = status == AshEngine::VegetationSurfaceStatus::Outside
				? AshEngine::VegetationSurfaceStatus::Ready
				: status;
			snapshot.result.samples = {
				VegetationTest::NonReadySurfaceSample(0, status)
			};
			if (residue_kind == 0)
			{
				snapshot.result.samples[0].world_height_meters = 1.0;
			}
			else if (residue_kind == 1)
			{
				snapshot.result.samples[0].world_normal = glm::dvec3(0.0, 1.0, 0.0);
			}
			else
			{
				snapshot.result.samples[0].material_slot_weights[7] = 1;
			}
			const auto result = AshEngine::sample_vegetation_surface_batch(
				snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
			CheckFailedWithoutSamples(result);
		}
	}
}

TEST_CASE("Vegetation surface normal policy normalizes and rounds canonical slopes")
{
	glm::dvec3 normalized(9.0);
	uint16_t slope = std::numeric_limits<uint16_t>::max();
	REQUIRE(AshEngine::evaluate_vegetation_surface_normal(
		glm::dvec3(0.0, 2.0, 0.0), normalized, slope));
	CHECK(normalized == glm::dvec3(0.0, 1.0, 0.0));
	CHECK(slope == 0);

	REQUIRE(AshEngine::evaluate_vegetation_surface_normal(
		glm::dvec3(4.0, 0.0, 0.0), normalized, slope));
	CHECK(normalized == glm::dvec3(1.0, 0.0, 0.0));
	CHECK(slope == 1571);

	REQUIRE(AshEngine::evaluate_vegetation_surface_normal(
		glm::dvec3(0.0, -3.0, 0.0), normalized, slope));
	CHECK(normalized == glm::dvec3(0.0, -1.0, 0.0));
	CHECK(slope == 1571);
}

TEST_CASE("Vegetation surface normal policy rejects tiny and non-finite inputs without output residue")
{
	const std::array<glm::dvec3, 3> invalid_normals{
		glm::dvec3(0.0),
		glm::dvec3(1.0e-21, 0.0, 0.0),
		glm::dvec3(std::numeric_limits<double>::infinity(), 0.0, 0.0)
	};
	for (const glm::dvec3& invalid_normal : invalid_normals)
	{
		glm::dvec3 normalized(7.0, 8.0, 9.0);
		uint16_t slope = 1234;
		CHECK_FALSE(AshEngine::evaluate_vegetation_surface_normal(
			invalid_normal, normalized, slope));
		CHECK(normalized == glm::dvec3(7.0, 8.0, 9.0));
		CHECK(slope == 1234);
	}
}

TEST_CASE("Vegetation surface wrapper rejects non-finite ready data and invalid material totals")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
	};

	SUBCASE("NaN height")
	{
		snapshot.result.samples[0].world_height_meters =
			std::numeric_limits<double>::quiet_NaN();
	}
	SUBCASE("infinite normal")
	{
		snapshot.result.samples[0].world_normal.x =
			std::numeric_limits<double>::infinity();
	}
	SUBCASE("zero normal")
	{
		snapshot.result.samples[0].world_normal = glm::dvec3(0.0);
	}
	SUBCASE("weights total 254")
	{
		snapshot.result.samples[0].material_slot_weights = { 254, 0, 0, 0, 0, 0, 0, 0 };
	}
	SUBCASE("weights total 256")
	{
		snapshot.result.samples[0].material_slot_weights = { 255, 1, 0, 0, 0, 0, 0, 0 };
	}

	const auto result = AshEngine::sample_vegetation_surface_batch(
		snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
	CheckFailedWithoutSamples(result);
}

TEST_CASE("Vegetation surface wrapper validates request count coordinates cancellation and deadline before sampling")
{
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
	};
	std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};
	AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::milliseconds(50));

	SUBCASE("empty requests")
	{
		requests.clear();
	}
	SUBCASE("4097 requests")
	{
		requests.assign(4097, VegetationTest::SurfaceRequest(0.5, 0.5));
	}
	SUBCASE("NaN local coordinate")
	{
		requests[0].local_xz.x = std::numeric_limits<double>::quiet_NaN();
	}
	SUBCASE("infinite local coordinate")
	{
		requests[0].local_xz.y = std::numeric_limits<double>::infinity();
	}
	SUBCASE("negative local coordinate")
	{
		requests[0].local_xz.x = -0.001;
	}
	SUBCASE("local coordinate at chunk size")
	{
		requests[0].local_xz.y = kVegetationChunkSizeMeters;
	}
	SUBCASE("null cancellation state")
	{
		control.cancel_requested.reset();
	}
	SUBCASE("default deadline")
	{
		control.deadline = {};
	}
	SUBCASE("expired deadline")
	{
		control.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
	}
	SUBCASE("pre-cancelled")
	{
		auto cancelled = std::make_shared<std::atomic_bool>(true);
		control.cancel_requested = cancelled;
	}

	const auto result = AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);
	CheckFailedWithoutSamples(result);
	CHECK(snapshot.bounds_call_count == 0);
	CHECK(snapshot.identity_call_count == 0);
	CHECK(snapshot.sample_call_count == 0);
}

TEST_CASE("Vegetation surface wrapper validates coarse bounds without treating them as a point filter")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(AshEngine::VegetationChunkCoord{ 9, -7 }, glm::dvec2(1.0, 2.0))
	};

	SUBCASE("inverted x range fails before sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.surface_bounds = {
			AshEngine::VegetationChunkCoord{ 1, 0 }, AshEngine::VegetationChunkCoord{ 0, 0 }
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 0);
		CHECK(snapshot.identity_call_count == 0);
	}

	SUBCASE("inverted z range fails before sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.surface_bounds = {
			AshEngine::VegetationChunkCoord{ 0, 1 }, AshEngine::VegetationChunkCoord{ 0, 0 }
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 0);
		CHECK(snapshot.identity_call_count == 0);
	}

	SUBCASE("request outside coarse range is delegated")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.surface_bounds = {
			AshEngine::VegetationChunkCoord{ 0, 0 }, AshEngine::VegetationChunkCoord{ 0, 0 }
		};
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::NonReadySurfaceSample(0, AshEngine::VegetationSurfaceStatus::Outside)
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		REQUIRE(result.status == AshEngine::VegetationSurfaceStatus::Ready);
		REQUIRE(result.samples.size() == 1);
		CHECK(result.samples[0].status == AshEngine::VegetationSurfaceStatus::Outside);
		CHECK(snapshot.sample_call_count == 1);
		CHECK(snapshot.last_request_count == 1);
	}
}

TEST_CASE("Vegetation surface wrapper rechecks cancellation and deadline after provider return")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
	};

	SUBCASE("provider observes cancellation before returning")
	{
		auto cancel_requested = std::make_shared<std::atomic_bool>(false);
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = cancel_requested;
		control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		snapshot.before_sample_return = [cancel_requested](const auto&)
		{
			cancel_requested->store(true);
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 1);
	}

	SUBCASE("deadline expires inside provider")
	{
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::milliseconds(1));
		snapshot.before_sample_return = [](const AshEngine::VegetationOperationControl& provider_control)
		{
			while (std::chrono::steady_clock::now() <= provider_control.deadline)
			{
			}
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 1);
	}
}

TEST_CASE("Vegetation surface wrapper rejects zero or changing identities without publishing samples")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};

	SUBCASE("all-zero surface id")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		snapshot.identity_before.surface_id.fill(0);
		snapshot.identity_after = snapshot.identity_before;
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 0);
	}

	SUBCASE("content revision changes after sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		++snapshot.identity_after.content_revision;
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 1);
		CHECK(snapshot.identity_call_count == 2);
	}

	SUBCASE("surface id changes after sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		++snapshot.identity_after.surface_id[0];
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
		CHECK(snapshot.sample_call_count == 1);
		CHECK(snapshot.identity_call_count == 2);
	}

	SUBCASE("residency revision changes after sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		++snapshot.identity_after.residency_revision;
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
	}

	SUBCASE("transform revision changes after sampling")
	{
		VegetationTest::ScriptedSurfaceSnapshot snapshot{};
		++snapshot.identity_after.transform_revision;
		snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		snapshot.result.samples = {
			VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
		};
		const auto result = AshEngine::sample_vegetation_surface_batch(
			snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50)));
		CheckFailedWithoutSamples(result);
	}
}

TEST_CASE("Vegetation surface wrapper catches snapshot exceptions without publishing residue")
{
	const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
		VegetationTest::SurfaceRequest(0.5, 0.5)
	};
	VegetationTest::ScriptedSurfaceSnapshot snapshot{};
	snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
	snapshot.result.samples = {
		VegetationTest::ReadySurfaceSample(0, 1.0, glm::dvec3(0.0, 1.0, 0.0))
	};

	SUBCASE("bounds throws")
	{
		snapshot.throw_on_bounds = true;
	}
	SUBCASE("first identity read throws")
	{
		snapshot.throw_on_identity_call = 1;
	}
	SUBCASE("sample throws")
	{
		snapshot.throw_on_sample = true;
	}
	SUBCASE("second identity read throws")
	{
		snapshot.throw_on_identity_call = 2;
	}

	AshEngine::VegetationSurfaceBatchResult result{};
	CHECK_NOTHROW(result = AshEngine::sample_vegetation_surface_batch(
		snapshot, requests, VegetationTest::ActiveControl(std::chrono::milliseconds(50))));
	CheckFailedWithoutSamples(result);
}

TEST_CASE("Vegetation surface capture validates every status and snapshot pointer shape")
{
	const AshEngine::VegetationSurfaceBinding binding{ 42 };
	const auto ready_snapshot = std::make_shared<VegetationTest::ScriptedSurfaceSnapshot>();

	SUBCASE("Ready plus snapshot is legal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		provider.result.snapshot = ready_snapshot;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Ready);
		CHECK(result.snapshot == ready_snapshot);
		CHECK(provider.capture_call_count == 1);
		CHECK(provider.last_binding.surface_entity_id == 42);
	}

	SUBCASE("Ready plus null is illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Ready;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Pending plus null is legal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Pending;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Pending);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Pending plus snapshot is illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Pending;
		provider.result.snapshot = ready_snapshot;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Failed plus null is legal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Failed;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Failed plus snapshot is illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Failed;
		provider.result.snapshot = ready_snapshot;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Outside is illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Outside;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("Outside plus snapshot is also illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Outside;
		provider.result.snapshot = ready_snapshot;
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("unknown status is illegal")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = static_cast<AshEngine::VegetationSurfaceStatus>(255);
		const auto result = AshEngine::capture_vegetation_surface(&provider, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}
}

TEST_CASE("Vegetation surface capture fails closed for no provider and provider exceptions")
{
	const AshEngine::VegetationSurfaceBinding binding{ 42 };

	SUBCASE("no provider")
	{
		const auto result = AshEngine::capture_vegetation_surface(nullptr, binding);
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.detail.empty());
	}

	SUBCASE("provider throws")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.throw_on_capture = true;
		AshEngine::VegetationSurfaceCaptureResult result{};
		CHECK_NOTHROW(result = AshEngine::capture_vegetation_surface(&provider, binding));
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("zero binding is rejected before provider invocation")
	{
		VegetationTest::ScriptedSurfaceProvider provider{};
		provider.result.status = AshEngine::VegetationSurfaceStatus::Pending;
		const auto result = AshEngine::capture_vegetation_surface(
			&provider, AshEngine::VegetationSurfaceBinding{});
		CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
		CHECK_FALSE(result.snapshot);
		CHECK(provider.capture_call_count == 0);
	}
}
