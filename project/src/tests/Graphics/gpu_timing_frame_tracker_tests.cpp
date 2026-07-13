#include "Graphics/GpuTimingFrameTracker.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cstdint>

namespace
{
    auto make_snapshot(uint64_t frame_index, double elapsed_ms) -> RHI::GpuTimingFrameSnapshot
    {
        RHI::GpuTimingFrameSnapshot snapshot{};
        snapshot.submitted_frame_index = frame_index;
        snapshot.frame_elapsed_ms = elapsed_ms;
        snapshot.scope_count = 2;
        snapshot.overflowed = true;
        snapshot.scopes[0] = { 0x101ull, elapsed_ms / 4.0 };
        snapshot.scopes[1] = { 0x202ull, elapsed_ms / 2.0 };
        return snapshot;
    }

    auto record_and_submit(
        RHI::GpuTimingFrameTracker& tracker,
        uint64_t frame_index,
        uint64_t completion_value) -> RHI::GpuTimingFrameHandle
    {
        RHI::GpuTimingFrameHandle handle{};
        REQUIRE(tracker.begin_recording(frame_index, handle) == RHI::GpuTimingResult::Success);
        REQUIRE(tracker.mark_submitted(handle, completion_value) == RHI::GpuTimingResult::Success);
        return handle;
    }
}

TEST_CASE("GPU timing tracker materializes a completed slot before reusing it")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle slot{};
    RHI::GpuTimingFrameHandle second_slot{};
    RHI::GpuTimingFrameHandle reused_slot{};
    RHI::GpuTimingFrameSnapshot snapshot{};

    CHECK(tracker.begin_recording(17, slot) == RHI::GpuTimingResult::Success);
    CHECK(tracker.mark_submitted(slot, 91) == RHI::GpuTimingResult::Success);
    CHECK(tracker.begin_recording(18, second_slot) == RHI::GpuTimingResult::Success);
    CHECK(second_slot != slot);
    CHECK(tracker.mark_completed(slot, make_snapshot(17, 2.5)) == RHI::GpuTimingResult::Success);
    CHECK(tracker.begin_recording(19, reused_slot) == RHI::GpuTimingResult::Success);
    CHECK(reused_slot.slot_index == slot.slot_index);
    CHECK(reused_slot.generation != slot.generation);

    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
    CHECK(snapshot.submitted_frame_index == 17);
    CHECK(snapshot.frame_elapsed_ms == doctest::Approx(2.5));
    CHECK(snapshot.scope_count == 2);
    CHECK(snapshot.overflowed);
    CHECK(snapshot.scopes[0].stable_name_hash == 0x101ull);
    CHECK(snapshot.scopes[1].elapsed_ms == doctest::Approx(1.25));
    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);
}

TEST_CASE("GPU timing tracker exposes submitted query metadata")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle handle{};
    RHI::GpuTimingSubmissionInfo submission{};

    REQUIRE(tracker.begin_recording(30, handle) == RHI::GpuTimingResult::Success);
    CHECK(tracker.get_submission(handle, submission) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.set_query_count(handle, 14) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.mark_submitted(handle, 700) == RHI::GpuTimingResult::Success);
    CHECK(tracker.set_query_count(handle, 15) == RHI::GpuTimingResult::InvalidState);
    REQUIRE(tracker.get_submission(handle, submission) == RHI::GpuTimingResult::Success);
    CHECK(submission.submitted_frame_index == 30);
    CHECK(submission.completion_value == 700);
    CHECK(submission.query_count == 14);
}

