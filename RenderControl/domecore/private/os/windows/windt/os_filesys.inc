// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")

DOME_NAMESPACE_BEGIN

struct DOSRFindFileInfo : public DOSResourceInfo
{
    HANDLE              m_Handle;
};
TStaticPool<DOSRFindFileInfo, DOME_MAX_FINDFILEOPERATION>       g_FindFilePool;


DResult      OS_FileSys::FileFindFirst(OSHandle& o_Handle, const Char* i_pPath, Int i_BuffSize, Char* o_pFileName, Bool* o_bFolder)
{
    DOME_ASSERT(!g_FindFilePool.isFull());
    if(g_FindFilePool.isFull())
        return R_OUTOFRANGE;

    WIN32_FIND_DATAA l_FindData;
    HANDLE l_Handle = ::FindFirstFileA(i_pPath, &l_FindData);
    if (l_Handle != INVALID_HANDLE_VALUE)
    {
        Int ifree = g_FindFilePool.alloc();
        DOSRFindFileInfo* l_pFindFileInfo = g_FindFilePool.getPtr(ifree);

        l_pFindFileInfo->m_Handle = l_Handle;
        o_Handle.set(OS_Manager::OSRT_FINDFILE, (S16)ifree);

        strncpy_s(o_pFileName, i_BuffSize, l_FindData.cFileName, _TRUNCATE);
        *o_bFolder = (l_FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        return R_SUCCESS;
    }
    else
    {
        return R_FAILED;
    }
}

DResult      OS_FileSys::FileFindNext(OSHandle i_Handle, Int i_BuffSize, Char* o_pFileName, Bool* o_bFolder)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FINDFILE);
    DOME_ASSERT(g_FindFilePool.isAlloced(i_Handle.getHandle()));

    WIN32_FIND_DATAA l_FindData;
    BOOL hr = ::FindNextFileA(g_FindFilePool.getPtr(i_Handle.getHandle())->m_Handle, &l_FindData);
    if (hr)
    {
        strncpy_s(o_pFileName, i_BuffSize, l_FindData.cFileName, _TRUNCATE);
        *o_bFolder = (l_FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return R_SUCCESS;
    }
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileFindClose(OSHandle i_Handle)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FINDFILE);
    DOME_ASSERT(g_FindFilePool.isAlloced(i_Handle.getHandle()));

    BOOL hr = DM_TRUE;
    DOSRFindFileInfo* l_pFindFileInfo = g_FindFilePool.getPtr(i_Handle.getHandle());

    hr = ::FindClose(l_pFindFileInfo->m_Handle);
    l_pFindFileInfo->m_Handle = DM_NULL;

    g_FindFilePool.free(i_Handle.getHandle());

    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

Bool         OS_FileSys::FileExist(const Char* i_pFileName)
{
    return ::GetFileAttributesA(i_pFileName) != 0xFFFFFFFF;
}

