#include "Function/Asset/VegetationFileOps.h"

#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <iterator>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <processthreadsapi.h>
#endif

namespace AshEngine
{
	namespace
	{
		constexpr size_t k_maximum_stage_write_bytes = 1024u * 1024u;

		bool same_path_component(
			const std::filesystem::path& lhs,
			const std::filesystem::path& rhs) noexcept
		{
#if defined(_WIN32)
			const std::wstring& lhs_native = lhs.native();
			const std::wstring& rhs_native = rhs.native();
			if (lhs_native.empty() || rhs_native.empty())
			{
				return lhs_native.empty() && rhs_native.empty();
			}
			if (lhs_native.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
				rhs_native.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			{
				return false;
			}
			return CompareStringOrdinal(
				lhs_native.data(), static_cast<int>(lhs_native.size()),
				rhs_native.data(), static_cast<int>(rhs_native.size()), TRUE) == CSTR_EQUAL;
#else
			return lhs == rhs;
#endif
		}

		bool same_path_identity(
			const std::filesystem::path& lhs,
			const std::filesystem::path& rhs) noexcept
		{
			auto lhs_component = lhs.begin();
			auto rhs_component = rhs.begin();
			while (lhs_component != lhs.end() && rhs_component != rhs.end())
			{
				if (!same_path_component(*lhs_component, *rhs_component))
				{
					return false;
				}
				++lhs_component;
				++rhs_component;
			}
			return lhs_component == lhs.end() && rhs_component == rhs.end();
		}

		bool is_strict_lexical_descendant(
			const std::filesystem::path& child,
			const std::filesystem::path& parent)
		{
			if (child.empty() || parent.empty() || same_path_identity(child, parent))
			{
				return false;
			}
			auto child_component = child.begin();
			for (auto parent_component = parent.begin();
				parent_component != parent.end(); ++parent_component)
			{
				if (child_component == child.end() ||
					!same_path_component(*child_component, *parent_component))
				{
					return false;
				}
				++child_component;
			}
			return child_component != child.end();
		}

		bool contains_exact_path(
			const std::vector<std::filesystem::path>& paths,
			const std::filesystem::path& path)
		{
			return std::find_if(paths.begin(), paths.end(),
				[&](const std::filesystem::path& candidate)
				{
					return same_path_identity(candidate, path);
				}) != paths.end();
		}

		bool erase_exact_path(
			std::vector<std::filesystem::path>& paths,
			const std::filesystem::path& path)
		{
			const auto found = std::find_if(paths.begin(), paths.end(),
				[&](const std::filesystem::path& candidate)
				{
					return same_path_identity(candidate, path);
				});
			if (found == paths.end())
			{
				return false;
			}
			paths.erase(found);
			return true;
		}

		VegetationFileInspection failed_inspection(
			const VegetationFileResultStatus status,
			std::string error)
		{
			VegetationFileInspection result{};
			result.status = status;
			result.error = std::move(error);
			return result;
		}

		VegetationFileBytesResult failed_bytes(
			const VegetationFileResultStatus status,
			std::string error)
		{
			VegetationFileBytesResult result{};
			result.status = status;
			result.error = std::move(error);
			return result;
		}

		VegetationStageFileResult failed_stage_file(
			std::string error,
			const VegetationFileResultStatus status = VegetationFileResultStatus::Failed)
		{
			VegetationStageFileResult result{};
			result.status = status;
			result.error = std::move(error);
			return result;
		}

		VegetationStageTreeResult failed_stage_tree(
			std::string error,
			const VegetationFileResultStatus status = VegetationFileResultStatus::Failed)
		{
			VegetationStageTreeResult result{};
			result.status = status;
			result.error = std::move(error);
			return result;
		}

		VegetationFileLeaseResult failed_lease(
			const VegetationFileLeaseStatus status,
			std::string error)
		{
			VegetationFileLeaseResult result{};
			result.status = status;
			result.error = std::move(error);
			return result;
		}

#if defined(_WIN32)
		std::string win32_error(const char* action, const DWORD error)
		{
			return std::string(action ? action : "Vegetation file operation failed") +
				" (Win32 error " + std::to_string(static_cast<uint32_t>(error)) + ").";
		}

		bool is_not_found_error(const DWORD error)
		{
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		}

		bool is_collision_error(const DWORD error)
		{
			return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
		}

		std::wstring extended_path(const std::filesystem::path& path)
		{
			std::wstring value = path.native();
			if (value.rfind(L"\\\\?\\", 0) == 0)
			{
				return value;
			}
			if (value.rfind(L"\\\\", 0) == 0)
			{
				return L"\\\\?\\UNC\\" + value.substr(2);
			}
			return L"\\\\?\\" + value;
		}

		bool contains_dot_component(const std::filesystem::path& path)
		{
			for (const std::filesystem::path& component : path)
			{
				if (component == "." || component == "..")
				{
					return true;
				}
			}
			return false;
		}

		bool is_reserved_windows_component(const std::wstring& component)
		{
			std::wstring base = component.substr(0, component.find(L'.'));
			std::transform(base.begin(), base.end(), base.begin(),
				[](const wchar_t value) { return static_cast<wchar_t>(std::towupper(value)); });
			if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL")
			{
				return true;
			}
			if (base.size() == 4 && base[3] >= L'1' && base[3] <= L'9')
			{
				return base.rfind(L"COM", 0) == 0 || base.rfind(L"LPT", 0) == 0;
			}
			return false;
		}

		bool is_valid_windows_component(const std::filesystem::path& component_path)
		{
			const std::wstring component = component_path.native();
			if (component.empty() || component == L"." || component == L".." ||
				component.back() == L'.' || component.back() == L' ' ||
				is_reserved_windows_component(component))
			{
				return false;
			}
			for (const wchar_t value : component)
			{
				if (value < 32 || value == L'<' || value == L'>' || value == L':' ||
					value == L'"' || value == L'/' || value == L'\\' || value == L'|' ||
					value == L'?' || value == L'*')
				{
					return false;
				}
			}
			return true;
		}

		bool canonical_relative_path(
			const std::filesystem::path& input,
			const bool allow_empty,
			std::filesystem::path& output)
		{
			output.clear();
			if (input.empty())
			{
				return allow_empty;
			}
			if (input.is_absolute() || input.has_root_name() || input.has_root_directory() ||
				contains_dot_component(input))
			{
				return false;
			}
			const std::wstring generic = input.generic_wstring();
			if (generic.empty() || generic.front() == L'/' || generic.back() == L'/' ||
				generic.find(L"//") != std::wstring::npos)
			{
				return false;
			}
			for (const std::filesystem::path& component : input)
			{
				if (!is_valid_windows_component(component))
				{
					return false;
				}
			}
			output = input.lexically_normal();
			return !output.empty();
		}

		bool normalized_absolute_path(
			const std::filesystem::path& input,
			const bool require_absolute_input,
			std::filesystem::path& output)
		{
			output.clear();
			if (input.empty() || contains_dot_component(input) ||
				(require_absolute_input && !input.is_absolute()))
			{
				return false;
			}
			std::error_code error{};
			const std::filesystem::path absolute = std::filesystem::absolute(input, error);
			if (error || absolute.empty() || !absolute.is_absolute())
			{
				return false;
			}
			output = absolute.lexically_normal();
			return !output.empty() && output.is_absolute();
		}

		enum class PathProbeStatus : uint8_t
		{
			Present,
			Missing,
			Invalid,
			Failed
		};

		struct PathProbe
		{
			PathProbeStatus status = PathProbeStatus::Failed;
			DWORD attributes = INVALID_FILE_ATTRIBUTES;
			DWORD error = ERROR_SUCCESS;
		};

		PathProbe probe_absolute_path(const std::filesystem::path& absolute)
		{
			if (absolute.empty() || !absolute.is_absolute() || contains_dot_component(absolute))
			{
				return { PathProbeStatus::Invalid, INVALID_FILE_ATTRIBUTES, ERROR_INVALID_NAME };
			}

			std::filesystem::path current = absolute.root_path();
			if (current.empty())
			{
				return { PathProbeStatus::Invalid, INVALID_FILE_ATTRIBUTES, ERROR_INVALID_NAME };
			}
			DWORD attributes = GetFileAttributesW(extended_path(current).c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
			{
				const DWORD error = GetLastError();
				return { is_not_found_error(error) ? PathProbeStatus::Missing : PathProbeStatus::Failed,
					INVALID_FILE_ATTRIBUTES, error };
			}
			if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
				(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				return { PathProbeStatus::Invalid, attributes, ERROR_REPARSE_TAG_INVALID };
			}

			const std::filesystem::path relative = absolute.relative_path();
			size_t component_index = 0;
			const size_t component_count = static_cast<size_t>(
				std::distance(relative.begin(), relative.end()));
			for (const std::filesystem::path& component : relative)
			{
				++component_index;
				if (!is_valid_windows_component(component))
				{
					return { PathProbeStatus::Invalid, INVALID_FILE_ATTRIBUTES, ERROR_INVALID_NAME };
				}
				current /= component;
				attributes = GetFileAttributesW(extended_path(current).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES)
				{
					const DWORD error = GetLastError();
					return { is_not_found_error(error) ? PathProbeStatus::Missing : PathProbeStatus::Failed,
						INVALID_FILE_ATTRIBUTES, error };
				}
				if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					return { PathProbeStatus::Invalid, attributes, ERROR_REPARSE_TAG_INVALID };
				}
				if (component_index < component_count &&
					(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					return { PathProbeStatus::Invalid, attributes, ERROR_DIRECTORY };
				}
			}
			return { PathProbeStatus::Present, attributes, ERROR_SUCCESS };
		}

		bool existing_real_directory(
			const std::filesystem::path& input,
			const bool require_absolute_input,
			std::filesystem::path& absolute,
			std::string& error,
			VegetationFileResultStatus* out_status = nullptr)
		{
			if (out_status)
			{
				*out_status = VegetationFileResultStatus::Failed;
			}
			if (!normalized_absolute_path(input, require_absolute_input, absolute))
			{
				error = "Vegetation directory path is not canonical.";
				if (out_status)
				{
					*out_status = VegetationFileResultStatus::InvalidPath;
				}
				return false;
			}
			const PathProbe probe = probe_absolute_path(absolute);
			if (probe.status != PathProbeStatus::Present ||
				(probe.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				error = probe.status == PathProbeStatus::Invalid ?
					"Vegetation directory path contains a reparse or non-directory component." :
					win32_error("Vegetation directory is unavailable", probe.error);
				if (out_status && probe.status == PathProbeStatus::Invalid)
				{
					*out_status = VegetationFileResultStatus::InvalidPath;
				}
				return false;
			}
			if (out_status)
			{
				*out_status = VegetationFileResultStatus::Succeeded;
			}
			return true;
		}

		bool lower_utf8_path(const std::filesystem::path& path, std::string& output)
		{
			output.clear();
			const std::wstring input = path.generic_wstring();
			if (input.empty() || input.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			{
				return false;
			}
			const int input_size = static_cast<int>(input.size());
			const int lowered_size = LCMapStringEx(
				LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
				input.data(), input_size, nullptr, 0, nullptr, nullptr, 0);
			if (lowered_size <= 0)
			{
				return false;
			}
			std::wstring lowered(static_cast<size_t>(lowered_size), L'\0');
			if (LCMapStringEx(
				LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
				input.data(), input_size, lowered.data(), lowered_size,
				nullptr, nullptr, 0) != lowered_size)
			{
				return false;
			}
			const int utf8_size = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, lowered.data(), lowered_size,
				nullptr, 0, nullptr, nullptr);
			if (utf8_size <= 0)
			{
				return false;
			}
			output.resize(static_cast<size_t>(utf8_size));
			return WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, lowered.data(), lowered_size,
				output.data(), utf8_size, nullptr, nullptr) == utf8_size;
		}

		bool stage_file_name_is_owned(const std::filesystem::path& path)
		{
			const std::wstring name = path.filename().native();
			return name.rfind(L".ashveg-layer-stage-", 0) == 0;
		}

		bool stage_tree_name_is_owned(const std::filesystem::path& path)
		{
			const std::wstring name = path.filename().native();
			return name.rfind(L".ashveg-stage-tree-", 0) == 0;
		}

		class WindowsFileHandle
		{
		public:
			explicit WindowsFileHandle(const HANDLE handle = INVALID_HANDLE_VALUE)
				: m_handle(handle)
			{
			}

			~WindowsFileHandle()
			{
				if (m_handle != INVALID_HANDLE_VALUE)
				{
					CloseHandle(m_handle);
				}
			}

			WindowsFileHandle(const WindowsFileHandle&) = delete;
			WindowsFileHandle& operator=(const WindowsFileHandle&) = delete;

			HANDLE get() const noexcept { return m_handle; }

			HANDLE release() noexcept
			{
				const HANDLE handle = m_handle;
				m_handle = INVALID_HANDLE_VALUE;
				return handle;
			}

			void close() noexcept
			{
				if (m_handle != INVALID_HANDLE_VALUE)
				{
					CloseHandle(m_handle);
					m_handle = INVALID_HANDLE_VALUE;
				}
			}

		private:
			HANDLE m_handle = INVALID_HANDLE_VALUE;
		};

		bool query_file_identity(
			const HANDLE handle,
			VegetationFileIdentity& out_identity,
			DWORD* out_file_attributes = nullptr) noexcept
		{
			out_identity = {};
			if (out_file_attributes)
			{
				*out_file_attributes = INVALID_FILE_ATTRIBUTES;
			}
			if (handle == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			BY_HANDLE_FILE_INFORMATION information{};
			if (GetFileInformationByHandle(handle, &information) == FALSE)
			{
				return false;
			}
			out_identity.available = true;
			out_identity.volume_serial_number =
				static_cast<uint64_t>(information.dwVolumeSerialNumber);
			out_identity.file_index =
				(static_cast<uint64_t>(information.nFileIndexHigh) << 32u) |
				static_cast<uint64_t>(information.nFileIndexLow);
			if (out_file_attributes)
			{
				*out_file_attributes = information.dwFileAttributes;
			}
			return true;
		}

		class WindowsStageFileWriter final : public IVegetationStageFileWriter
		{
		public:
			explicit WindowsStageFileWriter(const HANDLE handle)
				: m_handle(handle)
			{
			}

			~WindowsStageFileWriter() override
			{
				close_without_flush();
			}

			bool WriteBlock(const uint64_t offset, const VegetationByteSpan bytes) override
			{
				if (m_handle == INVALID_HANDLE_VALUE || m_closed || m_failed ||
					bytes.data == nullptr || bytes.size == 0 ||
					bytes.size > k_maximum_stage_write_bytes || offset != m_next_offset ||
					bytes.size > std::numeric_limits<uint64_t>::max() - m_next_offset)
				{
					return false;
				}

				if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
				{
					m_failed = true;
					return false;
				}
				LARGE_INTEGER position{};
				position.QuadPart = static_cast<LONGLONG>(offset);
				if (!SetFilePointerEx(m_handle, position, nullptr, FILE_BEGIN))
				{
					m_failed = true;
					return false;
				}

				size_t written_total = 0;
				while (written_total < bytes.size)
				{
					const size_t remaining = bytes.size - written_total;
					const DWORD requested = static_cast<DWORD>(std::min<size_t>(
						remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
					DWORD written = 0;
					if (!WriteFile(m_handle, bytes.data + written_total, requested, &written, nullptr) ||
						written == 0 || written > requested)
					{
						m_failed = true;
						return false;
					}
					written_total += static_cast<size_t>(written);
				}
				m_next_offset += static_cast<uint64_t>(bytes.size);
				return true;
			}

			bool FlushAndClose() override
			{
				if (m_handle == INVALID_HANDLE_VALUE || m_closed)
				{
					return false;
				}
				const bool content_valid = !m_failed && m_next_offset != 0;
				const bool flushed = content_valid && FlushFileBuffers(m_handle) != FALSE;
				const HANDLE handle = m_handle;
				m_handle = INVALID_HANDLE_VALUE;
				m_closed = true;
				const bool closed = CloseHandle(handle) != FALSE;
				return content_valid && flushed && closed;
			}

		private:
			void close_without_flush() noexcept
			{
				if (m_handle != INVALID_HANDLE_VALUE)
				{
					CloseHandle(m_handle);
					m_handle = INVALID_HANDLE_VALUE;
				}
				m_closed = true;
			}

			HANDLE m_handle = INVALID_HANDLE_VALUE;
			uint64_t m_next_offset = 0;
			bool m_failed = false;
			bool m_closed = false;
		};

		class WindowsNamedMutexLease final : public IVegetationFileLease
		{
		public:
			explicit WindowsNamedMutexLease(const HANDLE mutex)
				: m_mutex(mutex)
			{
			}

			~WindowsNamedMutexLease() override
			{
				if (m_mutex != nullptr)
				{
					ReleaseMutex(m_mutex);
					CloseHandle(m_mutex);
				}
			}

		private:
			HANDLE m_mutex = nullptr;
		};

		std::atomic<uint64_t> g_stage_nonce{ 1 };

		std::wstring unique_name(
			const wchar_t* prefix,
			const uint64_t operation_serial,
			const uint64_t nonce,
			const wchar_t* suffix)
		{
			return std::wstring(prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" +
				std::to_wstring(operation_serial) + L"-" + std::to_wstring(nonce) + suffix;
		}

		VegetationStageFileResult create_stage_file_at(
			const std::filesystem::path& absolute_path,
			DWORD* out_win32_error = nullptr)
		{
			if (out_win32_error)
			{
				*out_win32_error = ERROR_SUCCESS;
			}
			WindowsFileHandle handle(CreateFileW(
				extended_path(absolute_path).c_str(), GENERIC_WRITE,
				0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (handle.get() == INVALID_HANDLE_VALUE)
			{
				const DWORD error = GetLastError();
				if (out_win32_error)
				{
					*out_win32_error = error;
				}
				return failed_stage_file(win32_error(
					"Vegetation stage file create-new failed", error));
			}

			VegetationStageFileResult result{};
			try
			{
				if (!query_file_identity(handle.get(), result.file_identity))
				{
					handle.close();
					DeleteFileW(extended_path(absolute_path).c_str());
					return failed_stage_file(
						"Vegetation stage file identity query failed.");
				}
				result.owned_stage_file = absolute_path;
				auto writer = std::make_unique<WindowsStageFileWriter>(handle.get());
				handle.release();
				result.writer = std::move(writer);
				result.status = VegetationFileResultStatus::Succeeded;
				return result;
			}
			catch (...)
			{
				result.writer.reset();
				handle.close();
				DeleteFileW(extended_path(absolute_path).c_str());
				if (out_win32_error)
				{
					*out_win32_error = ERROR_NOT_ENOUGH_MEMORY;
				}
				return failed_stage_file("Vegetation stage writer allocation failed.");
			}
		}

		VegetationCreateNewStatus move_create_new(
			const std::filesystem::path& stage,
			const std::filesystem::path& target)
		{
			std::filesystem::path stage_absolute{};
			std::filesystem::path target_absolute{};
			if (!normalized_absolute_path(stage, true, stage_absolute) ||
				!normalized_absolute_path(target, true, target_absolute) ||
				stage_absolute == target_absolute)
			{
				return VegetationCreateNewStatus::Failed;
			}
			const PathProbe source_probe = probe_absolute_path(stage_absolute);
			if (source_probe.status != PathProbeStatus::Present ||
				(source_probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				return VegetationCreateNewStatus::Failed;
			}
			const PathProbe target_probe = probe_absolute_path(target_absolute);
			if (target_probe.status == PathProbeStatus::Present)
			{
				return VegetationCreateNewStatus::AlreadyExists;
			}
			if (target_probe.status != PathProbeStatus::Missing)
			{
				return VegetationCreateNewStatus::Failed;
			}
			std::filesystem::path parent{};
			std::string parent_error{};
			if (!existing_real_directory(
				target_absolute.parent_path(), true, parent, parent_error))
			{
				return VegetationCreateNewStatus::Failed;
			}
			if (MoveFileExW(
				extended_path(stage_absolute).c_str(), extended_path(target_absolute).c_str(),
				MOVEFILE_WRITE_THROUGH) != FALSE)
			{
				return VegetationCreateNewStatus::Created;
			}
			return is_collision_error(GetLastError()) ?
				VegetationCreateNewStatus::AlreadyExists : VegetationCreateNewStatus::Failed;
		}
#endif

		class DefaultVegetationFileOps final : public IVegetationFileOps
		{
		public:
			VegetationFileInspection InspectPath(
				const std::filesystem::path& asset_root,
				const std::filesystem::path& path) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path relative{};
					if (!canonical_relative_path(path, false, relative))
					{
						return failed_inspection(
							VegetationFileResultStatus::InvalidPath,
							"Vegetation path must be canonical and asset-root-relative.");
					}
					std::filesystem::path root_absolute{};
					std::string root_error{};
					VegetationFileResultStatus root_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						asset_root, false, root_absolute, root_error, &root_status))
					{
						return failed_inspection(
							root_status, std::move(root_error));
					}
					const std::filesystem::path target_absolute =
						(root_absolute / relative).lexically_normal();
					const PathProbe probe = probe_absolute_path(target_absolute);
					if (probe.status == PathProbeStatus::Invalid)
					{
						return failed_inspection(
							VegetationFileResultStatus::InvalidPath,
							"Vegetation path contains a reparse or non-directory component.");
					}
					if (probe.status == PathProbeStatus::Failed)
					{
						return failed_inspection(
							VegetationFileResultStatus::Failed,
							win32_error("Vegetation path inspection failed", probe.error));
					}
					std::string identity{};
					if (!lower_utf8_path(target_absolute, identity))
					{
						return failed_inspection(
							VegetationFileResultStatus::Failed,
							"Vegetation canonical path identity conversion failed.");
					}

					VegetationFileIdentity file_identity{};
					DWORD opened_attributes = INVALID_FILE_ATTRIBUTES;
					if (probe.status == PathProbeStatus::Present)
					{
						WindowsFileHandle identity_handle(CreateFileW(
							extended_path(target_absolute).c_str(), FILE_READ_ATTRIBUTES,
							FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							nullptr, OPEN_EXISTING,
							FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
							nullptr));
						if (identity_handle.get() == INVALID_HANDLE_VALUE ||
							!query_file_identity(
								identity_handle.get(), file_identity, &opened_attributes))
						{
							return failed_inspection(
								VegetationFileResultStatus::Failed,
								"Vegetation path file identity query failed.");
						}
						if ((opened_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
						{
							return failed_inspection(
								VegetationFileResultStatus::InvalidPath,
								"Vegetation path resolves to a reparse point.");
						}
					}

					VegetationFileInspection result{};
					result.status = VegetationFileResultStatus::Succeeded;
					result.canonical_relative_path = std::move(relative);
					result.resolved_absolute_path = target_absolute;
					result.canonical_identity = std::move(identity);
					result.file_identity = file_identity;
					result.exists = probe.status == PathProbeStatus::Present;
					result.is_regular_file = result.exists &&
						(opened_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
					return result;
				}
				catch (...)
				{
					return failed_inspection(
						VegetationFileResultStatus::Failed,
						"Vegetation path inspection failed unexpectedly.");
				}
#else
				(void)asset_root;
				(void)path;
				return failed_inspection(
					VegetationFileResultStatus::Failed,
					"Vegetation filesystem operations require Windows.");
#endif
			}

			VegetationFileBytesResult ReadAllBytes(
				const std::filesystem::path& path,
				const uint64_t max_bytes) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path absolute{};
					if (!normalized_absolute_path(path, true, absolute))
					{
						return failed_bytes(
							VegetationFileResultStatus::InvalidPath,
							"Vegetation read path must be canonical and absolute.");
					}
					const PathProbe probe = probe_absolute_path(absolute);
					if (probe.status == PathProbeStatus::Missing)
					{
						return failed_bytes(
							VegetationFileResultStatus::NotFound,
							"Vegetation file was not found.");
					}
					if (probe.status == PathProbeStatus::Invalid)
					{
						return failed_bytes(
							VegetationFileResultStatus::InvalidPath,
							"Vegetation read path contains a reparse or non-directory component.");
					}
					if (probe.status != PathProbeStatus::Present ||
						(probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						return failed_bytes(
							VegetationFileResultStatus::Failed,
							"Vegetation read target is not a regular file.");
					}

					WindowsFileHandle handle(CreateFileW(
						extended_path(absolute).c_str(), GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
							FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
					if (handle.get() == INVALID_HANDLE_VALUE)
					{
						const DWORD error = GetLastError();
						return failed_bytes(
							is_not_found_error(error) ? VegetationFileResultStatus::NotFound :
								VegetationFileResultStatus::Failed,
							win32_error("Vegetation file open failed", error));
					}
					FILE_ATTRIBUTE_TAG_INFO tag_info{};
					if (!GetFileInformationByHandleEx(
						handle.get(), FileAttributeTagInfo, &tag_info, sizeof(tag_info)))
					{
						return failed_bytes(
							VegetationFileResultStatus::Failed,
							win32_error("Vegetation read handle inspection failed", GetLastError()));
					}
					if ((tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
						(tag_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
						GetFileType(handle.get()) != FILE_TYPE_DISK)
					{
						return failed_bytes(
							VegetationFileResultStatus::InvalidPath,
							"Vegetation read handle is not a regular non-reparse file.");
					}
					LARGE_INTEGER size{};
					if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0)
					{
						return failed_bytes(
							VegetationFileResultStatus::Failed,
							win32_error("Vegetation file size query failed", GetLastError()));
					}
					const uint64_t file_size = static_cast<uint64_t>(size.QuadPart);
					if (file_size > max_bytes || file_size > static_cast<uint64_t>(
						std::numeric_limits<size_t>::max()))
					{
						return failed_bytes(
							VegetationFileResultStatus::LimitExceeded,
							"Vegetation file exceeds the bounded read limit.");
					}

					std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
					size_t total = 0;
					while (total < bytes.size())
					{
						const DWORD request = static_cast<DWORD>(std::min<size_t>(
							bytes.size() - total, 64u * 1024u));
						DWORD read = 0;
						if (!ReadFile(handle.get(), bytes.data() + total, request, &read, nullptr) ||
							read == 0 || read > request)
						{
							return failed_bytes(
								VegetationFileResultStatus::Failed,
								"Vegetation file ended or failed during bounded read.");
						}
						total += static_cast<size_t>(read);
					}
					uint8_t extra = 0;
					DWORD extra_read = 0;
					if (!ReadFile(handle.get(), &extra, 1, &extra_read, nullptr) || extra_read != 0)
					{
						return failed_bytes(
							VegetationFileResultStatus::Failed,
							"Vegetation file changed or exceeded its admitted byte snapshot.");
					}

					VegetationFileBytesResult result{};
					result.status = VegetationFileResultStatus::Succeeded;
					result.bytes = std::move(bytes);
					return result;
				}
				catch (...)
				{
					return failed_bytes(
						VegetationFileResultStatus::Failed,
						"Vegetation bounded read failed unexpectedly.");
				}
#else
				(void)path;
				(void)max_bytes;
				return failed_bytes(
					VegetationFileResultStatus::Failed,
					"Vegetation filesystem operations require Windows.");
#endif
			}

			VegetationFileResultStatus EnsureDirectoryTree(
				const std::filesystem::path& asset_root,
				const std::filesystem::path& relative_directory) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path relative{};
					if (!canonical_relative_path(relative_directory, true, relative))
					{
						return VegetationFileResultStatus::InvalidPath;
					}
					std::filesystem::path root_absolute{};
					std::string root_error{};
					VegetationFileResultStatus root_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						asset_root, false, root_absolute, root_error, &root_status))
					{
						return root_status;
					}
					if (relative.empty())
					{
						return VegetationFileResultStatus::Succeeded;
					}

					std::filesystem::path current = root_absolute;
					for (const std::filesystem::path& component : relative)
					{
						current /= component;
						DWORD attributes = GetFileAttributesW(extended_path(current).c_str());
						if (attributes == INVALID_FILE_ATTRIBUTES)
						{
							const DWORD inspect_error = GetLastError();
							if (!is_not_found_error(inspect_error))
							{
								return VegetationFileResultStatus::Failed;
							}
							if (!CreateDirectoryW(extended_path(current).c_str(), nullptr))
							{
								const DWORD create_error = GetLastError();
								if (!is_collision_error(create_error))
								{
									return VegetationFileResultStatus::Failed;
								}
							}
							attributes = GetFileAttributesW(extended_path(current).c_str());
						}
						if (attributes == INVALID_FILE_ATTRIBUTES ||
							(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
							(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
						{
							return VegetationFileResultStatus::InvalidPath;
						}
					}
					return VegetationFileResultStatus::Succeeded;
				}
				catch (...)
				{
					return VegetationFileResultStatus::Failed;
				}
#else
				(void)asset_root;
				(void)relative_directory;
				return VegetationFileResultStatus::Failed;
#endif
			}

			VegetationStageFileResult CreateUniqueSiblingStageFile(
				const std::filesystem::path& target,
				const uint64_t operation_serial) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path target_absolute{};
					if (!normalized_absolute_path(target, true, target_absolute) ||
						target_absolute.filename().empty() ||
						!is_valid_windows_component(target_absolute.filename()))
					{
						return failed_stage_file(
							"Vegetation stage target path is invalid.",
							VegetationFileResultStatus::InvalidPath);
					}
					const PathProbe target_probe = probe_absolute_path(target_absolute);
					if (target_probe.status == PathProbeStatus::Invalid ||
						target_probe.status == PathProbeStatus::Failed ||
						(target_probe.status == PathProbeStatus::Present &&
							(target_probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
					{
						return failed_stage_file(
							"Vegetation stage target is invalid.",
							target_probe.status == PathProbeStatus::Invalid ?
								VegetationFileResultStatus::InvalidPath :
								VegetationFileResultStatus::Failed);
					}
					std::filesystem::path parent{};
					std::string parent_error{};
					VegetationFileResultStatus parent_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						target_absolute.parent_path(), true, parent, parent_error, &parent_status))
					{
						return failed_stage_file(std::move(parent_error), parent_status);
					}

					for (size_t attempt = 0; attempt < 128; ++attempt)
					{
						const uint64_t nonce = g_stage_nonce.fetch_add(1, std::memory_order_relaxed);
						const std::filesystem::path stage = parent / unique_name(
							L".ashveg-layer-stage-", operation_serial, nonce, L".tmp");
						DWORD create_error = ERROR_SUCCESS;
						VegetationStageFileResult result = create_stage_file_at(stage, &create_error);
						if (result.status == VegetationFileResultStatus::Succeeded)
						{
							return result;
						}
						if (!is_collision_error(create_error))
						{
							return result;
						}
					}
					return failed_stage_file("Vegetation unique sibling stage attempts were exhausted.");
				}
				catch (...)
				{
					return failed_stage_file("Vegetation sibling stage creation failed unexpectedly.");
				}
#else
				(void)target;
				(void)operation_serial;
				return failed_stage_file("Vegetation filesystem operations require Windows.");
#endif
			}

			VegetationStageTreeResult CreateUniqueStageTree(
				const std::filesystem::path& store_root,
				const uint64_t operation_serial) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path root{};
					std::string root_error{};
					VegetationFileResultStatus root_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						store_root, true, root, root_error, &root_status))
					{
						return failed_stage_tree(std::move(root_error), root_status);
					}
					for (size_t attempt = 0; attempt < 128; ++attempt)
					{
						const uint64_t nonce = g_stage_nonce.fetch_add(1, std::memory_order_relaxed);
						const std::filesystem::path stage_root = root / unique_name(
							L".ashveg-stage-tree-", operation_serial, nonce, L"");
						if (CreateDirectoryW(extended_path(stage_root).c_str(), nullptr))
						{
							try
							{
								VegetationStageTreeResult result{};
								result.owned_stage_root = stage_root;
								result.status = VegetationFileResultStatus::Succeeded;
								return result;
							}
							catch (...)
							{
								RemoveDirectoryW(extended_path(stage_root).c_str());
								return failed_stage_tree(
									"Vegetation stage tree result allocation failed.");
							}
						}
						const DWORD create_error = GetLastError();
						if (!is_collision_error(create_error))
						{
							return failed_stage_tree(win32_error(
								"Vegetation stage tree create-new failed", create_error));
						}
					}
					return failed_stage_tree("Vegetation unique stage tree attempts were exhausted.");
				}
				catch (...)
				{
					return failed_stage_tree("Vegetation stage tree creation failed unexpectedly.");
				}
#else
				(void)store_root;
				(void)operation_serial;
				return failed_stage_tree("Vegetation filesystem operations require Windows.");
#endif
			}

			VegetationStageFileResult CreateOwnedStageFile(
				const std::filesystem::path& owned_stage_root,
				const std::filesystem::path& relative_path) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path root{};
					std::string root_error{};
					VegetationFileResultStatus root_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						owned_stage_root, true, root, root_error, &root_status))
					{
						return failed_stage_file(std::move(root_error), root_status);
					}
					if (!stage_tree_name_is_owned(root))
					{
						return failed_stage_file(
							"Vegetation owned stage root is invalid.",
							VegetationFileResultStatus::InvalidPath);
					}
					std::filesystem::path relative{};
					if (!canonical_relative_path(relative_path, false, relative))
					{
						return failed_stage_file(
							"Vegetation owned stage child path is invalid.",
							VegetationFileResultStatus::InvalidPath);
					}
					const std::filesystem::path child = (root / relative).lexically_normal();
					const PathProbe child_probe = probe_absolute_path(child);
					if (child_probe.status != PathProbeStatus::Missing)
					{
						return failed_stage_file(
							"Vegetation owned stage child already exists or is invalid.",
							child_probe.status == PathProbeStatus::Invalid ?
								VegetationFileResultStatus::InvalidPath :
								VegetationFileResultStatus::Failed);
					}
					std::filesystem::path parent{};
					std::string parent_error{};
					VegetationFileResultStatus parent_status = VegetationFileResultStatus::Failed;
					if (!existing_real_directory(
						child.parent_path(), true, parent, parent_error, &parent_status))
					{
						return failed_stage_file(std::move(parent_error), parent_status);
					}
					return create_stage_file_at(child);
				}
				catch (...)
				{
					return failed_stage_file("Vegetation owned stage child creation failed unexpectedly.");
				}
#else
				(void)owned_stage_root;
				(void)relative_path;
				return failed_stage_file("Vegetation filesystem operations require Windows.");
#endif
			}

			bool RemoveOwnedStageFile(const std::filesystem::path& stage_file) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path absolute{};
					if (!normalized_absolute_path(stage_file, true, absolute) ||
						!stage_file_name_is_owned(absolute))
					{
						return false;
					}
					const PathProbe probe = probe_absolute_path(absolute);
					if (probe.status == PathProbeStatus::Missing)
					{
						return true;
					}
					if (probe.status != PathProbeStatus::Present ||
						(probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						return false;
					}
					return DeleteFileW(extended_path(absolute).c_str()) != FALSE ||
						is_not_found_error(GetLastError());
				}
				catch (...)
				{
					return false;
				}
#else
				(void)stage_file;
				return false;
#endif
			}