TEST_CASE("GPU timing tracker rejects stale generations on every handle operation")
{
    RHI::GpuTimingFrameTracker tracker;
    const auto old_handle = record_and_submit(tracker, 40, 1000);
    REQUIRE(tracker.mark_completed(old_handle, make_snapshot(40, 1.0)) == RHI::GpuTimingResult::Success);

    RHI::GpuTimingFrameHandle current_handle{};
    REQUIRE(tracker.begin_recording(41, current_handle) == RHI::GpuTimingResult::Success);
    REQUIRE(current_handle.slot_index == old_handle.slot_index);
    REQUIRE(current_handle.generation != old_handle.generation);

    RHI::GpuTimingSubmissionInfo submission{};
    CHECK(tracker.set_query_count(old_handle, 2) == RHI::GpuTimingResult::StaleHandle);
    CHECK(tracker.mark_submitted(old_handle, 1001) == RHI::GpuTimingResult::StaleHandle);
    CHECK(tracker.get_submission(old_handle, submission) == RHI::GpuTimingResult::StaleHandle);
    CHECK(tracker.mark_completed(old_handle, make_snapshot(40, 2.0)) == RHI::GpuTimingResult::StaleHandle);
    CHECK(tracker.cancel_recording(old_handle) == RHI::GpuTimingResult::StaleHandle);
    CHECK(tracker.fail(old_handle, RHI::GpuTimingResult::RecordFailed) == RHI::GpuTimingResult::StaleHandle);
}

TEST_CASE("GPU timing tracker requires strictly increasing frame indices")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle first{};
    RHI::GpuTimingFrameHandle rejected{};
    RHI::GpuTimingFrameHandle next{};

    REQUIRE(tracker.begin_recording(50, first) == RHI::GpuTimingResult::Success);
    CHECK(tracker.begin_recording(50, rejected) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.begin_recording(49, rejected) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.begin_recording(51, next) == RHI::GpuTimingResult::Success);
}

TEST_CASE("GPU timing tracker keeps four active slots unavailable")
{
    RHI::GpuTimingFrameTracker tracker;
    std::array<RHI::GpuTimingFrameHandle, RHI::kGpuTimingFrameSlotCount> handles{};

    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        REQUIRE(tracker.begin_recording(60 + index, handles[index]) == RHI::GpuTimingResult::Success);
        CHECK(handles[index].slot_index == index);
        REQUIRE(tracker.mark_submitted(handles[index], 2000 + index) == RHI::GpuTimingResult::Success);
    }

    RHI::GpuTimingFrameHandle exhausted{};
    CHECK(tracker.begin_recording(64, exhausted) == RHI::GpuTimingResult::CapacityExceeded);

    REQUIRE(tracker.mark_completed(handles[0], make_snapshot(60, 0.6)) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.begin_recording(65, exhausted) == RHI::GpuTimingResult::Success);
    CHECK(exhausted.slot_index == handles[0].slot_index);
}

TEST_CASE("GPU timing tracker rejects completion before submit and mismatched snapshots")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle handle{};

    REQUIRE(tracker.begin_recording(70, handle) == RHI::GpuTimingResult::Success);
    CHECK(tracker.mark_completed(handle, make_snapshot(70, 1.0)) == RHI::GpuTimingResult::InvalidState);
    REQUIRE(tracker.mark_submitted(handle, 3000) == RHI::GpuTimingResult::Success);
    CHECK(tracker.mark_submitted(handle, 3001) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.mark_completed(handle, make_snapshot(71, 1.0)) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.mark_completed(handle, make_snapshot(70, 1.0)) == RHI::GpuTimingResult::Success);
}

TEST_CASE("GPU timing tracker publishes completed frames in submission order")
{
    RHI::GpuTimingFrameTracker tracker;
    const auto first = record_and_submit(tracker, 80, 4000);
    const auto second = record_and_submit(tracker, 81, 4001);

    CHECK(tracker.mark_completed(second, make_snapshot(81, 8.1)) == RHI::GpuTimingResult::InvalidState);
    REQUIRE(tracker.mark_completed(first, make_snapshot(80, 8.0)) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.mark_completed(second, make_snapshot(81, 8.1)) == RHI::GpuTimingResult::Success);

    RHI::GpuTimingFrameSnapshot snapshot{};
    REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
    CHECK(snapshot.submitted_frame_index == 80);
    REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
    CHECK(snapshot.submitted_frame_index == 81);
    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);
}

