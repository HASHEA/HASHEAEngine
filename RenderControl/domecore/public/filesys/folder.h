/*
    filename:       folder.h
    author:         Ming Dong
    date:           2016-MAR-26
    description:    
*/
#pragma once

#include "file.h"
#include "externalfs.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DFolder
{
public:
    DFolder(const DString& i_FolderName, IExternalFS* i_pExternalFS = DM_NULL);
    ~DFolder();

    const DString& getFolderName() const;

    DResult     reset();
    DResult     refresh();

    Int         getFileCount() const;
    Int         getFolderCount() const;

    const DString&    getFile(Int i_Index) const;
    const DString&    getFolder(Int i_Index) const;

    DResult     createFolder(const DString& i_FolderName);
    DResult     deleteFolder(const DString& i_FolderName);

private:
    class DFolder_Impl;
    DFolder_Impl*   me;
};


DOME_NAMESPACE_END