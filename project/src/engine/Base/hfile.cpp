#pragma once
#include "hplatform.h"
#include "hmemory.h"
#include "hassert.h"
#include "hstring.h"
#include "hfile.h"

#if defined(_WIN64)
#include <windows.h>
#else
#define MAX_PATH 65536
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <string.h>
namespace HASHEAENGINE
{
	static long FileGetSize(FileHandle f) {
		long fileSizeSigned;

		fseek(f, 0, SEEK_END);
		fileSizeSigned = ftell(f);
		fseek(f, 0, SEEK_SET);

		return fileSizeSigned;
	}
	
	static bool StringEndsWithChar(const char* s, char c) {
		cstring last_entry = strrchr(s, c);
		const size_t index = last_entry - s;
		return index == (strlen(s) - 1);
	}

	auto HASHEAENGINE::FileReadBinary(const char* fileName , Allocator* allocator, size_t* size) -> char*
	{
		char* outData = 0;
		FILE* file = fopen(fileName, "rb");
		if (file)
		{
			size_t fileSize = FileGetSize(file);
			outData = (char*)Hashea_Alloc(allocator,fileSize + 1 ,1);
			fread(outData, fileSize,1,file);
			outData[fileSize] = 0;
			if (size)
			{
				*size = fileSize;
			}
			fclose(file);
		}
		return outData;
	}
	auto FileReadText(const char* fileName , Allocator* allocator, size_t* size) -> char*
	{
		char* text = 0;
		FILE* file = fopen(fileName, "r");
		if (file)
		{
			size_t fileSize = FileGetSize(file);
			text = (char*)Hashea_Alloc(allocator, fileSize + 1, 1);
			size_t bytes_read = fread(text, fileSize, 1, file);
			text[bytes_read] = 0;
			if (size)
			{
				*size = fileSize;
			}
			fclose(file);
		}
		return text;
	}
	auto FileReadBinary(const char* fileName , Allocator* allocator) -> FileReadResult
	{
		FileReadResult result{ nullptr, 0 };
		FILE* file = fopen(fileName, "rb");
		if (file)
		{
			size_t fileSize = FileGetSize(file);
			result.data = (char*)Hashea_Alloc(allocator, fileSize + 1, 1);
			fread(result.data, fileSize, 1, file);
			result.data[fileSize] = 0;//waste anyway
			result.size = fileSize;
			fclose(file);
		}
		return result;
	}
	auto FileReadText(const char* fileName , Allocator* allocator) -> FileReadResult
	{
		FileReadResult result{ nullptr, 0 };
		FILE* file = fopen(fileName, "r");
		if (file)
		{
			size_t fileSize = FileGetSize(file);
			result.data = (char*)Hashea_Alloc(allocator, fileSize + 1, 1);
			size_t byteRead = fread(result.data, fileSize, 1, file);
			result.data[byteRead] = 0;
			result.size = byteRead;
			fclose(file);
		}
		return result;
	}
	auto FileWriteBinary(const char* fileName, void* memory, size_t size) -> void
	{
		FILE* file = fopen(fileName, "wb");
		fwrite(memory, size, 1, file);
		fclose(file);
	}
	auto FileExists(const char* fileName) -> bool
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
	auto FileOpen(const char* fileName, const char* mode, FileHandle* file) -> void
	{
#if defined(_WIN64)
		fopen_s(file, fileName, mode);
#else
		*file = fopen(fileName, mode);
#endif
	}
	auto FileClose(FileHandle file) -> void
	{
		if (file)
			fclose(file);
	}
	auto FileWrite(uint8_t* memory, uint32_t elementSize, uint32_t count, FileHandle file) -> size_t
	{
		return fwrite(memory, elementSize, count, file);
	}
	auto FileDelete(const char* filePath) -> bool
	{
#if defined(_WIN64)
		int result = remove(filePath);
		return result != 0;
#else
		int result = remove(filePath);
		return (result == 0);
#endif
	}
#if defined(_WIN64)
	auto FileLastWriteTime(const char* filename) -> FileTime
	{
		FILETIME lastWriteTime = {};

		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (GetFileAttributesExA(filename, GetFileExInfoStandard, &data)) {
			lastWriteTime.dwHighDateTime = data.ftLastWriteTime.dwHighDateTime;
			lastWriteTime.dwLowDateTime = data.ftLastWriteTime.dwLowDateTime;
		}

		return lastWriteTime;
	}
#endif

	auto FileResolveToFullPath(const char* path, char* outFullPath, uint32_t maxSize) -> uint32_t
	{
#if defined(_WIN64)
		return GetFullPathNameA(path, maxSize, outFullPath, nullptr);
#else
		return readlink(path, outFullPath, maxSize);
#endif // _WIN64;
	}
	auto FileDirectoryFromPath(char* path) -> void
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
	auto FileNameFromPath(char* path) -> void
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