			bool RemoveOwnedStageTree(const std::filesystem::path& stage_root) override
			{
#if defined(_WIN32)
				try
				{
					std::filesystem::path absolute{};
					if (!normalized_absolute_path(stage_root, true, absolute) ||
						!stage_tree_name_is_owned(absolute))
					{
						return false;
					}
					const PathProbe probe = probe_absolute_path(absolute);
					if (probe.status == PathProbeStatus::Missing)
					{
						return true;
					}
					if (probe.status != PathProbeStatus::Present ||
						(probe.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
					{
						return false;
					}
					std::error_code error{};
					std::filesystem::remove_all(absolute, error);
					return !error && !std::filesystem::exists(absolute, error) && !error;
				}
				catch (...)
				{
					return false;
				}
#else
				(void)stage_root;
				return false;
#endif
			}

			VegetationCreateNewStatus PublishImmutableFromStage(
				const std::filesystem::path& stage,
				const std::filesystem::path& content_addressed_target) override
			{
#if defined(_WIN32)
				try
				{
					return move_create_new(stage, content_addressed_target);
				}
				catch (...)
				{
					return VegetationCreateNewStatus::Failed;
				}
#else
				(void)stage;
				(void)content_addressed_target;
				return VegetationCreateNewStatus::Failed;
#endif
			}

			VegetationFileLeaseResult AcquireNamedLease(
				const std::string_view canonical_identity,
				const VegetationOperationControl& control) override
			{
#if defined(_WIN32)
				try
				{
					if (canonical_identity.empty() || control.cancel_requested == nullptr ||
						control.deadline == std::chrono::steady_clock::time_point{})
					{
						return failed_lease(
							VegetationFileLeaseStatus::Failed,
							"Vegetation lease input is invalid.");
					}
					if (control.cancel_requested->load(std::memory_order_relaxed))
					{
						return failed_lease(
							VegetationFileLeaseStatus::Cancelled,
							"Vegetation lease was cancelled.");
					}
					if (std::chrono::steady_clock::now() >= control.deadline)
					{
						return failed_lease(
							VegetationFileLeaseStatus::TimedOut,
							"Vegetation lease deadline expired.");
					}

					const VegetationSha256 digest = vegetation_sha256(
						reinterpret_cast<const uint8_t*>(canonical_identity.data()),
						canonical_identity.size());
					static constexpr wchar_t k_hex[] = L"0123456789abcdef";
					std::wstring name = L"Local\\AshEngine.VegetationCommit.";
					name.reserve(name.size() + digest.size() * 2);
					for (const uint8_t byte : digest)
					{
						name.push_back(k_hex[byte >> 4]);
						name.push_back(k_hex[byte & 0x0f]);
					}
					const HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
					if (mutex == nullptr)
					{
						return failed_lease(
							VegetationFileLeaseStatus::Failed,
							win32_error("Vegetation named mutex creation failed", GetLastError()));
					}

					for (;;)
					{
						if (control.cancel_requested->load(std::memory_order_relaxed))
						{
							CloseHandle(mutex);
							return failed_lease(
								VegetationFileLeaseStatus::Cancelled,
								"Vegetation lease was cancelled while waiting.");
						}
						const auto now = std::chrono::steady_clock::now();
						if (now >= control.deadline)
						{
							CloseHandle(mutex);
							return failed_lease(
								VegetationFileLeaseStatus::TimedOut,
								"Vegetation lease timed out.");
						}
						const auto remaining = control.deadline - now;
						const auto whole_milliseconds =
							std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
						const DWORD wait_milliseconds = whole_milliseconds <= 0 ? 0u :
							static_cast<DWORD>(std::min<int64_t>(whole_milliseconds, 25));
						const DWORD wait_result = WaitForSingleObject(mutex, wait_milliseconds);
						if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_ABANDONED)
						{
							if (control.cancel_requested->load(std::memory_order_relaxed) ||
								std::chrono::steady_clock::now() >= control.deadline)
							{
								ReleaseMutex(mutex);
								CloseHandle(mutex);
								return failed_lease(
									control.cancel_requested->load(std::memory_order_relaxed) ?
										VegetationFileLeaseStatus::Cancelled :
										VegetationFileLeaseStatus::TimedOut,
									"Vegetation lease became inactive at acquisition.");
							}
							VegetationFileLeaseResult result{};
							result.status = VegetationFileLeaseStatus::Acquired;
							try
							{
								result.lease = std::make_unique<WindowsNamedMutexLease>(mutex);
							}
							catch (...)
							{
								ReleaseMutex(mutex);
								CloseHandle(mutex);
								return failed_lease(
									VegetationFileLeaseStatus::Failed,
									"Vegetation lease allocation failed.");
							}
							return result;
						}
						if (wait_result == WAIT_FAILED)
						{
							const DWORD error = GetLastError();
							CloseHandle(mutex);
							return failed_lease(
								VegetationFileLeaseStatus::Failed,
								win32_error("Vegetation named mutex wait failed", error));
						}
						if (wait_result != WAIT_TIMEOUT)
						{
							CloseHandle(mutex);
							return failed_lease(
								VegetationFileLeaseStatus::Failed,
								"Vegetation named mutex returned an unexpected wait status.");
						}
					}
				}
				catch (...)
				{
					return failed_lease(
						VegetationFileLeaseStatus::Failed,
						"Vegetation lease acquisition failed unexpectedly.");
				}
#else
				(void)canonical_identity;
				(void)control;
				return failed_lease(
					VegetationFileLeaseStatus::Failed,
					"Vegetation filesystem operations require Windows.");
#endif
			}

			VegetationAtomicReplaceResult AtomicReplace(
				const std::filesystem::path& stage,
				const std::filesystem::path& target,
				VegetationOwnedStageCleanupRegistry& cleanup_registry) override
			{
#if defined(_WIN32)
				auto make_result = [](const VegetationAtomicReplaceStatus status,
					std::string error = {}, std::filesystem::path recovery_path = {})
				{
					VegetationAtomicReplaceResult result{};
					result.status = status;
					result.recovery_path = std::move(recovery_path);
					result.error = std::move(error);
					return result;
				};
				auto probe_native_file = [](const std::wstring& native) noexcept
				{
					const DWORD attributes = GetFileAttributesW(native.c_str());
					if (attributes == INVALID_FILE_ATTRIBUTES)
					{
						const DWORD error = GetLastError();
						return PathProbe{
							is_not_found_error(error) ? PathProbeStatus::Missing : PathProbeStatus::Failed,
							INVALID_FILE_ATTRIBUTES, error };
					}
					if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
					{
						return PathProbe{
							PathProbeStatus::Invalid, attributes, ERROR_REPARSE_TAG_INVALID };
					}
					return PathProbe{ PathProbeStatus::Present, attributes, ERROR_SUCCESS };
				};
				auto replace_path_state = [](const PathProbe& probe) noexcept
				{
					switch (probe.status)
					{
					case PathProbeStatus::Present:
						return (probe.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ?
							VegetationReplacePathState::PresentRegular :
							VegetationReplacePathState::Invalid;
					case PathProbeStatus::Missing:
						return VegetationReplacePathState::Missing;
					case PathProbeStatus::Invalid:
						return VegetationReplacePathState::Invalid;
					case PathProbeStatus::Failed:
					default:
						return VegetationReplacePathState::ProbeFailed;
					}
				};
				std::filesystem::path backup{};
				std::filesystem::path stage_absolute{};
				std::filesystem::path target_absolute{};
				std::wstring target_native{};
				std::wstring stage_native{};
				std::wstring backup_native{};
				bool backup_recovery_protected = false;
				try
				{
					if (!normalized_absolute_path(stage, true, stage_absolute) ||
						!normalized_absolute_path(target, true, target_absolute) ||
						stage_absolute == target_absolute)
					{
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							"Vegetation atomic replace paths are invalid.");
					}
					const PathProbe stage_probe = probe_absolute_path(stage_absolute);
					const PathProbe target_probe = probe_absolute_path(target_absolute);
					if (stage_probe.status != PathProbeStatus::Present ||
						(stage_probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
						target_probe.status != PathProbeStatus::Present ||
						(target_probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							"Vegetation atomic replace requires existing regular files.");
					}
					std::filesystem::path parent{};
					std::string parent_error{};
					if (!existing_real_directory(
						target_absolute.parent_path(), true, parent, parent_error))
					{
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							std::move(parent_error));
					}

					for (size_t attempt = 0; attempt < 128; ++attempt)
					{
						const uint64_t nonce = g_stage_nonce.fetch_add(1, std::memory_order_relaxed);
						backup = parent / unique_name(
							L".ashveg-layer-stage-replace-backup-", 0, nonce, L".tmp");
						if (probe_absolute_path(backup).status != PathProbeStatus::Missing)
						{
							backup.clear();
							continue;
						}
						if (cleanup_registry.RetainStageFileForRecovery(backup))
						{
							backup_recovery_protected = true;
							break;
						}
						backup.clear();
					}
					if (backup.empty())
					{
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							"Vegetation atomic replace backup reservation failed.");
					}
					target_native = extended_path(target_absolute);
					stage_native = extended_path(stage_absolute);
					backup_native = extended_path(backup);
					if (!cleanup_registry.BeginStageFilePublish(stage_absolute))
					{
						if (cleanup_registry.ReleaseRecoveryStageFile(backup))
						{
							backup_recovery_protected = false;
							(void)cleanup_registry.CleanupStageFile(backup, *this);
						}
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							"Vegetation atomic replace could not pin the owned stage for publication.");
					}
				}
				catch (...)
				{
					if (backup_recovery_protected &&
						cleanup_registry.ReleaseRecoveryStageFile(backup))
					{
						(void)cleanup_registry.CleanupStageFile(backup, *this);
					}
					return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
						"Vegetation atomic replace failed unexpectedly before publication.");
				}

