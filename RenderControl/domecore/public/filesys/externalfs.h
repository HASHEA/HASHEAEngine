/*
    filename:       externalfs.h
    author:         Ming Dong
    date:           2017-Jun-13
    description:    
*/
#pragma once

#include "../typedefs.h"

DOME_NAMESPACE_BEGIN

class IExternalFSFile
{
public:
    virtual ~IExternalFSFile() {}

    virtual DResult         create(Bool i_bDestroyExisting) = 0;
    virtual DResult         open(Bool i_bWrite) = 0;
    virtual DResult         close() = 0;

    virtual Int             getLength() const = 0;
    virtual DResult         read(Char* o_pBuffer, Int& io_ReadLen) const = 0;
    virtual DResult         write(const Char* i_pBuffer, Int& io_WriteLen) const = 0;
    virtual DResult         seek(Bool i_bFromBegin, Int i_Pos) const = 0;
};

class IExternalFSFolder
{
public:
    virtual ~IExternalFSFolder() {}

    virtual DResult         refresh() = 0;

    virtual Int             getFileCount() const = 0;
    virtual Int             getFolderCount() const = 0;
    virtual const Char*     getFile(Int i_Index) const = 0;
    virtual const Char*     getFolder(Int i_Index) const = 0;

    virtual DResult         createFolder(const Char* i_pFolderName) = 0;
    virtual DResult         deleteFolder(const Char* i_pFolderName) = 0;
};

class IExternalFS
{
public:
    virtual ~IExternalFS() {}

    virtual IExternalFSFile* createFileObject(const Char* i_pPath) = 0;
    virtual DResult destroyFileObject(IExternalFSFile* i_pFile) = 0;
    virtual IExternalFSFolder* createFolderObject(const Char* i_pPath) = 0;
    virtual DResult destroyFolderObject(IExternalFSFolder* i_pFolder) = 0;
};

DOME_CORE_API IExternalFS*      DOME_GetExternalFS();
DOME_CORE_API DResult           DOME_SetExternalFS(IExternalFS* i_pFS);

DOME_NAMESPACE_END