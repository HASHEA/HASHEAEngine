#include "doctest.h"

#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainImport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
	auto TestDirectory() -> std::filesystem::path
	{
		const std::filesystem::path directory =
			"Intermediate/test-temp/tests/terrain-import";
		std::filesystem::create_directories(directory);
		return directory;
	}

	auto MakeLayout(
		uint32_t width,
		uint32_t height,
		uint32_t component_quad_count = 2u) -> AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = width;
		layout.sample_count_z = height;
		layout.component_count_x = (width - 1u) / component_quad_count;
		layout.component_count_z = (height - 1u) / component_quad_count;
		layout.component_quad_count = component_quad_count;
		layout.sample_spacing_meters = 1.0f;
		REQUIRE(AshEngine::is_valid_terrain_grid_layout(layout));
		return layout;
	}

	auto WriteR16(
		const std::filesystem::path& path,
		const std::vector<uint16_t>& values,
		AshEngine::TerrainByteOrder byte_order) -> void
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		for (uint16_t value : values)
		{
			std::array<uint8_t, 2> bytes = {
				static_cast<uint8_t>(value),
				static_cast<uint8_t>(value >> 8u)
			};
			if (byte_order == AshEngine::TerrainByteOrder::BigEndian)
			{
				std::swap(bytes[0], bytes[1]);
			}
			REQUIRE(output.write(
				reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		}
	}

	auto WriteR32F(
		const std::filesystem::path& path,
		const std::vector<float>& values,
		AshEngine::TerrainByteOrder byte_order) -> void
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		for (float value : values)
		{
			std::array<uint8_t, 4> bytes{};
			std::memcpy(bytes.data(), &value, sizeof(value));
			if (byte_order == AshEngine::TerrainByteOrder::BigEndian)
			{
				std::reverse(bytes.begin(), bytes.end());
			}
			REQUIRE(output.write(
				reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		}
	}

	auto ReadAllBytes(const std::filesystem::path& path) -> std::vector<uint8_t>
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		REQUIRE(input.is_open());
		const std::streamsize size = input.tellg();
		REQUIRE(size >= 0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		input.seekg(0);
		REQUIRE(input.read(reinterpret_cast<char*>(bytes.data()), size));
		return bytes;
	}

	auto ReadTextFile(const std::filesystem::path& path) -> std::string
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.is_open());
		return std::string(
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	enum class TestExrPixelType : int32_t
	{
		Half = 1,
		Float = 2
	};

	struct TestExrChannel
	{
		std::string name{};
		TestExrPixelType pixel_type = TestExrPixelType::Float;
		std::vector<float> values{};
	};

	auto FloatToHalf(float value) -> uint16_t
	{
		uint32_t bits = 0u;
		std::memcpy(&bits, &value, sizeof(bits));
		const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
		uint32_t mantissa = bits & 0x007fffffu;
		int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
		if (exponent <= 0)
		{
			if (exponent < -10)
			{
				return sign;
			}
			mantissa = (mantissa | 0x00800000u) >> (1 - exponent);
			if ((mantissa & 0x00001000u) != 0u)
			{
				mantissa += 0x00002000u;
			}
			return static_cast<uint16_t>(sign | (mantissa >> 13u));
		}
		if (exponent >= 31)
		{
			return static_cast<uint16_t>(sign | 0x7c00u);
		}
		if ((mantissa & 0x00001000u) != 0u)
		{
			mantissa += 0x00002000u;
			if ((mantissa & 0x00800000u) != 0u)
			{
				mantissa = 0u;
				++exponent;
				if (exponent >= 31)
				{
					return static_cast<uint16_t>(sign | 0x7c00u);
				}
			}
		}
		return static_cast<uint16_t>(
			sign | static_cast<uint16_t>(exponent << 10u) |
			static_cast<uint16_t>(mantissa >> 13u));
	}

	auto WriteUncompressedExr(
		const std::filesystem::path& path,
		uint32_t width,
		uint32_t height,
		const std::vector<TestExrChannel>& channels,
		bool header_only = false) -> void
	{
		REQUIRE(width > 0u);
		REQUIRE(height > 0u);
		REQUIRE_FALSE(channels.empty());
		const size_t sample_count = static_cast<size_t>(width) * height;
	for (const TestExrChannel& channel : channels)
	{
		REQUIRE_FALSE(channel.name.empty());
		if (!header_only)
		{
			REQUIRE(channel.values.size() == sample_count);
		}
	}

		std::vector<uint8_t> bytes{};
		const auto append_u8 = [&](uint8_t value) { bytes.push_back(value); };
		const auto append_u16 = [&](uint16_t value)
			{
				bytes.push_back(static_cast<uint8_t>(value));
				bytes.push_back(static_cast<uint8_t>(value >> 8u));
			};
		const auto append_u32 = [&](uint32_t value)
			{
				for (uint32_t shift = 0u; shift < 32u; shift += 8u)
				{
					bytes.push_back(static_cast<uint8_t>(value >> shift));
				}
			};
		const auto append_u64 = [&](uint64_t value)
			{
				for (uint32_t shift = 0u; shift < 64u; shift += 8u)
				{
					bytes.push_back(static_cast<uint8_t>(value >> shift));
				}
			};
		const auto append_float = [&](float value)
			{
				uint32_t encoded = 0u;
				std::memcpy(&encoded, &value, sizeof(encoded));
				append_u32(encoded);
			};
		const auto append_string = [&](const std::string& value)
			{
				bytes.insert(bytes.end(), value.begin(), value.end());
				append_u8(0u);
			};
		const auto append_attribute = [&](
			const std::string& name,
			const std::string& type,
			const std::vector<uint8_t>& payload)
			{
				append_string(name);
				append_string(type);
				append_u32(static_cast<uint32_t>(payload.size()));
				bytes.insert(bytes.end(), payload.begin(), payload.end());
			};
		const auto make_payload = [&](const std::function<void()>& writer)
			{
				std::vector<uint8_t> prefix{};
				prefix.swap(bytes);
				writer();
				std::vector<uint8_t> payload{};
				payload.swap(bytes);
				bytes.swap(prefix);
				return payload;
			};

		append_u32(0x01312f76u);
		append_u32(2u);
		append_attribute("channels", "chlist", make_payload([&]()
			{
				for (const TestExrChannel& channel : channels)
				{
					append_string(channel.name);
					append_u32(static_cast<uint32_t>(channel.pixel_type));
					append_u8(0u);
					append_u8(0u);
					append_u8(0u);
					append_u8(0u);
					append_u32(1u);
					append_u32(1u);
				}
				append_u8(0u);
			}));
		append_attribute("compression", "compression", make_payload([&]()
			{
				append_u8(0u);
			}));
		const auto append_window = [&]()
			{
				append_u32(0u);
				append_u32(0u);
				append_u32(width - 1u);
				append_u32(height - 1u);
			};
		append_attribute("dataWindow", "box2i", make_payload(append_window));
		append_attribute("displayWindow", "box2i", make_payload(append_window));
		append_attribute("lineOrder", "lineOrder", make_payload([&]()
			{
				append_u8(0u);
			}));
		append_attribute("pixelAspectRatio", "float", make_payload([&]()
			{
				append_float(1.0f);
			}));
		append_attribute("screenWindowCenter", "v2f", make_payload([&]()
			{
				append_float(0.0f);
				append_float(0.0f);
			}));
		append_attribute("screenWindowWidth", "float", make_payload([&]()
			{
				append_float(1.0f);
			}));
		append_u8(0u);
		if (header_only)
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			REQUIRE(output.is_open());
			REQUIRE(output.write(
				reinterpret_cast<const char*>(bytes.data()), bytes.size()));
			return;
		}

		uint64_t scanline_bytes = 0u;
		for (const TestExrChannel& channel : channels)
		{
			scanline_bytes += static_cast<uint64_t>(width) *
				(channel.pixel_type == TestExrPixelType::Half ? 2u : 4u);
		}
		uint64_t scanline_offset = bytes.size() + static_cast<uint64_t>(height) * 8u;
		for (uint32_t y = 0u; y < height; ++y)
		{
			append_u64(scanline_offset);
			scanline_offset += 8u + scanline_bytes;
		}
		for (uint32_t y = 0u; y < height; ++y)
		{
			append_u32(y);
			append_u32(static_cast<uint32_t>(scanline_bytes));
			for (const TestExrChannel& channel : channels)
			{
				for (uint32_t x = 0u; x < width; ++x)
				{
					const float value = channel.values[static_cast<size_t>(y) * width + x];
					if (channel.pixel_type == TestExrPixelType::Half)
					{
						append_u16(FloatToHalf(value));
					}
					else
					{
						append_float(value);
					}
				}
			}
		}
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		REQUIRE(output.write(
			reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	}

	auto ReadR32F(
		const std::filesystem::path& path,
		AshEngine::TerrainByteOrder byte_order) -> std::vector<float>
	{
		const std::vector<uint8_t> bytes = ReadAllBytes(path);
		REQUIRE(bytes.size() % sizeof(float) == 0u);
		std::vector<float> values(bytes.size() / sizeof(float));
		for (size_t index = 0u; index < values.size(); ++index)
		{
			std::array<uint8_t, 4> value_bytes{};
			std::copy_n(bytes.data() + index * 4u, 4u, value_bytes.data());
			if (byte_order == AshEngine::TerrainByteOrder::BigEndian)
			{
				std::reverse(value_bytes.begin(), value_bytes.end());
			}
			std::memcpy(&values[index], value_bytes.data(), sizeof(float));
		}
		return values;
	}

	auto MakeImportDesc(
		const std::filesystem::path& path,
		uint32_t source_width,
		uint32_t source_height,
		const AshEngine::TerrainGridLayout& target_layout)
		-> AshEngine::TerrainHeightImportDesc
	{
		AshEngine::TerrainHeightImportDesc desc{};
		desc.source_path = path;
		desc.format = AshEngine::TerrainHeightFileFormat::RawR16;
		desc.target_layout = target_layout;
		desc.height_mapping = { -100.0f, 1000.0f };
		desc.source_width = source_width;
		desc.source_height = source_height;
		return desc;
	}

	auto Import(
		const AshEngine::TerrainHeightImportDesc& desc,
		AshEngine::TerrainImportReport* report = nullptr)
		-> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::string error{};
		REQUIRE(AshEngine::import_terrain_height(
			17u, desc, snapshot, report, &error) == AshEngine::TerrainImportResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
		REQUIRE(snapshot);
		return snapshot;
	}

	auto FinalHeights(const AshEngine::TerrainAssetSnapshot& snapshot) -> std::vector<float>
	{
		std::vector<float> heights{};
		heights.reserve(static_cast<size_t>(snapshot.layout.sample_count_x) *
			snapshot.layout.sample_count_z);
		for (uint32_t z = 0u; z < snapshot.layout.sample_count_z; ++z)
		{
			for (uint32_t x = 0u; x < snapshot.layout.sample_count_x; ++x)
			{
				const AshEngine::TerrainComponentCoord owner =
					AshEngine::get_terrain_sample_owner(snapshot.layout, x, z);
				const size_t component_index = static_cast<size_t>(owner.z) *
					snapshot.layout.component_count_x + owner.x;
				REQUIRE(component_index < snapshot.components.size());
				REQUIRE(snapshot.components[component_index]);
				const auto& component = *snapshot.components[component_index];
				const AshEngine::TerrainSampleRect rect =
					AshEngine::get_terrain_component_snapshot_rect(snapshot.layout, owner);
				const size_t local = static_cast<size_t>(z - rect.min_z) *
					component.sample_width + (x - rect.min_x);
				REQUIRE(local < component.heights.size());
				heights.push_back(component.heights[local]);
			}
		}
		return heights;
	}

	auto MakeLayeredSnapshot(
		const std::shared_ptr<const AshEngine::TerrainAssetSnapshot>& source,
		AshEngine::TerrainLayerId& out_layer_id)
		-> std::shared_ptr<const AshEngine::TerrainAssetSnapshot>
	{
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*source);
		out_layer_id.bytes[0] = 0x42u;
		AshEngine::TerrainEditLayer layer{};
		layer.id = out_layer_id;
		layer.name = "Export Layer";
		layer.height_blocks.push_back({
			{ 0u, 0u }, { 0u, 0u, 1u, 1u }, { 23.5f }, { 1.0f }
		});
		std::array<float, AshEngine::k_terrain_material_layer_count> weights{};
		weights[3] = 1.0f;
		weights[4] = 3.0f;
		layer.weight_blocks.push_back({
			{ 0u, 0u }, { 0u, 0u, 1u, 1u }, { weights }, { 1.0f }
		});
		snapshot->edit_layers =
			std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
				std::vector<AshEngine::TerrainEditLayer>{ layer });
		return snapshot;
	}
}