				// All paths and registry storage are prepared before this call. From here
				// onward, publication outcome transitions are allocation-free under the
				// registry lock, so cleanup cannot race the staged replacement.
				if (ReplaceFileW(target_native.c_str(), stage_native.c_str(),
					backup_native.c_str(), 0, nullptr, nullptr) != FALSE)
				{
					const bool stage_consumed = cleanup_registry.ResolveStageFilePublish(
						stage_absolute, VegetationStageFilePublishResolution::Consumed);
					if (cleanup_registry.ReleaseRecoveryStageFile(backup))
					{
						backup_recovery_protected = false;
						(void)cleanup_registry.CleanupStageFile(backup, *this);
					}
					return make_result(VegetationAtomicReplaceStatus::Replaced,
						stage_consumed ? std::string{} :
							"Vegetation atomic replace succeeded but stage ownership cleanup failed.");
				}

				const DWORD replace_error = GetLastError();
				const PathProbe failed_target_probe = probe_native_file(target_native);
				const PathProbe failed_backup_probe = probe_native_file(backup_native);
				const VegetationFailedReplaceAction failure_action =
					select_vegetation_failed_replace_action(
						static_cast<uint32_t>(replace_error),
						replace_path_state(failed_target_probe),
						replace_path_state(failed_backup_probe));