	auto FileExtensionFromPath(char* path) -> char*
	{
		char* last_separator = strrchr(path, '.');

		return last_separator + 1;
	}
	auto DirectoryExists(const char* path) -> bool
	{
#if defined(_WIN64)
		WIN32_FILE_ATTRIBUTE_DATA unused;
		return GetFileAttributesExA(path, GetFileExInfoStandard, &unused);
#else
		int result = access(path, F_OK);
		return (result == 0);
#endif // _WIN64
	}
	auto DirectoryCreate(const char* path) -> bool
	{
#if defined(_WIN64)
		int result = CreateDirectoryA(path, NULL);
		return result != 0;
#else
		int result = mkdir(path, S_IRWXU | S_IRWXG);
		return (result == 0);
#endif // _WIN64
	}
	auto DirectoryDelete(const char* path) -> bool
	{
#if defined(_WIN64)
		int result = RemoveDirectoryA(path);
		return result != 0;
#else
		int result = rmdir(path);
		return (result == 0);
#endif // _WIN64
	}
	auto DirectoryCurrent(Directory* directory) -> void
	{
#if defined(_WIN64)
		DWORD written_chars = GetCurrentDirectoryA(k_max_path, directory->path);
		H_ASSERT(written_chars < k_max_path);
		directory->path[written_chars] = 0;
#else
		getcwd(directory->path, k_max_path);
#endif // _WIN64
	}
	auto DirectoryChange(const char* path) -> HS_Result
	{
#if defined(_WIN64)
		if (!SetCurrentDirectoryA(path)) {
			HLogInfo("Cannot change current directory to {}\n", path);
			return HS_FAIL;
		}
		
#else
		if (chdir(path) != 0) {
			HLogInfo("Cannot change current directory to{ }\n", path);
			return HS_FAIL;

		}
#endif // _WIN64
		return HS_OK;
	}
	auto FileOpenDirectory(const char* path, Directory* outDirectory) -> HS_Result
	{
		// Open file trying to conver to full path instead of relative.
		// If an error occurs, just copy the name.
		if (FileResolveToFullPath(path, outDirectory->path, MAX_PATH) == 0) {
			strcpy(outDirectory->path, path);
		}

		// Add '\\' if missing
		if (!StringEndsWithChar(path, '\\')) {
			strcat(outDirectory->path, "\\");
		}

		if (!StringEndsWithChar(outDirectory->path, '*')) {
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
			return HS_FAIL;
		}
#else
		H_ASSERTLOG(false, "Platform Not implemented");
		return HS_FAIL;
#endif
		return HS_OK;
	}
	auto FileCloseDirectory(Directory* directory) -> HS_Result
	{
#if defined(_WIN64)
		if (directory->os_handle) {
			if (!FindClose(directory->os_handle))
			{
				HLogWarning("Failed to Close Directory!");
				return HS_FAIL;
			}
		}
#else
		H_ASSERTLOG(false, "Not implemented");
#endif
		return HS_OK;
	}
	auto FileParentDirectory(Directory* directory) -> void
	{
		Directory new_directory;

		const char* last_directory_separator = strrchr(directory->path, '\\');
		size_t index = last_directory_separator - directory->path;

		if (index > 0) {

			strncpy(new_directory.path, directory->path, index);
			new_directory.path[index] = 0;

			last_directory_separator = strrchr(new_directory.path, '\\');
			size_t second_index = last_directory_separator - new_directory.path;

			if (last_directory_separator) {
				new_directory.path[second_index] = 0;
			}
			else {
				new_directory.path[index] = 0;
			}

			HS_Result ret = FileOpenDirectory(new_directory.path, &new_directory);
			if (HS_CHECK_FAILED(ret))
			{
				HLogError("Failed to open directory : {}", new_directory.path);
				return;
			}
#if defined(_WIN64)
			// Update directory
			if (new_directory.os_handle) {
				*directory = new_directory;
			}
#else
			RASSERTM(false, "Not implemented");
#endif
		}
	}
	auto FileSubDirectory(Directory* directory, const char* subDirectoryName) -> HS_Result
	{
		// Remove the last '*' from the path. It will be re-added by the file_open.
		if (StringEndsWithChar(directory->path, '*')) {
			directory->path[strlen(directory->path) - 1] = 0;
		}

		strcat(directory->path, subDirectoryName);
		HS_Result ret = FileOpenDirectory(directory->path, directory);
		if (HS_CHECK_FAILED(ret))
		{
			HLogError("Failed to open directory : {}", directory->path);
			return HS_FAIL;
		}
		return HS_OK;
	}
	auto FileFindFilesInPath(const char* filePattern, StringArray& files) -> void
	{

		files.Clear();

#if defined(_WIN64)
		WIN32_FIND_DATAA find_data;
		HANDLE hFind;
		if ((hFind = FindFirstFileA(filePattern, &find_data)) != INVALID_HANDLE_VALUE) {
			do {

				files.Intern(find_data.cFileName);

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
	auto FileFindFilesInPath(const char* extension, const char* searchPattern, StringArray& files, StringArray& directories) -> void
	{
		files.Clear();
		directories.Clear();

#if defined(_WIN64)
		WIN32_FIND_DATAA find_data;
		HANDLE hFind;
		if ((hFind = FindFirstFileA(searchPattern, &find_data)) != INVALID_HANDLE_VALUE) {
			do {
				if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					directories.Intern(find_data.cFileName);
				}
				else {
					// If filename contains the extension, add it
					if (strstr(find_data.cFileName, extension)) {
						files.Intern(find_data.cFileName);
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

	auto EnvironmentVariableGet(const char* name, char* output, uint32_t outputSize) -> void
	{
#if defined(_WIN64)
		ExpandEnvironmentStringsA(name, output, outputSize);
#else
		const char* real_output = getenv(name);
		strncpy(output, real_output, outputSize);
#endif
	}

	ScopedFile::ScopedFile(const char* fileName, const char* mode)
	{
		FileOpen(fileName, mode, &file);
	}

	ScopedFile::~ScopedFile()
	{
		FileClose(file);
	}

};