TEST_CASE("Terrain import RAW R16 honors byte order and independent axis flips")
{
	const auto directory = TestDirectory();
	const auto little_path = directory / "r16-little.raw";
	const auto big_path = directory / "r16-big.raw";
	const std::array<uint16_t, 5> row = { 0u, 1u, 32768u, 65534u, 65535u };
	std::vector<uint16_t> values{};
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		for (uint32_t x = 0u; x < row.size(); ++x)
		{
			values.push_back(row[(x + z) % row.size()]);
		}
	}
	WriteR16(little_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	WriteR16(big_path, values, AshEngine::TerrainByteOrder::BigEndian);
	const auto layout = MakeLayout(5u, 3u);

	auto desc = MakeImportDesc(little_path, 5u, 3u, layout);
	AshEngine::TerrainImportReport report{};
	const auto little = Import(desc, &report);
	CHECK(report.source_width == 5u);
	CHECK(report.source_height == 3u);
	CHECK(report.source_bits_per_sample == 16u);
	REQUIRE(little->base_heights);
	CHECK(*little->base_heights == values);

	desc.source_path = big_path;
	desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	const auto big = Import(desc);
	CHECK(*big->base_heights == values);

	desc.source_path = little_path;
	desc.byte_order = AshEngine::TerrainByteOrder::LittleEndian;
	desc.flip_x = true;
	const auto flipped_x = Import(desc);
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		for (uint32_t x = 0u; x < 5u; ++x)
		{
			CHECK((*flipped_x->base_heights)[z * 5u + x] == values[z * 5u + (4u - x)]);
		}
	}

	desc.flip_x = false;
	desc.flip_z = true;
	const auto flipped_z = Import(desc);
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		for (uint32_t x = 0u; x < 5u; ++x)
		{
			CHECK((*flipped_z->base_heights)[z * 5u + x] == values[(2u - z) * 5u + x]);
		}
	}

	std::filesystem::remove(little_path);
	std::filesystem::remove(big_path);
}

