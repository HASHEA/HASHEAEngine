/*
    filename:       os_filesys.h
    author:         Ming Dong
    date:           2016-MAR-10
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "os_common.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_FileSys
{
public:
    static DResult      FileFindFirst(OSHandle& o_Handle, const Char* i_pPath, Int i_BuffSize, Char* o_pFileName, Bool* o_bFolder);
    static DResult      FileFindNext(OSHandle i_Handle, Int i_BuffSize, Char* o_pFileName, Bool* o_bFolder);
    static DResult      FileFindClose(OSHandle i_Handle);

    static Bool         FileExist(const Char* i_pFileName);
    static DResult      FileCopy(const Char* i_pSrcFileName, const Char* i_pDstFileName, Bool i_bFailIfExist);
    static DResult      FileMove(const Char* i_pSrcFileName, const Char* i_pDstFileName);
    static DResult      FileDelete(const Char* i_pFileName);

    static DResult      DirectoryCreate(const Char* i_pDirName);
    static DResult      DirectoryRemove(const Char* i_pDirName);
    static DResult      DirectorySetCurrent(const Char* i_pDirName);
    static DResult      DirectoryGetCurrent(Int i_BuffSize, Char* o_pDirName);

    static DResult      FileCreate(OSHandle& o_Handle, const Char* i_pFileName, Bool i_bDestroyExisting);
    static DResult      FileOpen(OSHandle& o_Handle, const Char* i_pFileName, Bool i_bWrite);
    static DResult      FileClose(OSHandle& i_Handle);
    static Int          FileGetLength(OSHandle i_Handle);
    static DResult      FileRead(OSHandle i_Handle, Char* o_pBuffer, Int i_BeginPos, Int& io_ReadLen);
    static DResult      FileWrite(OSHandle i_Handle, const Char* i_pBuffer, Int i_BeginPos, Int& io_WriteLen);
    static DResult      FileSeek(OSHandle i_Handle, Bool i_bFromBegin, Int i_Pos);
    static DResult      FileRead(OSHandle i_Handle, Char* o_pBuffer, Int& io_ReadLen);
    static DResult      FileWrite(OSHandle i_Handle, const Char* i_pBuffer, Int& io_WriteLen);

    static DResult      Init();
    static DResult      Uninit();
};


DOME_NAMESPACE_END