				if (failure_action == VegetationFailedReplaceAction::RestoreBackup)
				{
					if (MoveFileExW(
						backup_native.c_str(), target_native.c_str(),
						MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
					{
						const bool stage_preserved = cleanup_registry.ResolveStageFilePublish(
							stage_absolute, VegetationStageFilePublishResolution::TargetPreserved);
						if (cleanup_registry.ReleaseRecoveryStageFile(backup))
						{
							backup_recovery_protected = false;
							(void)cleanup_registry.ForgetConsumedStageFile(backup);
						}
						return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
							stage_preserved ?
								win32_error("Vegetation atomic replace failed and restored the target",
									replace_error) :
								"Vegetation atomic replace restored the target but stage state is invalid.");
					}
					(void)cleanup_registry.ResolveStageFilePublish(
						stage_absolute, VegetationStageFilePublishResolution::TargetPreserved);
					return make_result(VegetationAtomicReplaceStatus::RecoveryRequired,
						win32_error("Vegetation atomic replace retained a recovery backup",
							replace_error), backup);
				}

				if (failure_action == VegetationFailedReplaceAction::RetainBackup)
				{
					(void)cleanup_registry.ResolveStageFilePublish(
						stage_absolute, VegetationStageFilePublishResolution::TargetPreserved);
					return make_result(VegetationAtomicReplaceStatus::RecoveryRequired,
						win32_error("Vegetation atomic replace retained a recovery backup",
							replace_error), backup);
				}

				if (failure_action == VegetationFailedReplaceAction::RetainStage)
				{
					const bool stage_recovery = cleanup_registry.ResolveStageFilePublish(
						stage_absolute, VegetationStageFilePublishResolution::RecoveryRequired);
					if (stage_recovery && backup_recovery_protected &&
						cleanup_registry.ReleaseRecoveryStageFile(backup))
					{
						backup_recovery_protected = false;
						(void)cleanup_registry.CleanupStageFile(backup, *this);
					}
					return make_result(VegetationAtomicReplaceStatus::RecoveryRequired,
						stage_recovery ?
							win32_error("Vegetation atomic replace retained the replacement stage",
								replace_error) :
							"Vegetation atomic replace could not resolve its pinned recovery stage.",
						stage_absolute);
				}

				const bool stage_preserved = cleanup_registry.ResolveStageFilePublish(
					stage_absolute, VegetationStageFilePublishResolution::TargetPreserved);
				if (stage_preserved && backup_recovery_protected &&
					cleanup_registry.ReleaseRecoveryStageFile(backup))
				{
					backup_recovery_protected = false;
					(void)cleanup_registry.CleanupStageFile(backup, *this);
				}
				if (!stage_preserved)
				{
					return make_result(VegetationAtomicReplaceStatus::RecoveryRequired,
						"Vegetation atomic replace could not resolve its pinned stage.",
						stage_absolute);
				}
				return make_result(VegetationAtomicReplaceStatus::TargetPreserved,
					win32_error("Vegetation atomic replace failed with target preserved",
						replace_error));
#else
				(void)stage;
				(void)target;
				(void)cleanup_registry;
				VegetationAtomicReplaceResult result{};
				result.status = VegetationAtomicReplaceStatus::TargetPreserved;
				result.error = "Vegetation filesystem operations require Windows.";
				return result;
#endif
			}

