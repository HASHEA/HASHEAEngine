/*
    filename:       file.cpp
    author:         Ming Dong
    date:           2016-MAR-26
    description:    
*/

#include "../../public/filesys/file.h"

DOME_NAMESPACE_BEGIN

class DFile::DFile_Impl
{
public:
    DFile_Impl(const DString& i_FileName, IExternalFS* i_pExternalFS)
    {
        m_FileName = i_FileName;
        m_pExternalFS = i_pExternalFS;
        m_pExternalFSFile = DM_NULL;

        if (m_pExternalFS)
        {
            m_pExternalFSFile = m_pExternalFS->createFileObject(i_FileName.c_str());
        }
    }
    
    ~DFile_Impl()
    {
        if (m_pExternalFS)
        {
            if (m_pExternalFSFile)
            {
                m_pExternalFS->destroyFileObject(m_pExternalFSFile);
                m_pExternalFSFile = DM_NULL;
            }
        }
        else
        {
            if (m_FileHandle.isValid())
            {
                OS_FileSys::FileClose(m_FileHandle);
                m_FileHandle.invalid();
            }
        }
    }

    DResult create(Bool i_bDestroyExisting)
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->create(i_bDestroyExisting);
        }
        else
        {
            return OS_FileSys::FileCreate(m_FileHandle, m_FileName.c_str(), i_bDestroyExisting);
        }
    }

    DResult open(Bool i_bWrite)
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->open(i_bWrite);
        }
        else
        {
            return OS_FileSys::FileOpen(m_FileHandle, m_FileName.c_str(), i_bWrite);
        }
    }

    DResult close()
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->close();
        }
        else
        {
            DResult l_hr = OS_FileSys::FileClose(m_FileHandle);
            m_FileHandle.invalid();
            return l_hr;
        }
    }

    const DString& getFileName() const
    {
        return m_FileName;
    }

    Int     getLength() const
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->getLength();
        }
        else
        {
            return OS_FileSys::FileGetLength(m_FileHandle);
        }
    }

    DResult read(Char* o_pBuffer, Int& io_ReadLen) const
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->read(o_pBuffer, io_ReadLen);
        }
        else
        {
            return OS_FileSys::FileRead(m_FileHandle, o_pBuffer, io_ReadLen);
        }
    }

    DResult write(const Char* i_pBuffer, Int& io_WriteLen) const
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->write(i_pBuffer, io_WriteLen);
        }
        else
        {
            return OS_FileSys::FileWrite(m_FileHandle, i_pBuffer, io_WriteLen);
        }
    }

    DResult seek(Bool i_bFromBegin, Int i_Pos) const
    {
        if (m_pExternalFS)
        {
            return m_pExternalFSFile->seek(i_bFromBegin, i_Pos);
        }
        else
        {
            return OS_FileSys::FileSeek(m_FileHandle, i_bFromBegin, i_Pos);
        }
    }

private:
    IExternalFS*    m_pExternalFS;
    DString         m_FileName;
    OSHandle        m_FileHandle;
    IExternalFSFile* m_pExternalFSFile;
};

DFile::DFile(const DString& i_FileName, IExternalFS* i_pExternalFS)
{
    me = DOME_New(DFile_Impl)(i_FileName, i_pExternalFS);
}

DFile::~DFile()
{
    DOME_Del(me);
}

DResult DFile::create(Bool i_bDestroyExisting)
{
    return me->create(i_bDestroyExisting);
}

DResult DFile::open(Bool i_bWrite)
{
    return me->open(i_bWrite);
}

DResult DFile::close()
{
    return me->close();
}

const DString& DFile::getFileName() const
{
    return me->getFileName();
}

Int     DFile::getLength() const
{
    return me->getLength();
}

DResult DFile::read(Char* o_pBuffer, Int& io_ReadLen) const
{
    return me->read(o_pBuffer, io_ReadLen);
}

DResult DFile::write(const Char* i_pBuffer, Int& io_WriteLen) const
{
    return me->write(i_pBuffer, io_WriteLen);
}

DResult DFile::seek(Bool i_bFromBegin, Int i_Pos) const
{
    return me->seek(i_bFromBegin, i_Pos);
}



DOME_NAMESPACE_END