TEST_CASE("GPU timing tracker blocks newer submission behind an older recording frame")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle first{};
    RHI::GpuTimingFrameHandle second{};

    REQUIRE(tracker.begin_recording(82, first) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.begin_recording(83, second) == RHI::GpuTimingResult::Success);
    CHECK(tracker.mark_submitted(second, 4101) == RHI::GpuTimingResult::InvalidState);
    REQUIRE(tracker.mark_submitted(first, 4100) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.mark_submitted(second, 4101) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.mark_completed(first, make_snapshot(82, 8.2)) == RHI::GpuTimingResult::Success);
    REQUIRE(tracker.mark_completed(second, make_snapshot(83, 8.3)) == RHI::GpuTimingResult::Success);

    RHI::GpuTimingFrameSnapshot snapshot{};
    REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
    CHECK(snapshot.submitted_frame_index == 82);
    REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
    CHECK(snapshot.submitted_frame_index == 83);
}

TEST_CASE("GPU timing tracker releases a recording-order barrier on cancel or failure")
{
    SUBCASE("cancel")
    {
        RHI::GpuTimingFrameTracker tracker;
        RHI::GpuTimingFrameHandle first{};
        RHI::GpuTimingFrameHandle second{};

        REQUIRE(tracker.begin_recording(84, first) == RHI::GpuTimingResult::Success);
        REQUIRE(tracker.begin_recording(85, second) == RHI::GpuTimingResult::Success);
        CHECK(tracker.mark_submitted(second, 4201) == RHI::GpuTimingResult::InvalidState);
        REQUIRE(tracker.cancel_recording(first) == RHI::GpuTimingResult::Success);
        CHECK(tracker.mark_submitted(second, 4201) == RHI::GpuTimingResult::Success);
    }

    SUBCASE("failure")
    {
        RHI::GpuTimingFrameTracker tracker;
        RHI::GpuTimingFrameHandle first{};
        RHI::GpuTimingFrameHandle second{};

        REQUIRE(tracker.begin_recording(86, first) == RHI::GpuTimingResult::Success);
        REQUIRE(tracker.begin_recording(87, second) == RHI::GpuTimingResult::Success);
        CHECK(tracker.mark_submitted(second, 4301) == RHI::GpuTimingResult::InvalidState);
        REQUIRE(tracker.fail(first, RHI::GpuTimingResult::RecordFailed) == RHI::GpuTimingResult::Success);
        CHECK(tracker.mark_submitted(second, 4301) == RHI::GpuTimingResult::Success);
    }
}

TEST_CASE("GPU timing tracker cancels only recording slots without publishing")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle cancelled{};
    RHI::GpuTimingFrameHandle reused{};
    RHI::GpuTimingFrameSnapshot snapshot{};

    REQUIRE(tracker.begin_recording(90, cancelled) == RHI::GpuTimingResult::Success);
    CHECK(tracker.cancel_recording(cancelled) == RHI::GpuTimingResult::Success);
    CHECK(tracker.cancel_recording(cancelled) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);

    REQUIRE(tracker.begin_recording(91, reused) == RHI::GpuTimingResult::Success);
    CHECK(reused.slot_index == cancelled.slot_index);
    REQUIRE(tracker.mark_submitted(reused, 5000) == RHI::GpuTimingResult::Success);
    CHECK(tracker.cancel_recording(reused) == RHI::GpuTimingResult::InvalidState);
}

