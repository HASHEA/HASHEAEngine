#include "Graphics/DirectX12/DX12GpuTiming.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    struct SourceBlock
    {
        size_t begin = std::string::npos;
        size_t end = std::string::npos;
        std::string text{};

        auto valid() const -> bool
        {
            return begin != std::string::npos && end != std::string::npos;
        }
    };

    auto read_source(const char* path) -> std::string
    {
        std::ifstream input(path);
        if (!input.is_open())
        {
            return {};
        }
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>() };
    }

    auto count_occurrences(std::string_view source, std::string_view needle) -> size_t
    {
        if (needle.empty())
        {
            return 0;
        }

        size_t count = 0;
        size_t search_from = 0;
        while ((search_from = source.find(needle, search_from)) != std::string_view::npos)
        {
            ++count;
            search_from += needle.size();
        }
        return count;
    }

    auto extract_balanced_block(
        const std::string& source,
        std::string_view marker,
        size_t search_from = 0) -> SourceBlock
    {
        const size_t marker_position = source.find(marker, search_from);
        if (marker_position == std::string::npos)
        {
            return {};
        }
        const size_t open_brace = source.find('{', marker_position + marker.size());
        if (open_brace == std::string::npos)
        {
            return {};
        }

        enum class ParseState : uint8_t
        {
            Code,
            String,
            Character,
            LineComment,
            BlockComment
        };

        ParseState state = ParseState::Code;
        uint32_t depth = 0;
        for (size_t index = open_brace; index < source.size(); ++index)
        {
            const char current = source[index];
            const char next = index + 1u < source.size() ? source[index + 1u] : '\0';
            switch (state)
            {
            case ParseState::Code:
                if (current == '/' && next == '/')
                {
                    state = ParseState::LineComment;
                    ++index;
                }
                else if (current == '/' && next == '*')
                {
                    state = ParseState::BlockComment;
                    ++index;
                }
                else if (current == '"')
                {
                    state = ParseState::String;
                }
                else if (current == '\'')
                {
                    state = ParseState::Character;
                }
                else if (current == '{')
                {
                    ++depth;
                }
                else if (current == '}')
                {
                    if (depth == 0u)
                    {
                        return {};
                    }
                    --depth;
                    if (depth == 0u)
                    {
                        const size_t end = index + 1u;
                        return { open_brace, end, source.substr(open_brace, end - open_brace) };
                    }
                }
                break;
            case ParseState::String:
                if (current == '\\')
                {
                    ++index;
                }
                else if (current == '"')
                {
                    state = ParseState::Code;
                }
                break;
            case ParseState::Character:
                if (current == '\\')
                {
                    ++index;
                }
                else if (current == '\'')
                {
                    state = ParseState::Code;
                }
                break;
            case ParseState::LineComment:
                if (current == '\n')
                {
                    state = ParseState::Code;
                }
                break;
            case ParseState::BlockComment:
                if (current == '*' && next == '/')
                {
                    state = ParseState::Code;
                    ++index;
                }
                break;
            }
        }
        return {};
    }
}

TEST_CASE("DX12 GPU timing converts queue ticks and validates frequency")
{
    CHECK(RHI::dx12_timestamp_ms(1000ull, 4000ull, 1'000'000ull) == doctest::Approx(3.0));
    CHECK(RHI::dx12_validate_timestamp_frequency(0ull) == RHI::GpuTimingResult::QueueFrequencyInvalid);
    CHECK(RHI::dx12_validate_timestamp_frequency(27'000'000ull) == RHI::GpuTimingResult::Success);
}