TEST_CASE("Terrain import RAW export round-trips R16 and byte-stable R32F")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "roundtrip-source.raw";
	const auto r16_path = directory / "roundtrip-r16.raw";
	const auto r32_path = directory / "roundtrip-r32.raw";
	const auto r32_second_path = directory / "roundtrip-r32-second.raw";
	const std::array<uint16_t, 5> row = { 0u, 1u, 32768u, 65534u, 65535u };
	std::vector<uint16_t> values{};
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		values.insert(values.end(), row.begin(), row.end());
	}
	WriteR16(source_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	const auto desc = MakeImportDesc(source_path, 5u, 3u, MakeLayout(5u, 3u));
	const auto source = Import(desc);

	AshEngine::TerrainHeightExportDesc export_desc{};
	export_desc.destination_path = r16_path;
	export_desc.format = AshEngine::TerrainHeightFileFormat::RawR16;
	std::string error{};
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	auto reimport_desc = desc;
	reimport_desc.source_path = r16_path;
	const auto r16_reimported = Import(reimport_desc);
	const auto before = FinalHeights(*source);
	const auto after = FinalHeights(*r16_reimported);
	REQUIRE(before.size() == after.size());
	const float tolerance = desc.height_mapping.height_range / 65535.0f;
	for (size_t index = 0u; index < before.size(); ++index)
	{
		CHECK(std::abs(before[index] - after[index]) <= tolerance);
	}

	export_desc.destination_path = r32_path;
	export_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	export_desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	reimport_desc.source_path = r32_path;
	reimport_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	reimport_desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	const auto r32_reimported = Import(reimport_desc);
	export_desc.destination_path = r32_second_path;
	REQUIRE(AshEngine::export_terrain_height(*r32_reimported, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadAllBytes(r32_path) == ReadAllBytes(r32_second_path));

	for (const auto& path : { source_path, r16_path, r32_path, r32_second_path })
	{
		std::filesystem::remove(path);
	}
}

TEST_CASE("Terrain import RAW R32F preserves arbitrary finite values byte-for-byte")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "r32-arbitrary-source.raw";
	const auto exported_path = directory / "r32-arbitrary-export.raw";
	const std::vector<float> values = {
		0.1234567f, -3.25f, 17.03125f, 499.99997f, 899.75f,
		1.0f / 3.0f, 2.0f / 7.0f, -99.875f, 42.424242f, 700.00006f,
		-0.0f, 0.0f, 128.125f, 256.0625f, 512.03125f
	};
	WriteR32F(source_path, values, AshEngine::TerrainByteOrder::BigEndian);
	auto desc = MakeImportDesc(source_path, 5u, 3u, MakeLayout(5u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	const auto snapshot = Import(desc);
	CHECK(FinalHeights(*snapshot) == values);

	AshEngine::TerrainHeightExportDesc export_desc{};
	export_desc.destination_path = exported_path;
	export_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	export_desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	std::string error{};
	REQUIRE(AshEngine::export_terrain_height(*snapshot, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadAllBytes(exported_path) == ReadAllBytes(source_path));

	std::filesystem::remove(source_path);
	std::filesystem::remove(exported_path);
}

TEST_CASE("Terrain import RAW exports every logical source and rejects invalid selectors early")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "source-selection.raw";
	std::vector<uint16_t> values(15u, 32768u);
	WriteR16(source_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	const auto base = Import(MakeImportDesc(
		source_path, 5u, 3u, MakeLayout(5u, 3u)));
	AshEngine::TerrainLayerId layer_id{};
	const auto layered = MakeLayeredSnapshot(base, layer_id);
	std::string error{};

	AshEngine::TerrainHeightExportDesc desc{};
	desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	desc.destination_path = directory / "final-source.raw";
	desc.source = AshEngine::TerrainExportSource::FinalComposedHeight;
	REQUIRE(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadR32F(desc.destination_path, desc.byte_order)[0] ==
		FinalHeights(*layered)[0]);

	desc.destination_path = directory / "base-source.raw";
	desc.source = AshEngine::TerrainExportSource::BaseHeight;
	REQUIRE(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadR32F(desc.destination_path, desc.byte_order)[0] ==
		AshEngine::decode_terrain_height_r16(32768u, layered->height_mapping));

	desc.destination_path = directory / "height-layer-source.raw";
	desc.source = AshEngine::TerrainExportSource::HeightEditLayer;
	desc.source_layer_id = layer_id;
	REQUIRE(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadR32F(desc.destination_path, desc.byte_order)[0] == 23.5f);

	desc.destination_path = directory / "weight-layer-source.raw";
	desc.source = AshEngine::TerrainExportSource::MaterialWeightLayer;
	desc.material_layer_index = 3u;
	REQUIRE(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadR32F(desc.destination_path, desc.byte_order)[0] == 0.25f);

	const auto invalid_path = directory / "invalid-source.raw";
	desc.destination_path = invalid_path;
	desc.source = AshEngine::TerrainExportSource::HeightEditLayer;
	desc.source_layer_id = {};
	CHECK(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::InvalidArguments);
	CHECK_FALSE(std::filesystem::exists(invalid_path));
	CHECK_FALSE(std::filesystem::exists(invalid_path.string() + ".tmp"));
	desc.source = AshEngine::TerrainExportSource::MaterialWeightLayer;
	desc.source_layer_id = layer_id;
	desc.material_layer_index = AshEngine::k_terrain_material_layer_count;
	CHECK(AshEngine::export_terrain_height(*layered, desc, &error) ==
		AshEngine::TerrainImportResult::InvalidArguments);
	CHECK_FALSE(std::filesystem::exists(invalid_path));

	for (const auto& name : {
		"final-source.raw", "base-source.raw", "height-layer-source.raw",
		"weight-layer-source.raw" })
	{
		std::filesystem::remove(directory / name);
	}
	std::filesystem::remove(source_path);
}

TEST_CASE("Terrain import RAW applies explicit crop and deterministic Catmull-Rom")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "resize-source.raw";
	const std::vector<uint16_t> values = {
		0u, 1000u, 2000u, 3000u, 4000u, 5000u,
		6000u, 7000u, 8000u, 9000u, 10000u, 11000u,
		12000u, 13000u, 14000u, 15000u, 16000u, 17000u
	};
	WriteR16(source_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	auto desc = MakeImportDesc(source_path, 6u, 3u, MakeLayout(3u, 3u));
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> sentinel{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		1u, desc, sentinel, nullptr, &error) ==
		AshEngine::TerrainImportResult::InvalidDimensions);
	CHECK_FALSE(sentinel);

	desc.resize_policy = AshEngine::TerrainResizePolicy::Crop;
	const auto cropped = Import(desc);
	REQUIRE(cropped->base_heights);
	CHECK(*cropped->base_heights == std::vector<uint16_t>{
		1000u, 2000u, 3000u,
		7000u, 8000u, 9000u,
		13000u, 14000u, 15000u });

	desc.target_layout = MakeLayout(5u, 5u);
	desc.resize_policy = AshEngine::TerrainResizePolicy::CatmullRom;
	const auto first = Import(desc);
	const auto second = Import(desc);
	REQUIRE(first->base_heights);
	REQUIRE(second->base_heights);
	CHECK(*first->base_heights == *second->base_heights);
	std::filesystem::remove(source_path);
}

TEST_CASE("Terrain import RAW enforces memory budget and cancellation without publication")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "policy-source.raw";
	WriteR16(source_path, { 0u }, AshEngine::TerrainByteOrder::LittleEndian);
	auto desc = MakeImportDesc(source_path, 0xffffffffu, 0xffffffffu, MakeLayout(3u, 3u));
	desc.resize_policy = AshEngine::TerrainResizePolicy::CatmullRom;
	desc.peak_memory_limit_bytes = 1024ull * 1024ull * 1024ull;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		1u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK_FALSE(snapshot);

	desc = MakeImportDesc(source_path, 1u, 1u, MakeLayout(3u, 3u));
	desc.resize_policy = AshEngine::TerrainResizePolicy::CatmullRom;
	desc.cancellation.cancel();
	CHECK(AshEngine::import_terrain_height(
		1u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::Cancelled);
	CHECK_FALSE(snapshot);
	std::filesystem::remove(source_path);
}

TEST_CASE("Terrain import RAW production R32F fits the approved one GiB phase budget")
{
	const auto directory = TestDirectory();
	const auto missing_source = directory / "missing-production-r32.raw";
	const auto destination = directory / "missing-production-r32.AshTerrain";
	std::filesystem::remove(missing_source);
	std::filesystem::remove(destination);
	std::filesystem::remove(destination.string() + ".import.tmp");

	auto desc = MakeImportDesc(
		missing_source,
		AshEngine::k_terrain_sample_count,
		AshEngine::k_terrain_sample_count,
		AshEngine::make_default_terrain_grid_layout());
	desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	desc.peak_memory_limit_bytes = 1024ull * 1024ull * 1024ull;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		101u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::IoFailure);
	CHECK_FALSE(snapshot);
	CHECK(AshEngine::import_terrain_height_to_container(
		101u, desc, destination, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::IoFailure);
	CHECK_FALSE(snapshot);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(destination.string() + ".import.tmp"));

	desc.peak_memory_limit_bytes = 980000000ull;
	CHECK(AshEngine::import_terrain_height(
		101u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK_FALSE(snapshot);
}

TEST_CASE("Terrain import RAW cancellation removes temporary export after rows begin")
{
	const auto directory = TestDirectory();
	const auto destination = directory / "cancelled-export.raw";
	const auto temporary = std::filesystem::path(destination.string() + ".tmp");
	std::filesystem::remove(destination);
	std::filesystem::remove(temporary);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		5u, MakeLayout(1025u, 1025u, 256u), { -100.0f, 1000.0f },
		10.0f, snapshot, &error));
	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	AshEngine::TerrainCancellationToken cancellation = desc.cancellation;
	std::thread cancel_thread([&]()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		std::error_code error_code{};
		while (std::chrono::steady_clock::now() < deadline)
		{
			const uint64_t size = std::filesystem::exists(temporary, error_code)
				? std::filesystem::file_size(temporary, error_code) : 0u;
			if (!error_code && size > 0u)
			{
				cancellation.cancel();
				return;
			}
			error_code.clear();
			std::this_thread::yield();
		}
		cancellation.cancel();
	});
	const auto result = AshEngine::export_terrain_height(*snapshot, desc, &error);
	cancel_thread.join();
	CHECK(result == AshEngine::TerrainImportResult::Cancelled);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Terrain import RAW container publication validates then atomically replaces")
{
	const auto directory = TestDirectory();
	const auto source_path = directory / "container-source.raw";
	const auto destination = directory / "imported.AshTerrain";
	std::filesystem::remove(destination);
	std::filesystem::remove(destination.string() + ".import.tmp");
	std::vector<uint16_t> values(15u, 12345u);
	WriteR16(source_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	const auto desc = MakeImportDesc(source_path, 5u, 3u, MakeLayout(5u, 3u));
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> published{};
	AshEngine::TerrainImportReport report{};
	std::string error{};
	REQUIRE(AshEngine::import_terrain_height_to_container(
		91u, desc, destination, published, &report, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE(published);
	CHECK(published->asset_id == 91u);
	CHECK(published->source_path == destination);
	CHECK(report.source_bits_per_sample == 16u);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(AshEngine::load_terrain_container(
		destination, loaded, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	CHECK(*loaded->base_heights == values);

	const std::vector<uint8_t> before = ReadAllBytes(destination);
	auto limited = desc;
	limited.peak_memory_limit_bytes = 1024u * 1024u;
	const auto previous = published;
	CHECK(AshEngine::import_terrain_height_to_container(
		91u, limited, destination, published, nullptr, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK(published == previous);
	CHECK(ReadAllBytes(destination) == before);
	CHECK_FALSE(std::filesystem::exists(destination.string() + ".import.tmp"));

	auto cancelled = desc;
	cancelled.cancellation.cancel();
	CHECK(AshEngine::import_terrain_height_to_container(
		91u, cancelled, destination, published, nullptr, &error) ==
		AshEngine::TerrainImportResult::Cancelled);
	CHECK(published == previous);
	CHECK(ReadAllBytes(destination) == before);
	CHECK_FALSE(std::filesystem::exists(destination.string() + ".import.tmp"));

	const auto r32_source = directory / "container-r32-source.raw";
	const auto r32_destination = directory / "container-r32.AshTerrain";
	const auto r32_export = directory / "container-r32-export.raw";
	const std::vector<float> r32_values = {
		0.1234567f, -3.25f, 17.03125f, 499.99997f, 899.75f,
		1.0f / 3.0f, 2.0f / 7.0f, -99.875f, 42.424242f, 700.00006f,
		-0.0f, 0.0f, 128.125f, 256.0625f, 512.03125f
	};
	WriteR32F(r32_source, r32_values, AshEngine::TerrainByteOrder::BigEndian);
	auto r32_desc = MakeImportDesc(r32_source, 5u, 3u, MakeLayout(5u, 3u));
	r32_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	r32_desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> r32_published{};
	REQUIRE(AshEngine::import_terrain_height_to_container(
		92u, r32_desc, r32_destination, r32_published, nullptr, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE(r32_published);
	CHECK(FinalHeights(*r32_published) == r32_values);
	AshEngine::TerrainHeightExportDesc r32_export_desc{};
	r32_export_desc.destination_path = r32_export;
	r32_export_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	r32_export_desc.byte_order = AshEngine::TerrainByteOrder::BigEndian;
	REQUIRE(AshEngine::export_terrain_height(
		*r32_published, r32_export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	CHECK(ReadAllBytes(r32_export) == ReadAllBytes(r32_source));

	std::filesystem::remove(source_path);
	std::filesystem::remove(destination);
	std::filesystem::remove(r32_source);
	std::filesystem::remove(r32_destination);
	std::filesystem::remove(r32_export);
}

TEST_CASE("Terrain import RAW source contract prepares publication before replace and bounds RLE")
{
	const std::string import_source = ReadTextFile(
		"project/src/engine/Function/Asset/TerrainImport.cpp");
	const size_t prepared = import_source.find("prepared_publication");
	const size_t replaced = import_source.find(
		"replace_file_atomically(temporary, destination_path)");
	REQUIRE(prepared != std::string::npos);
	REQUIRE(replaced != std::string::npos);
	CHECK(prepared < replaced);

	const std::string container_source = ReadTextFile(
		"project/src/engine/Function/Asset/TerrainContainer.cpp");
	CHECK(container_source.find("encode_terrain_rle_if_smaller(") !=
		std::string::npos);
	CHECK(container_source.find("stream_terrain_rle(") != std::string::npos);
	CHECK(container_source.find("encode_rle_candidate_if_smaller(") ==
		std::string::npos);
}

TEST_CASE("Terrain import PNG 16-bit grayscale round trips linearly")
{
	const auto directory = TestDirectory();
	const auto raw_path = directory / "png-gradient.raw";
	const auto png_path = directory / "png-gradient.png";
	const std::vector<uint16_t> encoded = {
		0u, 1u, 8192u,
		16384u, 32768u, 49152u,
		57344u, 65534u, 65535u
	};
	WriteR16(raw_path, encoded, AshEngine::TerrainByteOrder::LittleEndian);
	const auto layout = MakeLayout(3u, 3u);
	auto raw_desc = MakeImportDesc(raw_path, 3u, 3u, layout);
	const auto source = Import(raw_desc);

	AshEngine::TerrainHeightExportDesc export_desc{};
	export_desc.destination_path = png_path;
	export_desc.format = AshEngine::TerrainHeightFileFormat::Png;
	std::string error{};
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);

	auto png_desc = MakeImportDesc(png_path, 3u, 3u, layout);
	png_desc.format = AshEngine::TerrainHeightFileFormat::Png;
	AshEngine::TerrainImportReport report{};
	const auto round_trip = Import(png_desc, &report);
	CHECK(report.source_width == 3u);
	CHECK(report.source_height == 3u);
	CHECK(report.source_bits_per_sample == 16u);
	CHECK(report.warnings.empty());
	const auto expected = FinalHeights(*source);
	const auto actual = FinalHeights(*round_trip);
	REQUIRE(actual.size() == expected.size());
	const float tolerance = source->height_mapping.height_range / 65535.0f;
	for (size_t index = 0u; index < actual.size(); ++index)
	{
		CHECK(std::abs(actual[index] - expected[index]) <= tolerance);
	}

	std::filesystem::remove(raw_path);
	std::filesystem::remove(png_path);
}

TEST_CASE("Terrain import PNG 8-bit grayscale warns about precision")
{
	const auto path = TestDirectory() / "png-gray8.png";
	const std::array<uint8_t, 77> png = {
		0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au,
		0x00u, 0x00u, 0x00u, 0x0du, 0x49u, 0x48u, 0x44u, 0x52u,
		0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x03u,
		0x08u, 0x00u, 0x00u, 0x00u, 0x00u, 0x73u, 0x43u, 0xeau,
		0x63u, 0x00u, 0x00u, 0x00u, 0x14u, 0x49u, 0x44u, 0x41u,
		0x54u, 0x78u, 0xdau, 0x63u, 0x60u, 0x50u, 0x70u, 0x60u,
		0x48u, 0x68u, 0x58u, 0xc0u, 0x70u, 0xe0u, 0xc1u, 0x7fu,
		0x00u, 0x11u, 0x4bu, 0x04u, 0x80u, 0xf9u, 0xdfu, 0x38u,
		0xcfu, 0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4eu,
		0x44u, 0xaeu, 0x42u, 0x60u, 0x82u
	};
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		REQUIRE(output.write(
			reinterpret_cast<const char*>(png.data()), png.size()));
	}

	auto desc = MakeImportDesc(path, 3u, 3u, MakeLayout(3u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::Png;
	AshEngine::TerrainImportReport report{};
	const auto snapshot = Import(desc, &report);
	CHECK(report.source_bits_per_sample == 8u);
	REQUIRE(report.warnings.size() == 1u);
	CHECK(report.warnings[0] ==
		"8-bit PNG height source reduces terrain precision.");
	const std::array<uint8_t, 9> values = {
		0u, 32u, 64u, 96u, 128u, 160u, 192u, 224u, 255u
	};
	const auto heights = FinalHeights(*snapshot);
	REQUIRE(heights.size() == values.size());
	const float tolerance = desc.height_mapping.height_range / 65535.0f;
	for (size_t index = 0u; index < heights.size(); ++index)
	{
		const float expected = desc.height_mapping.height_offset +
			desc.height_mapping.height_range * values[index] / 255.0f;
		CHECK(std::abs(heights[index] - expected) <= tolerance);
	}

	desc.flip_x = true;
	const auto flipped_x = FinalHeights(*Import(desc));
	desc.flip_x = false;
	desc.flip_z = true;
	const auto flipped_z = FinalHeights(*Import(desc));
	for (uint32_t z = 0u; z < 3u; ++z)
	{
		for (uint32_t x = 0u; x < 3u; ++x)
		{
			const size_t output = static_cast<size_t>(z) * 3u + x;
			const float expected_x = desc.height_mapping.height_offset +
				desc.height_mapping.height_range * values[z * 3u + (2u - x)] / 255.0f;
			const float expected_z = desc.height_mapping.height_offset +
				desc.height_mapping.height_range * values[(2u - z) * 3u + x] / 255.0f;
			CHECK(std::abs(flipped_x[output] - expected_x) <= tolerance);
			CHECK(std::abs(flipped_z[output] - expected_z) <= tolerance);
		}
	}

	desc.flip_z = false;
	desc.source_width = 5u;
	desc.target_layout = MakeLayout(5u, 3u);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> mismatched{};
	std::string mismatch_error{};
	CHECK(AshEngine::import_terrain_height(
		17u, desc, mismatched, nullptr, &mismatch_error) ==
		AshEngine::TerrainImportResult::InvalidDimensions);
	CHECK_FALSE(mismatched);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain import PNG enforces exact WIC pixel negotiation")
{
	const std::string source = ReadTextFile(
		"project/src/engine/Function/Asset/TerrainPngCodecWin.cpp");
	CHECK(source.find("SetPixelFormat") != std::string::npos);
	CHECK(source.find("GUID_WICPixelFormat16bppGray") != std::string::npos);
	CHECK(source.find("IsEqualGUID") != std::string::npos);
}

TEST_CASE("Terrain import PNG removes temporary output when row production fails")
{
	const auto destination = TestDirectory() / "png-row-failure.png";
	const auto temporary = std::filesystem::path(destination.string() + ".tmp");
	std::filesystem::remove(destination);
	std::filesystem::remove(temporary);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		23u, MakeLayout(3u, 3u), { -100.0f, 1000.0f }, 25.0f,
		source, &error));
	REQUIRE(source);
	auto broken = std::make_shared<AshEngine::TerrainAssetSnapshot>(*source);
	REQUIRE_FALSE(broken->components.empty());
	broken->components[0].reset();

	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::Png;
	CHECK(AshEngine::export_terrain_height(*broken, desc, &error) ==
		AshEngine::TerrainImportResult::EncodeFailure);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporary));

	const auto cancelled_destination = TestDirectory() / "png-cancelled.png";
	const auto cancelled_temporary =
		std::filesystem::path(cancelled_destination.string() + ".tmp");
	std::filesystem::remove(cancelled_destination);
	std::filesystem::remove(cancelled_temporary);
	desc.destination_path = cancelled_destination;
	desc.cancellation.cancel();
	CHECK(AshEngine::export_terrain_height(*source, desc, &error) ==
		AshEngine::TerrainImportResult::Cancelled);
	CHECK_FALSE(std::filesystem::exists(cancelled_destination));
	CHECK_FALSE(std::filesystem::exists(cancelled_temporary));
}

TEST_CASE("Terrain import PNG rejects color pixels without luminance conversion")
{
	const auto path = TestDirectory() / "png-color.png";
	const std::array<uint8_t, 91> png = {
		0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au,
		0x00u, 0x00u, 0x00u, 0x0du, 0x49u, 0x48u, 0x44u, 0x52u,
		0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x03u,
		0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0xd9u, 0x4au, 0x22u,
		0xe8u, 0x00u, 0x00u, 0x00u, 0x22u, 0x49u, 0x44u, 0x41u,
		0x54u, 0x78u, 0xdau, 0x63u, 0xf8u, 0xcfu, 0xc0u, 0xc0u,
		0x00u, 0xc1u, 0x0au, 0x0eu, 0x09u, 0x0du, 0x0du, 0x0du,
		0xffu, 0xffu, 0xffu, 0x67u, 0xe0u, 0x12u, 0x91u, 0xd3u,
		0x30u, 0xb2u, 0x71u, 0x0bu, 0x88u, 0x02u, 0x00u, 0x98u,
		0xe1u, 0x09u, 0xfdu, 0x27u, 0xb9u, 0xafu, 0x8fu, 0x00u,
		0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4eu, 0x44u, 0xaeu,
		0x42u, 0x60u, 0x82u
	};
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		REQUIRE(output.write(
			reinterpret_cast<const char*>(png.data()), png.size()));
	}
	auto desc = MakeImportDesc(path, 3u, 3u, MakeLayout(3u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::Png;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		29u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::UnsupportedFormat);
	CHECK_FALSE(snapshot);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain import PNG rejects a grayscale TIFF with a PNG extension")
{
	const auto path = TestDirectory() / "not-really-png.png";
	const std::array<uint8_t, 171> tiff = {
		0x49u, 0x49u, 0x2au, 0x00u, 0x08u, 0x00u, 0x00u, 0x00u,
		0x0bu, 0x00u, 0x00u, 0x01u, 0x04u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u,
		0x04u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u,
		0x00u, 0x00u, 0x02u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x08u, 0x00u, 0x00u, 0x00u, 0x03u, 0x01u,
		0x03u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x06u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x11u, 0x01u,
		0x04u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0xa2u, 0x00u,
		0x00u, 0x00u, 0x15u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x16u, 0x01u,
		0x04u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u,
		0x00u, 0x00u, 0x17u, 0x01u, 0x04u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x09u, 0x00u, 0x00u, 0x00u, 0x1au, 0x01u,
		0x05u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x92u, 0x00u,
		0x00u, 0x00u, 0x1bu, 0x01u, 0x05u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x9au, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
		0x00u, 0x00u, 0x48u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x48u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u,
		0x00u, 0x00u, 0x00u, 0x20u, 0x40u, 0x60u, 0x80u, 0xa0u,
		0xc0u, 0xe0u, 0xffu
	};
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		REQUIRE(output.write(
			reinterpret_cast<const char*>(tiff.data()), tiff.size()));
	}
	auto desc = MakeImportDesc(path, 3u, 3u, MakeLayout(3u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::Png;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		31u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::UnsupportedFormat);
	CHECK_FALSE(snapshot);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain import EXR selects half and float channels linearly")
{
	const auto path = TestDirectory() / "terrain-channels.exr";
	const std::vector<float> half_values = {
		-100.0f, -50.0f, -25.0f,
		0.0f, 25.0f, 50.0f,
		100.0f, 500.0f, 900.0f
	};
	const std::vector<float> float_values = {
		-80.25f, -40.5f, -1.0f,
		0.0f, 1.0f, 40.5f,
		80.25f, 320.75f, 899.5f
	};
	WriteUncompressedExr(path, 3u, 3u, {
		{ "Height", TestExrPixelType::Float, float_values },
		{ "Y", TestExrPixelType::Half, half_values }
	});
	auto desc = MakeImportDesc(path, 3u, 3u, MakeLayout(3u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.exr_channel = "Y";
	AshEngine::TerrainImportReport half_report{};
	const auto half_snapshot = Import(desc, &half_report);
	CHECK(half_report.source_bits_per_sample == 16u);
	CHECK(half_report.warnings.empty());
	CHECK(FinalHeights(*half_snapshot) == half_values);

	desc.exr_channel = "Height";
	AshEngine::TerrainImportReport float_report{};
	const auto float_snapshot = Import(desc, &float_report);
	CHECK(float_report.source_bits_per_sample == 32u);
	CHECK(float_report.warnings.empty());
	CHECK(FinalHeights(*float_snapshot) == float_values);
	std::filesystem::remove(path);
}

TEST_CASE("Terrain import EXR round trips named float and half height channels")
{
	const auto directory = TestDirectory();
	const auto raw_path = directory / "exr-round-trip.raw";
	const auto float_path = directory / "exr-round-trip-float.exr";
	const auto half_path = directory / "exr-round-trip-half.exr";
	const std::vector<float> values = {
		-99.75f, -20.125f, -0.0f,
		0.125f, 100.5f, 300.25f,
		500.75f, 700.5f, 899.875f
	};
	WriteR32F(raw_path, values, AshEngine::TerrainByteOrder::LittleEndian);
	auto raw_desc = MakeImportDesc(raw_path, 3u, 3u, MakeLayout(3u, 3u));
	raw_desc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	const auto source = Import(raw_desc);

	AshEngine::TerrainHeightExportDesc export_desc{};
	export_desc.destination_path = float_path;
	export_desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	export_desc.exr_channel = "Height";
	export_desc.exr_pixel_type = AshEngine::TerrainExrPixelType::Float;
	std::string error{};
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);

	auto exr_desc = MakeImportDesc(float_path, 3u, 3u, source->layout);
	exr_desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	exr_desc.exr_channel = "Height";
	AshEngine::TerrainImportReport report{};
	const auto round_trip = Import(exr_desc, &report);
	CHECK(report.source_bits_per_sample == 32u);
	CHECK(FinalHeights(*round_trip) == values);

	export_desc.destination_path = half_path;
	export_desc.exr_pixel_type = AshEngine::TerrainExrPixelType::Half;
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	exr_desc.source_path = half_path;
	AshEngine::TerrainImportReport half_report{};
	const auto half_round_trip = Import(exr_desc, &half_report);
	CHECK(half_report.source_bits_per_sample == 16u);
	const std::vector<float> half_values = FinalHeights(*half_round_trip);
	REQUIRE(half_values.size() == values.size());
	for (size_t index = 0u; index < values.size(); ++index)
	{
		CHECK(std::abs(half_values[index] - values[index]) <= 0.25f);
	}
	std::filesystem::remove(raw_path);
	std::filesystem::remove(float_path);
	std::filesystem::remove(half_path);
}

TEST_CASE("Terrain import EXR rejects missing channels and malformed files")
{
	const auto directory = TestDirectory();
	const auto valid_path = directory / "exr-missing-channel.exr";
	const auto malformed_path = directory / "exr-malformed.exr";
	WriteUncompressedExr(valid_path, 3u, 3u, {
		{ "Y", TestExrPixelType::Float, std::vector<float>(9u, 12.0f) }
	});
	{
		std::ofstream output(malformed_path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		const std::array<uint8_t, 8> malformed = {
			0x76u, 0x2fu, 0x31u, 0x01u, 0xffu, 0xffu, 0xffu, 0xffu
		};
		REQUIRE(output.write(
			reinterpret_cast<const char*>(malformed.data()), malformed.size()));
	}

	auto desc = MakeImportDesc(valid_path, 3u, 3u, MakeLayout(3u, 3u));
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.exr_channel = "Missing";
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		47u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::DecodeFailure);
	CHECK_FALSE(snapshot);

	desc.source_path = malformed_path;
	desc.exr_channel = "Y";
	CHECK(AshEngine::import_terrain_height(
		47u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::DecodeFailure);
	CHECK_FALSE(snapshot);
	std::filesystem::remove(valid_path);
	std::filesystem::remove(malformed_path);
}

TEST_CASE("Terrain import EXR marks and reloads long channel names")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		51u, MakeLayout(3u, 3u), { -100.0f, 1000.0f }, 42.0f,
		source, &error));
	const auto destination = TestDirectory() / "exr-long-channel.exr";
	const std::string channel_name = "TerrainHeightChannelNameLongerThanThirtyOne";
	AshEngine::TerrainHeightExportDesc export_desc{};
	export_desc.destination_path = destination;
	export_desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	export_desc.exr_channel = channel_name;
	REQUIRE(AshEngine::export_terrain_height(*source, export_desc, &error) ==
		AshEngine::TerrainImportResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	const std::vector<uint8_t> bytes = ReadAllBytes(destination);
	REQUIRE(bytes.size() >= 8u);
	CHECK((bytes[5] & 0x04u) != 0u);

	auto import_desc = MakeImportDesc(destination, 3u, 3u, source->layout);
	import_desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	import_desc.exr_channel = channel_name;
	const auto reloaded = Import(import_desc);
	CHECK(FinalHeights(*reloaded) == FinalHeights(*source));
	std::filesystem::remove(destination);
}

TEST_CASE("Terrain import EXR cancels before publication and removes temporary output")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		53u, MakeLayout(3u, 3u), { -100.0f, 1000.0f }, 0.0f,
		source, &error));
	const auto destination = TestDirectory() / "exr-cancelled.exr";
	const auto temporary = std::filesystem::path(destination.string() + ".tmp");
	std::filesystem::remove(destination);
	std::filesystem::remove(temporary);
	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.exr_channel = "Y";
	desc.cancellation.cancel();
	CHECK(AshEngine::export_terrain_height(*source, desc, &error) ==
		AshEngine::TerrainImportResult::Cancelled);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Terrain import EXR cancels after encoding starts and before final rename")
{
	AshEngine::TerrainGridLayout layout{};
	layout.sample_count_x = 2049u;
	layout.sample_count_z = 2049u;
	layout.component_count_x = 8u;
	layout.component_count_z = 8u;
	layout.component_quad_count = 256u;
	layout.sample_spacing_meters = 1.0f;
	REQUIRE(AshEngine::is_valid_terrain_grid_layout(layout));
	auto source = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	source->layout = layout;
	source->height_mapping = { -100.0f, 1000.0f };
	std::vector<uint16_t> base_heights(
		static_cast<size_t>(layout.sample_count_x) * layout.sample_count_z);
	uint32_t random_state = 0x6b8b4567u;
	for (uint16_t& value : base_heights)
	{
		random_state = random_state * 1664525u + 1013904223u;
		value = static_cast<uint16_t>(random_state >> 16u);
	}
	source->base_heights =
		std::make_shared<const std::vector<uint16_t>>(std::move(base_heights));

	const auto destination = TestDirectory() / "exr-cancelled-after-encode.exr";
	const auto temporary = std::filesystem::path(destination.string() + ".tmp");
	std::filesystem::remove(destination);
	std::filesystem::remove(temporary);
	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.source = AshEngine::TerrainExportSource::BaseHeight;
	desc.exr_channel = "Y";
	AshEngine::TerrainImportResult result = AshEngine::TerrainImportResult::Success;
	std::string error{};
	std::thread exporter([&]()
		{
			result = AshEngine::export_terrain_height(*source, desc, &error);
		});
	bool temporary_observed = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (std::filesystem::exists(temporary))
		{
			temporary_observed = true;
			desc.cancellation.cancel();
			break;
		}
		if (std::filesystem::exists(destination))
		{
			break;
		}
		std::this_thread::yield();
	}
	if (!temporary_observed)
	{
		desc.cancellation.cancel();
	}
	exporter.join();
	CHECK(temporary_observed);
	CHECK(result == AshEngine::TerrainImportResult::Cancelled);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Terrain import EXR enforces one GiB before contiguous export allocation")
{
	AshEngine::TerrainGridLayout layout{};
	layout.sample_count_x = 32769u;
	layout.sample_count_z = 32769u;
	layout.component_count_x = 128u;
	layout.component_count_z = 128u;
	layout.component_quad_count = 256u;
	layout.sample_spacing_meters = 1.0f;
	REQUIRE(AshEngine::is_valid_terrain_grid_layout(layout));
	auto source = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	source->layout = layout;
	source->height_mapping = { -100.0f, 1000.0f };
	AshEngine::TerrainLayerId layer_id{};
	layer_id.bytes[0] = 0x5au;
	AshEngine::TerrainEditLayer layer{};
	layer.id = layer_id;
	source->edit_layers =
		std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ layer });

	const auto destination = TestDirectory() / "exr-over-budget.exr";
	std::filesystem::remove(destination);
	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.source = AshEngine::TerrainExportSource::HeightEditLayer;
	desc.source_layer_id = layer_id;
	desc.exr_channel = "Y";
	std::string error{};
	CHECK(AshEngine::export_terrain_height(*source, desc, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("Terrain import EXR budgets pixel and encoded buffers before allocation")
{
	AshEngine::TerrainGridLayout layout{};
	layout.sample_count_x = 10241u;
	layout.sample_count_z = 10241u;
	layout.component_count_x = 40u;
	layout.component_count_z = 40u;
	layout.component_quad_count = 256u;
	layout.sample_spacing_meters = 1.0f;
	REQUIRE(AshEngine::is_valid_terrain_grid_layout(layout));
	auto source = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	source->layout = layout;
	source->height_mapping = { -100.0f, 1000.0f };
	AshEngine::TerrainLayerId layer_id{};
	layer_id.bytes[0] = 0x7cu;
	AshEngine::TerrainEditLayer layer{};
	layer.id = layer_id;
	source->edit_layers =
		std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ layer });

	const auto destination = TestDirectory() / "exr-combined-over-budget.exr";
	const auto temporary = std::filesystem::path(destination.string() + ".tmp");
	std::filesystem::remove(destination);
	std::filesystem::remove(temporary);
	AshEngine::TerrainHeightExportDesc desc{};
	desc.destination_path = destination;
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.source = AshEngine::TerrainExportSource::HeightEditLayer;
	desc.source_layer_id = layer_id;
	desc.exr_channel = "Y";
	desc.exr_pixel_type = AshEngine::TerrainExrPixelType::Float;
	std::string error{};
	CHECK(AshEngine::export_terrain_height(*source, desc, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK_FALSE(std::filesystem::exists(destination));
	CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Terrain import EXR rejects multi-channel decode above its peak budget")
{
	const auto source_path = TestDirectory() / "exr-multi-channel-over-budget.exr";
	std::vector<TestExrChannel> channels{};
	channels.reserve(64u);
	for (uint32_t index = 0u; index < 64u; ++index)
	{
		channels.push_back({
			index == 0u ? "Height" : "C" + std::to_string(index),
			TestExrPixelType::Float,
			{}
		});
	}
	WriteUncompressedExr(source_path, 257u, 257u, channels, true);
	auto desc = MakeImportDesc(
		source_path, 257u, 257u, MakeLayout(257u, 257u, 256u));
	desc.format = AshEngine::TerrainHeightFileFormat::Exr;
	desc.exr_channel = "Height";
	desc.peak_memory_limit_bytes = 40ull * 1024ull * 1024ull;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	CHECK(AshEngine::import_terrain_height(
		59u, desc, snapshot, nullptr, &error) ==
		AshEngine::TerrainImportResult::MemoryLimitExceeded);
	CHECK_FALSE(snapshot);
	std::filesystem::remove(source_path);
}