TEST_CASE("GPU timing tracker failure releases a slot and preserves the first fatal code")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle first{};
    RHI::GpuTimingFrameHandle second{};

    REQUIRE(tracker.begin_recording(100, first) == RHI::GpuTimingResult::Success);
    CHECK(tracker.fail(first, RHI::GpuTimingResult::Success) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.fail(first, RHI::GpuTimingResult::Pending) == RHI::GpuTimingResult::InvalidState);
    CHECK(tracker.fail(first, RHI::GpuTimingResult::RecordFailed) == RHI::GpuTimingResult::Success);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::RecordFailed);

    REQUIRE(tracker.begin_recording(101, second) == RHI::GpuTimingResult::Success);
    CHECK(second.slot_index == first.slot_index);
    REQUIRE(tracker.mark_submitted(second, 6000) == RHI::GpuTimingResult::Success);
    CHECK(tracker.fail(second, RHI::GpuTimingResult::DeviceLost) == RHI::GpuTimingResult::Success);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::RecordFailed);
}

TEST_CASE("GPU timing tracker FIFO overflow keeps queued snapshots and releases the completed slot")
{
    RHI::GpuTimingFrameTracker tracker;
    RHI::GpuTimingFrameHandle overflowed{};

    for (uint32_t index = 0; index < RHI::kGpuTimingSnapshotFifoCapacity; ++index)
    {
        const uint64_t frame_index = 110 + index;
        const auto handle = record_and_submit(tracker, frame_index, 7000 + index);
        REQUIRE(
            tracker.mark_completed(handle, make_snapshot(frame_index, static_cast<double>(index))) ==
            RHI::GpuTimingResult::Success);
    }

    overflowed = record_and_submit(tracker, 118, 7008);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::Success);
    CHECK(tracker.mark_completed(overflowed, make_snapshot(118, 8.0)) == RHI::GpuTimingResult::CapacityExceeded);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::CapacityExceeded);

    RHI::GpuTimingFrameHandle released{};
    REQUIRE(tracker.begin_recording(119, released) == RHI::GpuTimingResult::Success);
    CHECK(released.slot_index == overflowed.slot_index);

    RHI::GpuTimingFrameSnapshot snapshot{};
    for (uint32_t index = 0; index < RHI::kGpuTimingSnapshotFifoCapacity; ++index)
    {
        REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
        CHECK(snapshot.submitted_frame_index == 110 + index);
        CHECK(snapshot.frame_elapsed_ms == doctest::Approx(static_cast<double>(index)));
    }
    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::CapacityExceeded);
}

TEST_CASE("GPU timing tracker FIFO preserves order and overflow contents across ring wrap")
{
    RHI::GpuTimingFrameTracker tracker;

    for (uint64_t frame_index = 200; frame_index < 206; ++frame_index)
    {
        const auto handle = record_and_submit(tracker, frame_index, 8000 + frame_index);
        REQUIRE(tracker.mark_completed(handle, make_snapshot(frame_index, frame_index / 10.0)) ==
                RHI::GpuTimingResult::Success);
    }

    RHI::GpuTimingFrameSnapshot snapshot{};
    for (uint64_t frame_index = 200; frame_index < 204; ++frame_index)
    {
        REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
        CHECK(snapshot.submitted_frame_index == frame_index);
    }

    for (uint64_t frame_index = 206; frame_index < 212; ++frame_index)
    {
        const auto handle = record_and_submit(tracker, frame_index, 8000 + frame_index);
        REQUIRE(tracker.mark_completed(handle, make_snapshot(frame_index, frame_index / 10.0)) ==
                RHI::GpuTimingResult::Success);
    }

    const auto overflowed = record_and_submit(tracker, 212, 8212);
    CHECK(tracker.mark_completed(overflowed, make_snapshot(212, 21.2)) ==
          RHI::GpuTimingResult::CapacityExceeded);

    for (uint64_t frame_index = 204; frame_index < 212; ++frame_index)
    {
        REQUIRE(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Success);
        CHECK(snapshot.submitted_frame_index == frame_index);
        CHECK(snapshot.frame_elapsed_ms == doctest::Approx(frame_index / 10.0));
    }
    CHECK(tracker.try_publish(snapshot) == RHI::GpuTimingResult::Pending);
    CHECK(tracker.sticky_failure() == RHI::GpuTimingResult::CapacityExceeded);
}