TEST_CASE("DX12 GPU timing sticky gate precedes slot reuse and EndQuery")
{
    const std::string source =
        read_source("project/src/engine/Graphics/DirectX12/DX12GpuTiming.cpp");
    REQUIRE_FALSE(source.empty());
    const size_t begin_frame = source.find("auto DX12GpuTiming::begin_frame");
    const size_t begin_scope = source.find("auto DX12GpuTiming::begin_scope", begin_frame);
    REQUIRE(begin_frame != std::string::npos);
    REQUIRE(begin_scope != std::string::npos);
    const std::string method = source.substr(begin_frame, begin_scope - begin_frame);

    const size_t sticky_read = method.find(
        "const GpuTimingResult sticky_failure = m_tracker.sticky_failure();");
    const size_t sticky_return = method.find("return sticky_failure;", sticky_read);
    const size_t begin_recording = method.find("m_tracker.begin_recording", sticky_return);
    const size_t first_query = method.find("EndQuery", begin_recording);
    REQUIRE(sticky_read != std::string::npos);
    REQUIRE(sticky_return != std::string::npos);
    REQUIRE(begin_recording != std::string::npos);
    REQUIRE(first_query != std::string::npos);
    CHECK(sticky_read < sticky_return);
    CHECK(sticky_return < begin_recording);
    CHECK(begin_recording < first_query);
}

TEST_CASE("DX12 GPU timing resolves before command list close and only after a complete frame")
{
    const std::string source =
        read_source("project/src/engine/Graphics/DirectX12/DX12GpuTiming.cpp");
    REQUIRE_FALSE(source.empty());
    const size_t end_frame = source.find("auto DX12GpuTiming::end_frame");
    const size_t try_collect = source.find("auto DX12GpuTiming::try_collect", end_frame);
    REQUIRE(end_frame != std::string::npos);
    REQUIRE(try_collect != std::string::npos);
    const std::string method = source.substr(end_frame, try_collect - end_frame);

    const size_t frame_end_query = method.find("EndQuery");
    const size_t set_query_count = method.find("m_tracker.set_query_count", frame_end_query);
    const size_t resolve_queries = method.find("ResolveQueryData", set_query_count);
    REQUIRE(frame_end_query != std::string::npos);
    REQUIRE(set_query_count != std::string::npos);
    REQUIRE(resolve_queries != std::string::npos);
    CHECK(frame_end_query < set_query_count);
    CHECK(set_query_count < resolve_queries);
}

TEST_CASE("DX12 GPU timing checked fence signal preserves the prior wait target on failure")
{
    const std::string source =
        read_source("project/src/engine/Graphics/DirectX12/DX12Fence.cpp");
    REQUIRE_FALSE(source.empty());
    const SourceBlock method = extract_balanced_block(source, "DX12Fence::signal_checked");
    REQUIRE(method.valid());
    const SourceBlock success =
        extract_balanced_block(method.text, "if (SUCCEEDED(signal_result))");
    REQUIRE(success.valid());

    constexpr std::string_view initialize_output = "out_value = m_fenceValue;";
    constexpr std::string_view commit_fence = "m_fenceValue = candidate_value;";
    constexpr std::string_view commit_output = "out_value = candidate_value;";
    const size_t candidate = method.text.find(
        "const uint64_t candidate_value = m_fenceValue + 1u;");
    const size_t queue_signal = method.text.find(
        "queue->Signal(m_fence.Get(), candidate_value)", candidate);
    REQUIRE(candidate != std::string::npos);
    REQUIRE(queue_signal != std::string::npos);
    CHECK(candidate < queue_signal);
    CHECK(queue_signal < success.begin);

    CHECK(count_occurrences(method.text, initialize_output) == 1u);
    CHECK(count_occurrences(method.text.substr(0u, queue_signal), initialize_output) == 1u);
    CHECK(count_occurrences(success.text, initialize_output) == 0u);
    CHECK(count_occurrences(method.text, commit_fence) == 1u);
    CHECK(count_occurrences(method.text, commit_output) == 1u);
    CHECK(count_occurrences(success.text, commit_fence) == 1u);
    CHECK(count_occurrences(success.text, commit_output) == 1u);

    const std::string before_signal = method.text.substr(0u, queue_signal);
    CHECK(count_occurrences(before_signal, commit_fence) == 0u);
    CHECK(count_occurrences(before_signal, commit_output) == 0u);
    std::string outside_success = method.text;
    outside_success.erase(success.begin, success.end - success.begin);
    CHECK(count_occurrences(outside_success, commit_fence) == 0u);
    CHECK(count_occurrences(outside_success, commit_output) == 0u);
}

