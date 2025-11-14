/*
    filename:       folder.cpp
    author:         Ming Dong
    date:           2016-MAR-26
    description:    
*/

#include "../../public/filesys/folder.h"

DOME_NAMESPACE_BEGIN

class DFolder::DFolder_Impl
{
public:
    DFolder_Impl(const DString& i_FolderPath, IExternalFS* i_pExternalFS)
    {
        m_FolderPath = i_FolderPath;
        m_pExternalFS = i_pExternalFS;
    }

    ~DFolder_Impl()
    {

    }

    const DString& getFolderName() const
    {
        return m_FolderPath;
    }

    DResult     reset()
    {
        m_FileList.clear();
        m_FolderList.clear();
        return R_SUCCESS;
    }

    DResult     refresh()
    {
        reset();

        if (m_pExternalFS)
        {
            IExternalFSFolder* l_pFolder = m_pExternalFS->createFolderObject(m_FolderPath.c_str());
            if (!l_pFolder)
                return R_FAILED;
            l_pFolder->refresh();
            for (Int i = 0; i < l_pFolder->getFolderCount(); ++i)
            {
                m_FolderList.push_back(l_pFolder->getFolder(i));
            }
            for (Int i = 0; i < l_pFolder->getFileCount(); ++i)
            {
                m_FileList.push_back(l_pFolder->getFile(i));
            }
            m_pExternalFS->destroyFolderObject(l_pFolder);
            return R_SUCCESS;
        }

        Char l_Buff[DOME_MAX_FILEPATHLENGTH];
        OSHandle l_hFindFile;
        DString l_SearchPath = m_FolderPath + "/*";
        Bool l_bFolder = DM_FALSE;
        if (DM_SUCC(OS_FileSys::FileFindFirst(l_hFindFile, l_SearchPath.c_str(), DOME_MAX_FILEPATHLENGTH, l_Buff, &l_bFolder)))
        {
            do
            {
                if (l_bFolder)
                {
                    m_FolderList.push_back(DString(l_Buff));
                }
                else
                {
                    m_FileList.push_back(DString(l_Buff));
                }
            }
            while(DM_SUCC(OS_FileSys::FileFindNext(l_hFindFile, DOME_MAX_FILEPATHLENGTH, l_Buff, &l_bFolder)));

            OS_FileSys::FileFindClose(l_hFindFile);
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }

    Int         getFileCount() const
    {
        return m_FileList.size();
    }

    Int         getFolderCount() const
    {
        return m_FolderList.size();
    }

    const DString&    getFile(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < getFileCount());
        return m_FileList[i_Index];
    }

    const DString&    getFolder(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < getFolderCount());
        return m_FolderList[i_Index];
    }

    DResult     createFolder(const DString& i_FolderName)
    {
        if (m_pExternalFS)
        {
            IExternalFSFolder* l_pFolder = m_pExternalFS->createFolderObject(m_FolderPath.c_str());
            if (!l_pFolder)
                return R_FAILED;
            l_pFolder->createFolder(i_FolderName.c_str());
            m_pExternalFS->destroyFolderObject(l_pFolder);
            return R_SUCCESS;
        }
        else
        {
            DString l_NewFolderName = getFolderName() + "/" + i_FolderName;
            return OS_FileSys::DirectoryCreate(l_NewFolderName.c_str());
        }
    }

    DResult     deleteFolder(const DString& i_FolderName)
    {
        if (m_pExternalFS)
        {
            IExternalFSFolder* l_pFolder = m_pExternalFS->createFolderObject(m_FolderPath.c_str());
            if (!l_pFolder)
                return R_FAILED;
            l_pFolder->deleteFolder(i_FolderName.c_str());
            m_pExternalFS->destroyFolderObject(l_pFolder);
            return R_SUCCESS;
        }
        else
        {
            DString l_DelFolderName = getFolderName() + "/" + i_FolderName;
            return OS_FileSys::DirectoryRemove(l_DelFolderName.c_str());
        }
    }

private:
    typedef TArray<DString>     _StringList;
    IExternalFS*        m_pExternalFS;
    DString             m_FolderPath;
    _StringList         m_FileList;
    _StringList         m_FolderList;
};


DFolder::DFolder(const DString& i_FolderName, IExternalFS* i_pExternalFS)
{
    me = DOME_New(DFolder_Impl)(i_FolderName, i_pExternalFS);
}

DFolder::~DFolder()
{
    DOME_Del(me);
}

const DString& DFolder::getFolderName() const
{
    return me->getFolderName();
}

DResult     DFolder::reset()
{
    return me->reset();
}

DResult     DFolder::refresh()
{
    return me->refresh();
}

Int         DFolder::getFileCount() const
{
    return me->getFileCount();
}

Int         DFolder::getFolderCount() const
{
    return me->getFolderCount();
}

const DString&    DFolder::getFile(Int i_Index) const
{
    return me->getFile(i_Index);
}

const DString&    DFolder::getFolder(Int i_Index) const
{
    return me->getFolder(i_Index);
}

DResult     DFolder::createFolder(const DString& i_FolderName)
{
    return me->createFolder(i_FolderName);
}

DResult     DFolder::deleteFolder(const DString& i_FolderName)
{
    return me->deleteFolder(i_FolderName);
}


DOME_NAMESPACE_END