DResult      OS_FileSys::FileCopy(const Char* i_pSrcFileName, const Char* i_pDstFileName, Bool i_bFailIfExist)
{
    BOOL hr = ::CopyFileA(i_pSrcFileName, i_pDstFileName, i_bFailIfExist);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileMove(const Char* i_pSrcFileName, const Char* i_pDstFileName)
{
    BOOL hr = ::MoveFileA(i_pSrcFileName, i_pDstFileName);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileDelete(const Char* i_pFileName)
{
    BOOL hr = ::DeleteFileA(i_pFileName);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::DirectoryCreate(const Char* i_pDirName)
{
    BOOL hr = ::CreateDirectoryA(i_pDirName, DM_NULL);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::DirectoryRemove(const Char* i_pDirName)
{
    BOOL hr = ::RemoveDirectoryA(i_pDirName);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::DirectorySetCurrent(const Char* i_pDirName)
{
    BOOL hr = ::SetCurrentDirectoryA(i_pDirName);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::DirectoryGetCurrent(Int i_BuffSize, Char* o_pDirName)
{
    BOOL hr = ::GetCurrentDirectoryA((DWORD)i_BuffSize, o_pDirName);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

struct DOSRFileInfo : public DOSResourceInfo
{
    HANDLE          m_Handle;
};
TStaticPool<DOSRFileInfo, DOME_MAX_FILECREATED>         g_FilePool;

DResult     OS_FileSys::FileCreate(OSHandle& o_Handle, const Char* i_pFileName, Bool i_bDestroyExisting)
{
    DOME_ASSERT(!g_FilePool.isFull());
    if(g_FilePool.isFull())
        return R_FAILED;

    HANDLE l_Handle = ::CreateFileA(i_pFileName, GENERIC_READ | GENERIC_WRITE, 0, DM_NULL, i_bDestroyExisting ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, DM_NULL);
    if (l_Handle != INVALID_HANDLE_VALUE)
    {
        Int ifree = g_FilePool.alloc();
        DOSRFileInfo* l_pFileInfo = g_FilePool.getPtr(ifree);
        l_pFileInfo->m_Handle = l_Handle;
        o_Handle.set(OS_Manager::OSRT_FILE, (S16)ifree);
        return R_SUCCESS;
    }
    else
        return R_FAILED;
}

DResult     OS_FileSys::FileOpen(OSHandle& o_Handle, const Char* i_pFileName, Bool i_bWrite)
{
    DOME_ASSERT(!g_FilePool.isFull());
    if(g_FilePool.isFull())
        return R_FAILED;

    DWORD assess = GENERIC_READ;
    if(i_bWrite) assess |= GENERIC_WRITE;
    HANDLE l_Handle = ::CreateFileA(i_pFileName, assess, 0, DM_NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, DM_NULL);
    if (l_Handle != INVALID_HANDLE_VALUE)
    {
        Int ifree = g_FilePool.alloc();
        DOSRFileInfo* l_pFileInfo = g_FilePool.getPtr(ifree);
        l_pFileInfo->m_Handle = l_Handle;
        o_Handle.set(OS_Manager::OSRT_FILE, (S16)ifree);
        return R_SUCCESS;
    }
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileClose(OSHandle& i_Handle)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FILE);
    DOME_ASSERT(g_FilePool.isAlloced(i_Handle.getHandle()));

    BOOL hr = DM_TRUE;
    DOSRFileInfo* l_pFileInfo = g_FilePool.getPtr(i_Handle.getHandle());

    hr = ::CloseHandle(l_pFileInfo->m_Handle);
    l_pFileInfo->m_Handle = DM_NULL;
    g_FilePool.free(i_Handle.getHandle());

    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

Int          OS_FileSys::FileGetLength(OSHandle i_Handle)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FILE);
    DOME_ASSERT(g_FilePool.isAlloced(i_Handle.getHandle()));

    return ::GetFileSize(g_FilePool.getPtr(i_Handle.getHandle())->m_Handle, DM_NULL);
}

DResult      OS_FileSys::FileRead(OSHandle i_Handle, Char* o_pBuffer, Int i_BeginPos, Int& io_ReadLen)
{
    if(DM_FAIL(FileSeek(i_Handle, DM_TRUE, i_BeginPos)))
        return R_FAILED;

    return FileRead(i_Handle, o_pBuffer, io_ReadLen);
}

DResult      OS_FileSys::FileWrite(OSHandle i_Handle, const Char* i_pBuffer, Int i_BeginPos, Int& io_WriteLen)
{
    if(DM_FAIL(FileSeek(i_Handle, DM_TRUE, i_BeginPos)))
        return R_FAILED;

    return FileWrite(i_Handle, i_pBuffer, io_WriteLen);
}

DResult      OS_FileSys::FileSeek(OSHandle i_Handle, Bool i_bFromBegin, Int i_Pos)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FILE);
    DOME_ASSERT(g_FilePool.isAlloced(i_Handle.getHandle()));

    BOOL hr = ::SetFilePointer(g_FilePool.getPtr(i_Handle.getHandle())->m_Handle, 
        (LONG)i_Pos, DM_NULL, i_bFromBegin ? FILE_BEGIN : FILE_END);

    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileRead(OSHandle i_Handle, Char* o_pBuffer, Int& io_ReadLen)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FILE);
    DOME_ASSERT(g_FilePool.isAlloced(i_Handle.getHandle()));

    DWORD l_RealRead = 0;
    BOOL hr = ::ReadFile(g_FilePool.getPtr(i_Handle.getHandle())->m_Handle, o_pBuffer, (DWORD)io_ReadLen, &l_RealRead, DM_NULL);
    io_ReadLen = l_RealRead;
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::FileWrite(OSHandle i_Handle, const Char* i_pBuffer, Int& io_WriteLen)
{
    DOME_ASSERT(i_Handle.getType() == OS_Manager::OSRT_FILE);
    DOME_ASSERT(g_FilePool.isAlloced(i_Handle.getHandle()));

    DWORD l_RealWrite = 0;
    BOOL hr = ::WriteFile(g_FilePool.getPtr(i_Handle.getHandle())->m_Handle, i_pBuffer, (DWORD)io_WriteLen, &l_RealWrite, DM_NULL);
    io_WriteLen = l_RealWrite;
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult      OS_FileSys::Init()
{
    return R_SUCCESS;
}

DResult      OS_FileSys::Uninit()
{
    return R_SUCCESS;
}


DOME_NAMESPACE_END