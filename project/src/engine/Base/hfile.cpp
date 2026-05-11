#pragma once
#include "hplatform.h"
#include "hmemory.h"
#include "hassert.h"
#include "hstring.h"
#include "hfile.h"
#include <filesystem>
#if defined(_WIN64)
#include <windows.h>
#else
#define MAX_PATH 65536
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <string.h>
namespace AshEngine
{
	static long file_get_size(FileHandle f) {
		long fileSizeSigned;

		fseek(f, 0, SEEK_END);
		fileSizeSigned = ftell(f);
		fseek(f, 0, SEEK_SET);

		return fileSizeSigned;
	}
	
	static bool string_ends_with_char(const char* s, char c) {
		if (!s || s[0] == 0)
		{
			return false;
		}
		cstring last_entry = strrchr(s, c);
		if (!last_entry)
		{
			return false;
		}
		return last_entry == (s + strlen(s) - 1);
	}

	static auto file_release_read_buffer(char* data, Allocator* allocator) -> void
	{
		if (!data)
		{
			return;
		}
		if (allocator)
		{
			allocator->deallocate(data);
			return;
		}
		MemoryService::instance()->get_system_allocator()->deallocate(data);
	}

	auto file_read_binary(const char* fileName , size_t& size, Allocator* allocator) -> char*
	{
		size = 0;
		if (!fileName)
		{
			return nullptr;
		}
		char* outData = 0;
		FILE* file = fopen(fileName, "rb");
		if (file)
		{
			const long fileSizeSigned = file_get_size(file);
			if (fileSizeSigned < 0)
			{
				fclose(file);
				return nullptr;
			}
			size_t fileSize = static_cast<size_t>(fileSizeSigned);
			outData = (char*)Ash_Alloc(allocator,fileSize + 1 ,1);
			if (!outData)
			{
				fclose(file);
				return nullptr;
			}
			const size_t bytesRead = fread(outData, 1,fileSize,file);
			if (bytesRead == fileSize)
			{
				outData[fileSize] = 0;
				size = fileSize;
			}
			else
			{
				file_release_read_buffer(outData, allocator);
				outData = nullptr;
			}
			fclose(file);
		}
		return outData;
	}
	auto file_read_text(const char* fileName , size_t& size, Allocator* allocator) -> char*
	{
		size = 0;
		if (!fileName)
		{
			return nullptr;
		}
		char* text = 0;
		FILE* file = fopen(fileName, "r");
		if (file)
		{
			const long fileSizeSigned = file_get_size(file);
			if (fileSizeSigned < 0)
			{
				fclose(file);
				return nullptr;
			}
			size_t fileSize = static_cast<size_t>(fileSizeSigned);
			text = (char*)Ash_Alloc(allocator, fileSize + 1, 1);
			if (!text)
			{
				fclose(file);
				return nullptr;
			}
			size_t bytes_read = fread(text, 1, fileSize, file);
			if (bytes_read == fileSize || feof(file))
			{
				text[bytes_read] = 0;
				size = bytes_read;
			}
			else
			{
				file_release_read_buffer(text, allocator);
				text = nullptr;
			}
			fclose(file);
		}
		return text;
	}
	auto file_read_binary(const char* fileName , Allocator* allocator) -> FileReadResult
	{
		FileReadResult result{ nullptr, 0 };
		if (!fileName)
		{
			return result;
		}
		FILE* file = fopen(fileName, "rb");
		if (file)
		{
			const long fileSizeSigned = file_get_size(file);
			if (fileSizeSigned < 0)
			{
				fclose(file);
				return result;
			}
			size_t fileSize = static_cast<size_t>(fileSizeSigned);
			result.data = (char*)Ash_Alloc(allocator, fileSize + 1, 1);
			if (!result.data)
			{
				fclose(file);
				return result;
			}
			const size_t bytesRead = fread(result.data, 1, fileSize, file);
			if (bytesRead == fileSize)
			{
				result.data[fileSize] = 0;//waste anyway
				result.size = fileSize;
			}
			else
			{
				file_release_read_buffer(result.data, allocator);
				result.data = nullptr;
			}
			fclose(file);
		}
		return result;
	}
	auto file_read_text(const char* fileName , Allocator* allocator) -> FileReadResult
	{
		FileReadResult result{ nullptr, 0 };
		if (!fileName)
		{
			return result;
		}
		FILE* file = fopen(fileName, "r");
		if (file)
		{
			const long fileSizeSigned = file_get_size(file);
			if (fileSizeSigned < 0)
			{
				fclose(file);
				return result;
			}
			size_t fileSize = static_cast<size_t>(fileSizeSigned);
			result.data = (char*)Ash_Alloc(allocator, fileSize + 1, 1);
			if (!result.data)
			{
				fclose(file);
				return result;
			}
			size_t byteRead = fread(result.data, 1, fileSize, file);
			if (byteRead == fileSize || feof(file))
			{
				result.data[byteRead] = 0;
				result.size = byteRead;
			}
			else
			{
				file_release_read_buffer(result.data, allocator);
				result.data = nullptr;
			}
			fclose(file);
		}
		return result;
	}
	auto file_write_binary(const char* fileName, void* memory, size_t size) -> void
	{
		if (!fileName || (!memory && size > 0))
		{
			HLogError("Failed to write binary file: invalid input.");
			return;
		}
		FILE* file = fopen(fileName, "wb");
		if (file)
		{
			const size_t written = fwrite(memory, 1, size, file);
			if (written != size)
			{
				HLogError("Short write to file : {0} ! requested={1}, written={2}", fileName, size, written);
			}
			fclose(file);
		}
		else
		{
			HLogError("Failed to write to file : {0} ! ", fileName);
		}
		
	}
	auto file_exists(const char* fileName) -> bool
	{
#if defined(_WIN64)
		WIN32_FILE_ATTRIBUTE_DATA unused;
		return GetFileAttributesExA(fileName, GetFileExInfoStandard, &unused);
#else
		int result = access(fileName, F_OK);
		return (result == 0);
#endif // _WIN64
		return false;
	}
	auto file_open(const char* fileName, const char* mode, FileHandle* file) -> void
	{
#if defined(_WIN64)
		fopen_s(file, fileName, mode);
#else
		*file = fopen(fileName, mode);
#endif
	}
	auto file_close(FileHandle file) -> void
	{
		if (file)
			fclose(file);
	}
	auto file_write(uint8_t* memory, uint32_t elementSize, uint32_t count, FileHandle file) -> size_t
	{
		return fwrite(memory, elementSize, count, file);
	}
	auto file_delete(const char* filePath) -> bool
	{
		if (!filePath)
		{
			return false;
		}
#if defined(_WIN64)
		int result = remove(filePath);
		return result == 0;
#else
		int result = remove(filePath);
		return (result == 0);
#endif
	}
#if defined(_WIN64)
	auto file_last_write_time(const char* filename) -> FileTime
	{
		ASHFILETIME lastWriteTime = {};

		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (GetFileAttributesExA(filename, GetFileExInfoStandard, &data)) {
			lastWriteTime.dwHighDateTime = data.ftLastWriteTime.dwHighDateTime;
			lastWriteTime.dwLowDateTime = data.ftLastWriteTime.dwLowDateTime;
		}

		return lastWriteTime;
	}
#endif

