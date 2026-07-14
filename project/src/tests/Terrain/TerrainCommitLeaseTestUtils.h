#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef small
#undef small
#endif
#endif

namespace TerrainTests
{
#if defined(_WIN32)
	inline auto TerrainCommitLeaseNameForTest(const std::filesystem::path& path) -> std::wstring
	{
		std::error_code error{};
		const std::filesystem::path absolute = std::filesystem::absolute(path, error);
		if (error)
		{
			return {};
		}
		const std::filesystem::path canonical =
			std::filesystem::weakly_canonical(absolute, error);
		if (error || canonical.empty())
		{
			return {};
		}
		std::wstring identity = canonical.native();
		CharLowerBuffW(identity.data(), static_cast<DWORD>(identity.size()));
		uint64_t hash = 14695981039346656037ull;
		for (const wchar_t value : identity)
		{
			hash ^= static_cast<uint16_t>(value);
			hash *= 1099511628211ull;
		}
		constexpr wchar_t hex[] = L"0123456789abcdef";
		std::wstring name = L"Local\\AshEngine.TerrainContainer.";
		for (int shift = 60; shift >= 0; shift -= 4)
		{
			name.push_back(hex[(hash >> shift) & 0x0full]);
		}
		return name;
	}

	class ScopedTerrainCommitLeaseForTest
	{
	public:
		explicit ScopedTerrainCommitLeaseForTest(const std::filesystem::path& path)
		{
			std::promise<void> ready{};
			std::future<void> readyFuture = ready.get_future();
			std::future<void> releaseFuture = m_release.get_future();
			const std::wstring name = TerrainCommitLeaseNameForTest(path);
			m_thread = std::thread([
				this,
				name,
				ready = std::move(ready),
				releaseFuture = std::move(releaseFuture)]() mutable
			{
				HANDLE handle = name.empty() ? nullptr :
					CreateMutexW(nullptr, FALSE, name.c_str());
				if (handle != nullptr)
				{
					const DWORD waitResult = WaitForSingleObject(handle, 0u);
					m_acquired.store(
						waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED,
						std::memory_order_release);
				}
				ready.set_value();
				if (m_acquired.load(std::memory_order_acquire))
				{
					releaseFuture.wait();
					ReleaseMutex(handle);
				}
				if (handle != nullptr)
				{
					CloseHandle(handle);
				}
			});
			readyFuture.wait();
		}

		ScopedTerrainCommitLeaseForTest(const ScopedTerrainCommitLeaseForTest&) = delete;
		auto operator=(const ScopedTerrainCommitLeaseForTest&)
			-> ScopedTerrainCommitLeaseForTest& = delete;

		~ScopedTerrainCommitLeaseForTest()
		{
			m_release.set_value();
			if (m_thread.joinable())
			{
				m_thread.join();
			}
		}

		auto acquired() const noexcept -> bool
		{
			return m_acquired.load(std::memory_order_acquire);
		}

	private:
		std::promise<void> m_release{};
		std::thread m_thread{};
		std::atomic<bool> m_acquired{ false };
	};
#endif
}
