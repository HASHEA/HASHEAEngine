#pragma once
#include "hplatform.h"
#include "hcore.h"
#include <stdio.h>
namespace AshEngine
{

#if defined(_WIN64)

    typedef struct __ASHFILETIME {
        unsigned long       dwLowDateTime;
        unsigned long       dwHighDateTime;
    } ASHFILETIME, * ASHPFILETIME, * ASHLPFILETIME;

    using FileTime = __ASHFILETIME;

#endif
    using FileHandle = FILE*;
    static const uint32_t                k_max_path = 512;

    struct Directory {
        char                        path[k_max_path]{};

#if defined (_WIN64)
        void* os_handle = nullptr;
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

    auto file_read_binary(const char* fileName , size_t& size, Allocator* allocator = nullptr) -> char*;
    ASH_API auto file_read_text(const char* fileName ,  size_t& size, Allocator* allocator = nullptr) -> char*;
    auto file_read_binary(const char* fileName , Allocator* allocator = nullptr) -> FileReadResult;
    auto file_read_text(const char* fileName , Allocator* allocator = nullptr) -> FileReadResult;
    ASH_API auto file_write_binary(const char* fileName, void* memory, size_t size) -> void;
    auto file_exists(const char* fileName) -> bool;
    auto file_open(const char* fileName, const char* mode, FileHandle* file) -> void;
    auto file_close(FileHandle file)-> void;
    auto file_write(uint8_t* memory,uint32_t elementSize,uint32_t count,FileHandle file) -> size_t;
    ASH_API auto file_delete(const char* filePath) -> bool;
#if defined(_WIN64)
    auto file_last_write_time(const char* filename) -> FileTime;
#endif
    auto file_resolve_to_full_path(const char* path, char* outFullPath, uint32_t maxSize) -> uint32_t;
    auto file_directory_from_path(char* path) -> void;
    auto file_name_from_path(char* path) -> void;
    ASH_API auto file_extension_from_path(char* path) -> char*;

    auto directory_exists(const char* path) -> bool;
    auto directory_create(const char* path) -> bool;
    auto directory_delete(const char* path) -> bool;
    ASH_API auto directory_current(Directory* directory) -> void;
    auto directory_change(const char* path) -> bool;
    ASH_API auto file_open_directory(const char* path, Directory* outDirectory) -> bool;
    ASH_API auto file_close_directory(Directory* directory) -> bool;
    ASH_API auto file_parent_directory(Directory* directory) -> void;
    ASH_API auto file_sub_directory(Directory* directory, const char* subDirectoryName) -> bool;
    auto file_find_files_in_path(const char* filePattern,StringArray& files) -> void;
    auto file_find_files_in_path(const char* extension,const char* searchPattern, StringArray& files, StringArray& directories) -> void;
    auto env_var_get(const char* name, char* output, uint32_t outputSize) -> void;
    struct ScopedFile
    {
        ScopedFile(const char* fileName, const char* mode);
        ~ScopedFile();
        FileHandle file;
    };
    
};