	auto file_resolve_to_full_path(const char* path, char* outFullPath, uint32_t maxSize) -> uint32_t
	{
#if defined(_WIN64)
		return GetFullPathNameA(path, maxSize, outFullPath, nullptr);
#else
		return readlink(path, outFullPath, maxSize);
#endif // _WIN64;
	}
	auto file_directory_from_path(char* path) -> void
	{
		char* last_point = strrchr(path, '.');
		char* last_separator = strrchr(path, '/');
		if (last_separator != nullptr && last_point > last_separator) {
			*(last_separator + 1) = 0;
		}
		else {
			// Try searching backslash
			last_separator = strrchr(path, '\\');
			if (last_separator != nullptr && last_point > last_separator) {
				*(last_separator + 1) = 0;
			}
			else {
				// Wrong input!
				H_ASSERTLOG(false, "Malformed path %s!", path);
			}

		}
	}
	auto file_name_from_path(char* path) -> void
	{
		char* last_separator = strrchr(path, '/');
		if (last_separator == nullptr) {
			last_separator = strrchr(path, '\\');
		}

		if (last_separator != nullptr) {
			size_t name_length = strlen(last_separator + 1);

			memcpy(path, last_separator + 1, name_length);
			path[name_length] = 0;
		}
	}

	auto file_extension_from_path(char* path) -> char*
	{
		char* last_separator = strrchr(path, '.');
		if (!last_separator)
		{
			return nullptr;
		}
		return last_separator + 1;
	}
	auto directory_exists(const char* path) -> bool
	{
#if defined(_WIN64)
		WIN32_FILE_ATTRIBUTE_DATA unused;
		return GetFileAttributesExA(path, GetFileExInfoStandard, &unused);
#else
		int result = access(path, F_OK);
		return (result == 0);
#endif // _WIN64
	}
	auto directory_create(const char* path) -> bool
	{
//#if defined(_WIN64)
//		int result = CreateDirectoryA(path, NULL);
//		return result != 0;
//#else
//		int result = mkdir(path, S_IRWXU | S_IRWXG);
//		return (result == 0);
//#endif // _WIN64
		try {
			return std::filesystem::create_directories(path);
		}
		catch (const std::filesystem::filesystem_error& e) {
			std::cerr << "Failed to create directories !: " << e.what() << std::endl;
			return false;
		}
	}
	auto directory_delete(const char* path) -> bool
	{
#if defined(_WIN64)
		int result = RemoveDirectoryA(path);
		return result != 0;
#else
		int result = rmdir(path);
		return (result == 0);
#endif // _WIN64
	}
	auto directory_current(Directory* directory) -> void
	{
#if defined(_WIN64)
		DWORD written_chars = GetCurrentDirectoryA(k_max_path, directory->path);
		H_ASSERT(written_chars < k_max_path);
		directory->path[written_chars] = 0;
#else
		getcwd(directory->path, k_max_path);
#endif // _WIN64
	}
	auto directory_change(const char* path) -> bool
	{
#if defined(_WIN64)
		if (!SetCurrentDirectoryA(path)) {
			HLogInfo("Cannot change current directory to {}\n", path);
			return false;
		}
		
#else
		if (chdir(path) != 0) {
			HLogInfo("Cannot change current directory to{ }\n", path);
			return HS_FAIL;

		}
#endif // _WIN64
		return true;
	}
	auto file_open_directory(const char* path, Directory* outDirectory) -> bool
	{
		// Open file trying to conver to full path instead of relative.
		// If an error occurs, just copy the name.
		if (file_resolve_to_full_path(path, outDirectory->path, MAX_PATH) == 0) {
			strcpy(outDirectory->path, path);
		}

		// Add '\\' if missing
		if (!string_ends_with_char(path, '\\')) {
			strcat(outDirectory->path, "\\");
		}

		if (!string_ends_with_char(outDirectory->path, '*')) {
			strcat(outDirectory->path, "*");
		}

#if defined(_WIN64)
		outDirectory->os_handle = nullptr;
		WIN32_FIND_DATAA find_data;
		HANDLE found_handle;
		if ((found_handle = FindFirstFileA(outDirectory->path, &find_data)) != INVALID_HANDLE_VALUE) {
			outDirectory->os_handle = found_handle;
		}
		else {
			HLogWarning("Could not open directory {}\n", outDirectory->path);
			return false;
		}
#else
		H_ASSERTLOG(false, "Platform Not implemented");
		return HS_FAIL;
#endif
		return true;
	}
	auto file_close_directory(Directory* directory) -> bool
	{
#if defined(_WIN64)
		if (directory->os_handle) {
			if (!FindClose(directory->os_handle))
			{
				HLogWarning("Failed to Close Directory!");
				return false;
			}
		}
#else
		H_ASSERTLOG(false, "Not implemented");
#endif
		return true;
	}
	auto file_parent_directory(Directory* directory) -> void
	{
		if (!directory || directory->path[0] == 0)
		{
			return;
		}

		std::filesystem::path current_path(directory->path);
		if (current_path.filename() == "*")
		{
			current_path = current_path.parent_path();
		}

		const std::filesystem::path parent_path = current_path.parent_path();
		if (parent_path.empty() || parent_path == current_path)
		{
			return;
		}

		Directory new_directory{};
		const std::string parent_string = parent_path.string();
		bool ret = file_open_directory(parent_string.c_str(), &new_directory);
		if (!ret)
		{
			HLogError("Failed to open directory : {}", parent_string);
			return;
		}
#if defined(_WIN64)
		if (new_directory.os_handle) {
			file_close_directory(directory);
			*directory = new_directory;
		}
#else
		RASSERTM(false, "Not implemented");
#endif
	}
	auto file_sub_directory(Directory* directory, const char* subDirectoryName) -> bool
	{
		// Remove the last '*' from the path. It will be re-added by the file_open.
		if (string_ends_with_char(directory->path, '*')) {
			directory->path[strlen(directory->path) - 1] = 0;
		}

		strcat(directory->path, subDirectoryName);
		bool ret = file_open_directory(directory->path, directory);
		if (!ret)
		{
			HLogError("Failed to open directory : {}", directory->path);
			return false;
		}
		return true;
	}
	auto file_find_files_in_path(const char* filePattern, StringArray& files) -> void
	{

		files.clear();

#if defined(_WIN64)
		WIN32_FIND_DATAA find_data;
		HANDLE hFind;
		if ((hFind = FindFirstFileA(filePattern, &find_data)) != INVALID_HANDLE_VALUE) {
			do {

				files.intern(find_data.cFileName);

			} while (FindNextFileA(hFind, &find_data) != 0);
			FindClose(hFind);
		}
		else {
			HLogInfo("Cannot find file %s\n", filePattern);
		}
#else
		H_ASSERTLOG(false, "Not implemented");
#endif
	}
	auto file_find_files_in_path(const char* extension, const char* searchPattern, StringArray& files, StringArray& directories) -> void
	{
		files.clear();
		directories.clear();

#if defined(_WIN64)
		WIN32_FIND_DATAA find_data;
		HANDLE hFind;
		if ((hFind = FindFirstFileA(searchPattern, &find_data)) != INVALID_HANDLE_VALUE) {
			do {
				if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					directories.intern(find_data.cFileName);
				}
				else {
					// If filename contains the extension, add it
					if (strstr(find_data.cFileName, extension)) {
						files.intern(find_data.cFileName);
					}
				}

			} while (FindNextFileA(hFind, &find_data) != 0);
			FindClose(hFind);
		}
		else {
			HLogInfo("Cannot find directory %s\n", searchPattern);
		}
#else
		H_ASSERTLOG(false, "Not implemented");
#endif
	}

	auto env_var_get(const char* name, char* output, uint32_t outputSize) -> void
	{
#if defined(_WIN64)
		ExpandEnvironmentStringsA(name, output, outputSize);
#else
		const char* real_output = getenv(name);
		if (!real_output)
		{
			if (outputSize > 0)
			{
				output[0] = 0;
			}
			return;
		}
		strncpy(output, real_output, outputSize);
#endif
	}

	ScopedFile::ScopedFile(const char* fileName, const char* mode)
	{
		file_open(fileName, mode, &file);
	}

	ScopedFile::~ScopedFile()
	{
		file_close(file);
	}

};
