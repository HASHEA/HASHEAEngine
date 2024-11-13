#pragma once
#include "hplatform.h"
#include <stdio.h>
namespace HASHEAENGINE
{

#if defined(_WIN64)

    typedef struct __FILETIME {
        unsigned long       dwLowDateTime;
        unsigned long       dwHighDateTime;
    } FILETIME, * PFILETIME, * LPFILETIME;

    using FileTime = __FILETIME;

#endif
    using FileHandle = FILE*;
    static const uint32_t                k_max_path = 512;

    struct Directory {
        char                        path[k_max_path];

#if defined (_WIN64)
        void* os_handle;
#endif
    }; // struct Directory

    struct FileReadResult {
        char* data;
        size_t                       size;
    };

    // Read file and allocate memory from allocator.
    // User is responsible for freeing the memory.
    struct Allocator;
    struct StringArray;

    auto FileReadBinary(const char* fileName , Allocator* allocator, size_t* size) -> char*;
    auto FileReadText(const char* fileName , Allocator* allocator, size_t* size) -> char*;
    auto FileReadBinary(const char* fileName , Allocator* allocator) -> FileReadResult;
    auto FileReadText(const char* fileName , Allocator* allocator) -> FileReadResult;
    auto FileWriteBinary(const char* fileName, void* memory, size_t size) -> void;
    auto FileExists(const char* fileName) -> bool;
    auto FileOpen(const char* fileName, const char* mode, FileHandle* file) -> void;
    auto FileClose(FileHandle file)-> void;
    auto FileWrite(uint8_t* memory,uint32_t elementSize,uint32_t count,FileHandle file) -> size_t;
    auto FileDelete(const char* filePath) -> bool;
#if defined(_WIN64)
    auto FileLastWriteTime(const char* filename) -> FileTime;
#endif
    auto FileResolveToFullPath(const char* path, char* outFullPath, uint32_t maxSize) -> uint32_t;
    auto FileDirectoryFromPath(char* path) -> void;
    auto FileNameFromPath(char* path) -> void;
    auto FileExtensionFromPath(char* path) -> char*;

    auto DirectoryExists(const char* path) -> bool;
    auto DirectoryCreate(const char* path) -> bool;
    auto DirectoryDelete(const char* path) -> bool;
    auto DirectoryCurrent(Directory* directory) -> void;
    auto DirectoryChange(const char* path) -> HS_Result;
    auto FileOpenDirectory(const char* path, Directory* outDirectory) -> HS_Result;
    auto FileCloseDirectory(Directory* directory) -> HS_Result;
    auto FileParentDirectory(Directory* directory) -> void;
    auto FileSubDirectory(Directory* directory, const char* subDirectoryName) -> HS_Result;
    auto FileFindFilesInPath(const char* filePattern,StringArray& files) -> void;
    auto FileFindFilesInPath(const char* extension,const char* searchPattern, StringArray& files, StringArray& directories) -> void;
    auto EnvironmentVariableGet(const char* name, char* output, uint32_t outputSize) -> void;
    struct ScopedFile
    {
        ScopedFile(const char* fileName, const char* mode);
        ~ScopedFile();
        FileHandle file;
    };
};