TEST_CASE("DX12 GPU timing submission binds only after checked signal success")
{
    const std::string source =
        read_source("project/src/engine/Graphics/DirectX12/DX12Context.cpp");
    REQUIRE_FALSE(source.empty());
    const SourceBlock method = extract_balanced_block(
        source,
        "auto DX12Context::submit(const SubmitInfo& info) -> void");
    REQUIRE(method.valid());
    const SourceBlock success =
        extract_balanced_block(method.text, "if (SUCCEEDED(signal_result))");
    const SourceBlock failure =
        extract_balanced_block(method.text, "if (FAILED(signal_result))");
    REQUIRE(success.valid());
    REQUIRE(failure.valid());

    constexpr std::string_view checked_signal =
        "fr.fence->signal_checked(m_graphicsQueue.get_queue(), frame_completion_value)";
    constexpr std::string_view mark_submitted = "mark_frame_submitted";
    constexpr std::string_view fail_recording = "fail_frame_recording";
    const size_t checked_signal_position = method.text.find(checked_signal);
    REQUIRE(checked_signal_position != std::string::npos);
    CHECK(count_occurrences(method.text, checked_signal) == 1u);
    CHECK(checked_signal_position < success.begin);
    CHECK(checked_signal_position < failure.begin);

    CHECK(count_occurrences(method.text, mark_submitted) == 1u);
    CHECK(count_occurrences(success.text, mark_submitted) == 1u);
    CHECK(count_occurrences(failure.text, mark_submitted) == 0u);
    CHECK(count_occurrences(failure.text, fail_recording) == 1u);
    CHECK(count_occurrences(method.text, fail_recording) == 1u);
    CHECK(failure.text.find("GpuTimingResult::DeviceLost") != std::string::npos);
    CHECK(failure.text.find("GpuTimingResult::RecordFailed") != std::string::npos);

    const size_t mark_position = success.text.find(mark_submitted);
    REQUIRE(mark_position != std::string::npos);
    const size_t exact_value = success.text.find("frame_completion_value", mark_position);
    const size_t exact_fence = success.text.find("fr.fence->get_fence()", mark_position);
    REQUIRE(exact_value != std::string::npos);
    REQUIRE(exact_fence != std::string::npos);
    CHECK(mark_position < exact_value);
    CHECK(exact_value < exact_fence);
}

TEST_CASE("DX12 GPU timing ignores unrelated submits and cancels only at frame close")
{
    const std::string timing_source =
        read_source("project/src/engine/Graphics/DirectX12/DX12GpuTiming.cpp");
    REQUIRE_FALSE(timing_source.empty());
    const size_t mark_submitted = timing_source.find("auto DX12GpuTiming::mark_frame_submitted");
    const size_t fail_recording = timing_source.find("auto DX12GpuTiming::fail_frame_recording", mark_submitted);
    REQUIRE(mark_submitted != std::string::npos);
    REQUIRE(fail_recording != std::string::npos);
    const std::string mark_method =
        timing_source.substr(mark_submitted, fail_recording - mark_submitted);
    CHECK(mark_method.find("cancel_recording") == std::string::npos);
    CHECK(mark_method.find("if (!command_buffer_submitted)") != std::string::npos);
    CHECK(mark_method.find("return GpuTimingResult::Success;") != std::string::npos);

    const std::string context_source =
        read_source("project/src/engine/Graphics/DirectX12/DX12Context.cpp");
    REQUIRE_FALSE(context_source.empty());
    const size_t end_frame = context_source.find(
        "auto DX12Context::end_frame(bool has_acquired_swapchain_image) -> void");
    const size_t wait_idle = context_source.find("auto DX12Context::wait_idle", end_frame);
    REQUIRE(end_frame != std::string::npos);
    REQUIRE(wait_idle != std::string::npos);
    const std::string end_method = context_source.substr(end_frame, wait_idle - end_frame);
    CHECK(end_method.find("cancel_unsubmitted_frame") != std::string::npos);
}