			VegetationCreateNewStatus CreateNewFromStage(
				const std::filesystem::path& stage,
				const std::filesystem::path& target) override
			{
#if defined(_WIN32)
				try
				{
					return move_create_new(stage, target);
				}
				catch (...)
				{
					return VegetationCreateNewStatus::Failed;
				}
#else
				(void)stage;
				(void)target;
				return VegetationCreateNewStatus::Failed;
#endif
			}
		};
	}

	VegetationFailedReplaceAction select_vegetation_failed_replace_action(
		const uint32_t replace_error,
		const VegetationReplacePathState target_state,
		const VegetationReplacePathState backup_state) noexcept
	{
		constexpr uint32_t unable_to_move_replacement_2 = 1177u;
		if (replace_error == unable_to_move_replacement_2)
		{
			switch (backup_state)
			{
			case VegetationReplacePathState::PresentRegular:
				return VegetationFailedReplaceAction::RestoreBackup;
			case VegetationReplacePathState::Missing:
				return VegetationFailedReplaceAction::RetainStage;
			case VegetationReplacePathState::ProbeFailed:
			case VegetationReplacePathState::Invalid:
			default:
				return VegetationFailedReplaceAction::RetainBackup;
			}
		}
		if (target_state == VegetationReplacePathState::PresentRegular)
		{
			return VegetationFailedReplaceAction::TargetPreserved;
		}
		if (backup_state == VegetationReplacePathState::PresentRegular)
		{
			return VegetationFailedReplaceAction::RestoreBackup;
		}
		return VegetationFailedReplaceAction::RetainStage;
	}

	IVegetationFileOps& get_default_vegetation_file_ops()
	{
		static DefaultVegetationFileOps file_ops{};
		return file_ops;
	}

	struct VegetationOwnedStageCleanupRegistry::Impl
	{
		enum class StageFileState : uint8_t
		{
			Owned,
			InCleanup,
			Publishing,
			Recovery
		};

		struct StageFileEntry
		{
			std::filesystem::path path{};
			StageFileState state = StageFileState::Owned;
		};

		StageFileEntry* FindStageFile(const std::filesystem::path& path) noexcept
		{
			const auto found = std::find_if(stage_files.begin(), stage_files.end(),
				[&](const StageFileEntry& entry)
				{
					return same_path_identity(entry.path, path);
				});
			return found == stage_files.end() ? nullptr : &*found;
		}

		const StageFileEntry* FindStageFile(
			const std::filesystem::path& path) const noexcept
		{
			const auto found = std::find_if(stage_files.begin(), stage_files.end(),
				[&](const StageFileEntry& entry)
				{
					return same_path_identity(entry.path, path);
				});
			return found == stage_files.end() ? nullptr : &*found;
		}

		bool EraseStageFile(const std::filesystem::path& path)
		{
			const auto found = std::find_if(stage_files.begin(), stage_files.end(),
				[&](const StageFileEntry& entry)
				{
					return same_path_identity(entry.path, path);
				});
			if (found == stage_files.end())
			{
				return false;
			}
			stage_files.erase(found);
			return true;
		}

		mutable std::mutex mutex{};
		std::vector<StageFileEntry> stage_files{};
		std::vector<std::filesystem::path> stage_trees{};
		std::vector<std::filesystem::path> stage_trees_in_cleanup{};
	};

	VegetationOwnedStageCleanupRegistry::VegetationOwnedStageCleanupRegistry()
		: m_impl(std::make_unique<Impl>())
	{
	}

	VegetationOwnedStageCleanupRegistry::~VegetationOwnedStageCleanupRegistry() = default;

	bool VegetationOwnedStageCleanupRegistry::TrackStageFile(
		std::filesystem::path owned_stage_file)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (m_impl->FindStageFile(owned_stage_file) != nullptr ||
				contains_exact_path(m_impl->stage_trees, owned_stage_file))
			{
				return false;
			}
			if (std::any_of(
				m_impl->stage_trees.begin(), m_impl->stage_trees.end(),
				[&](const std::filesystem::path& tree)
				{
					return is_strict_lexical_descendant(owned_stage_file, tree);
				}))
			{
				return false;
			}
			m_impl->stage_files.push_back(
				{ std::move(owned_stage_file), Impl::StageFileState::Owned });
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::TrackStageTree(
		std::filesystem::path owned_stage_root)
	{
		if (owned_stage_root.empty() || !owned_stage_root.is_absolute() ||
			owned_stage_root.lexically_normal() != owned_stage_root)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (contains_exact_path(m_impl->stage_trees, owned_stage_root) ||
				m_impl->FindStageFile(owned_stage_root) != nullptr)
			{
				return false;
			}
			if (std::any_of(
				m_impl->stage_files.begin(), m_impl->stage_files.end(),
				[&](const Impl::StageFileEntry& file)
				{
					return is_strict_lexical_descendant(file.path, owned_stage_root);
				}) ||
				std::any_of(
					m_impl->stage_trees.begin(), m_impl->stage_trees.end(),
					[&](const std::filesystem::path& tree)
					{
						return is_strict_lexical_descendant(tree, owned_stage_root) ||
							is_strict_lexical_descendant(owned_stage_root, tree);
					}))
			{
				return false;
			}
			m_impl->stage_trees.push_back(std::move(owned_stage_root));
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::CleanupStageFile(
		const std::filesystem::path& owned_stage_file,
		IVegetationStageFileOps& file_ops)
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_impl->mutex);
				Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
				if (entry == nullptr || entry->state != Impl::StageFileState::Owned)
				{
					return false;
				}
				entry->state = Impl::StageFileState::InCleanup;
			}
			bool removed = false;
			try
			{
				removed = file_ops.RemoveOwnedStageFile(owned_stage_file);
			}
			catch (...)
			{
				removed = false;
			}
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr || entry->state != Impl::StageFileState::InCleanup)
			{
				return false;
			}
			if (!removed)
			{
				entry->state = Impl::StageFileState::Owned;
				return false;
			}
			return m_impl->EraseStageFile(owned_stage_file);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::CleanupStageTree(
		const std::filesystem::path& owned_stage_root,
		IVegetationStageFileOps& file_ops)
	{
		try
		{
			{
				std::lock_guard<std::mutex> lock(m_impl->mutex);
				if (!contains_exact_path(m_impl->stage_trees, owned_stage_root) ||
					contains_exact_path(m_impl->stage_trees_in_cleanup, owned_stage_root))
				{
					return false;
				}
				m_impl->stage_trees_in_cleanup.push_back(owned_stage_root);
			}
			bool removed = false;
			try
			{
				removed = file_ops.RemoveOwnedStageTree(owned_stage_root);
			}
			catch (...)
			{
				removed = false;
			}
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			erase_exact_path(m_impl->stage_trees_in_cleanup, owned_stage_root);
			if (!removed)
			{
				return false;
			}
			return erase_exact_path(m_impl->stage_trees, owned_stage_root);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::OwnsStageFile(
		const std::filesystem::path& owned_stage_file) const noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			return m_impl->FindStageFile(owned_stage_file) != nullptr;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::OwnsStageTree(
		const std::filesystem::path& owned_stage_root) const noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			return contains_exact_path(m_impl->stage_trees, owned_stage_root);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::BeginStageFilePublish(
		const std::filesystem::path& owned_stage_file) noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr || entry->state != Impl::StageFileState::Owned)
			{
				return false;
			}
			entry->state = Impl::StageFileState::Publishing;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::ResolveStageFilePublish(
		const std::filesystem::path& owned_stage_file,
		const VegetationStageFilePublishResolution resolution) noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr || entry->state != Impl::StageFileState::Publishing)
			{
				return false;
			}
			switch (resolution)
			{
			case VegetationStageFilePublishResolution::TargetPreserved:
				entry->state = Impl::StageFileState::Owned;
				return true;
			case VegetationStageFilePublishResolution::Consumed:
				return m_impl->EraseStageFile(owned_stage_file);
			case VegetationStageFilePublishResolution::RecoveryRequired:
				entry->state = Impl::StageFileState::Recovery;
				return true;
			default:
				return false;
			}
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::RetainStageFileForRecovery(
		std::filesystem::path owned_stage_file)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (contains_exact_path(m_impl->stage_trees, owned_stage_file) ||
				std::any_of(m_impl->stage_trees.begin(), m_impl->stage_trees.end(),
					[&](const std::filesystem::path& tree)
					{
						return is_strict_lexical_descendant(owned_stage_file, tree);
					}))
			{
				return false;
			}
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr)
			{
				m_impl->stage_files.push_back(
					{ std::move(owned_stage_file), Impl::StageFileState::Recovery });
				return true;
			}
			if (entry->state == Impl::StageFileState::InCleanup ||
				entry->state == Impl::StageFileState::Publishing)
			{
				return false;
			}
			entry->state = Impl::StageFileState::Recovery;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::TrackNewRecoveryStageFile(
		std::filesystem::path owned_stage_file)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (m_impl->FindStageFile(owned_stage_file) != nullptr ||
				contains_exact_path(m_impl->stage_trees, owned_stage_file) ||
				std::any_of(m_impl->stage_trees.begin(), m_impl->stage_trees.end(),
					[&](const std::filesystem::path& tree)
					{
						return is_strict_lexical_descendant(owned_stage_file, tree);
					}))
			{
				return false;
			}
			m_impl->stage_files.push_back(
				{ std::move(owned_stage_file), Impl::StageFileState::Recovery });
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::ReleaseRecoveryStageFile(
		const std::filesystem::path& owned_stage_file)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr ||
				(entry->state != Impl::StageFileState::Recovery &&
					entry->state != Impl::StageFileState::Publishing))
			{
				return false;
			}
			entry->state = Impl::StageFileState::Owned;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::IsRecoveryStageFile(
		const std::filesystem::path& owned_stage_file) const noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			const Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			return entry != nullptr &&
				(entry->state == Impl::StageFileState::Recovery ||
					entry->state == Impl::StageFileState::Publishing);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::ForgetConsumedStageFile(
		const std::filesystem::path& owned_stage_file)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr)
			{
				return true;
			}
			if (entry->state == Impl::StageFileState::InCleanup)
			{
				return true;
			}
			if (entry->state != Impl::StageFileState::Owned)
			{
				return false;
			}
			return m_impl->EraseStageFile(owned_stage_file);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::ReconcileConsumedStageFileAfterPublish(
		const std::filesystem::path& owned_stage_file,
		IVegetationStageFileOps& file_ops)
	{
		if (owned_stage_file.empty() || !owned_stage_file.is_absolute() ||
			owned_stage_file.lexically_normal() != owned_stage_file)
		{
			return false;
		}
		try
		{
			const VegetationFileBytesResult absence =
				file_ops.ReadAllBytes(owned_stage_file, 0);
			if (absence.status != VegetationFileResultStatus::NotFound ||
				!absence.bytes.empty())
			{
				return false;
			}

			std::lock_guard<std::mutex> lock(m_impl->mutex);
			Impl::StageFileEntry* entry = m_impl->FindStageFile(owned_stage_file);
			if (entry == nullptr || entry->state == Impl::StageFileState::InCleanup)
			{
				return true;
			}
			if (entry->state != Impl::StageFileState::Owned &&
				entry->state != Impl::StageFileState::Publishing)
			{
				return false;
			}
			return m_impl->EraseStageFile(owned_stage_file);
		}
		catch (...)
		{
			return false;
		}
	}

	bool VegetationOwnedStageCleanupRegistry::ForgetConsumedStageTree(
		const std::filesystem::path& owned_stage_root)
	{
		if (owned_stage_root.empty() || !owned_stage_root.is_absolute() ||
			owned_stage_root.lexically_normal() != owned_stage_root)
		{
			return false;
		}
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (!contains_exact_path(m_impl->stage_trees_in_cleanup, owned_stage_root))
			{
				erase_exact_path(m_impl->stage_trees, owned_stage_root);
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	VegetationOwnedStageCleanupStatus VegetationOwnedStageCleanupRegistry::RetryAll(
		IVegetationStageFileOps& file_ops)
	{
		VegetationOwnedStageCleanupStatus result{};
		std::vector<std::filesystem::path> stage_files{};
		std::vector<std::filesystem::path> stage_trees{};
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			stage_files.reserve(m_impl->stage_files.size());
			stage_trees.reserve(m_impl->stage_trees.size());
			for (const Impl::StageFileEntry& entry : m_impl->stage_files)
			{
				if (entry.state == Impl::StageFileState::Owned)
				{
					stage_files.push_back(entry.path);
				}
			}
			for (const std::filesystem::path& path : m_impl->stage_trees)
			{
				if (!contains_exact_path(m_impl->stage_trees_in_cleanup, path))
				{
					stage_trees.push_back(path);
				}
			}
		}
		catch (...)
		{
			result.all_removed = false;
			return result;
		}

		for (const std::filesystem::path& path : stage_files)
		{
			(void)CleanupStageFile(path, file_ops);
		}
		for (const std::filesystem::path& path : stage_trees)
		{
			(void)CleanupStageTree(path, file_ops);
		}

		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			for (const Impl::StageFileEntry& entry : m_impl->stage_files)
			{
				result.retained_stage_files.push_back(entry.path);
				if (entry.state == Impl::StageFileState::Recovery ||
					entry.state == Impl::StageFileState::Publishing)
				{
					result.retained_recovery_stage_files.push_back(entry.path);
				}
			}
			result.retained_stage_trees = m_impl->stage_trees;
		}
		result.all_removed = result.retained_stage_files.empty() &&
			result.retained_stage_trees.empty();
		return result;
	}

	bool VegetationOwnedStageCleanupRegistry::empty() const noexcept
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			return m_impl->stage_files.empty() && m_impl->stage_trees.empty();
		}
		catch (...)
		{
			return false;
		}
	}
}
