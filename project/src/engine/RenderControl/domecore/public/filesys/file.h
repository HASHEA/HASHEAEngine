/*
    filename:       file.h
    author:         Ming Dong
    date:           2016-MAR-26
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "../container.h"
#include "../os/os_filesys.h"
#include "externalfs.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DFile
{
public:
    DFile(const DString& i_FileName, IExternalFS* i_pExternalFS = DM_NULL);
    ~DFile();

    DResult create(Bool i_bDestroyExisting);
    DResult open(Bool i_bWrite);
    DResult close();

    const DString& getFileName() const;

    Int     getLength() const;
    DResult read(Char* o_pBuffer, Int& io_ReadLen) const;
    DResult write(const Char* i_pBuffer, Int& io_WriteLen) const;
    DResult seek(Bool i_bFromBegin, Int i_Pos) const;

private:
    class DFile_Impl;
    DFile_Impl* me;
};


DOME_NAMESPACE_END