#include "Graphics/Vulkan/VulkanGpuTiming.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <fstream>
#include <iterator>
#include <regex>
#include <string>

TEST_CASE("Vulkan GPU timing masks valid bits and handles wraparound")
{
    CHECK(RHI::vulkan_timestamp_delta(0x00fffff0ull, 0x00000010ull, 24u) == 0x20ull);
    CHECK(RHI::vulkan_timestamp_delta(100ull, 340ull, 64u) == 240ull);
    CHECK(RHI::vulkan_timestamp_delta(0ull, 1ull, 0u) == 0ull);
}

TEST_CASE("Vulkan GPU timing sticky gate precedes slot reuse and query reset")
{
    std::ifstream input("project/src/engine/Graphics/Vulkan/VulkanGpuTiming.cpp");
    REQUIRE(input.is_open());
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };

    const size_t begin_frame = source.find("auto VulkanGpuTiming::begin_frame");
    const size_t begin_scope = source.find("auto VulkanGpuTiming::begin_scope", begin_frame);
    REQUIRE(begin_frame != std::string::npos);
    REQUIRE(begin_scope != std::string::npos);
    const std::string begin_frame_source = source.substr(begin_frame, begin_scope - begin_frame);

    const size_t initialization_check =
        begin_frame_source.find("if (m_initialization_result != GpuTimingResult::Success)");
    const size_t initialization_return = begin_frame_source.find("return m_initialization_result;");
    const size_t sticky_read = begin_frame_source.find(
        "const GpuTimingResult sticky_failure = m_tracker.sticky_failure();");
    const size_t sticky_check =
        begin_frame_source.find("if (sticky_failure != GpuTimingResult::Success)");
    const size_t sticky_return = begin_frame_source.find("return sticky_failure;");
    const size_t begin_recording = begin_frame_source.find("m_tracker.begin_recording");
    const size_t query_reset = begin_frame_source.find("vkCmdResetQueryPool");
    REQUIRE(initialization_check != std::string::npos);
    REQUIRE(initialization_return != std::string::npos);
    REQUIRE(sticky_read != std::string::npos);
    REQUIRE(sticky_check != std::string::npos);
    REQUIRE(sticky_return != std::string::npos);
    REQUIRE(begin_recording != std::string::npos);
    REQUIRE(query_reset != std::string::npos);
    CHECK(initialization_check < initialization_return);
    CHECK(initialization_return < sticky_read);
    CHECK(sticky_read < sticky_check);
    CHECK(sticky_check < sticky_return);
    CHECK(sticky_return < begin_recording);
    CHECK(begin_recording < query_reset);
}

TEST_CASE("Vulkan GPU timing submission hook follows real queue success")
{
    std::ifstream input("project/src/engine/Graphics/Vulkan/VulkanContext.cpp");
    REQUIRE(input.is_open());
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };

    const size_t submit_method = source.find("auto VulkanContext::submit(const SubmitInfo& info) -> void");
    const size_t immediate_method = source.find("auto VulkanContext::submit_immediately", submit_method);
    REQUIRE(submit_method != std::string::npos);
    REQUIRE(immediate_method != std::string::npos);
    CHECK(source.substr(submit_method, immediate_method - submit_method).find("_mark_gpu_timing_submitted") == std::string::npos);

    const size_t end_frame = source.find("auto VulkanContext::end_frame(bool has_acquired_swapchain_image) -> void");
    REQUIRE(end_frame != std::string::npos);
    const std::string end_frame_source = source.substr(end_frame);
    CHECK(end_frame_source.find("const uint64_t frame_completion_value = absoluteFrame + 1u;") != std::string::npos);

    const std::regex submit_branch_contract{
        R"(if\s*\(\s*submit_result\s*==\s*VK_SUCCESS\s*\)\s*\{\s*_mark_gpu_timing_submitted\s*\(\s*frame_completion_value[^;{}]*\);\s*\}\s*else\s*\{\s*_fail_gpu_timing_submission\s*\(\s*\);\s*\}\s*VK_CHECK_RESULT\s*\(\s*submit_result\s*\)\s*;)" };

    size_t search_from = 0;
    uint32_t verified_submit_paths = 0;
    while (true)
    {
        const size_t queue_submit = end_frame_source.find("vkQueueSubmit", search_from);
        if (queue_submit == std::string::npos)
        {
            break;
        }
        const size_t next_queue_submit = end_frame_source.find("vkQueueSubmit", queue_submit + 1u);
        const size_t submit_path_end =
            next_queue_submit == std::string::npos ? end_frame_source.size() : next_queue_submit;
        const std::string submit_path =
            end_frame_source.substr(queue_submit, submit_path_end - queue_submit);
        CHECK(std::regex_search(submit_path, submit_branch_contract));
        ++verified_submit_paths;
        search_from = queue_submit + 1u;
    }
    CHECK(verified_submit_paths == 4u);
}
