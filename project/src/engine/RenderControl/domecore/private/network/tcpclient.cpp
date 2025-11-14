/*
    filename:       tcpclient.cpp
    author:         Ming Dong
    date:           2016-Mar-29
    description:    
*/

#include "../../public/network/tcp.h"
#include "tcpsocket.h"

DOME_NAMESPACE_BEGIN

namespace tcp
{

    class DTcpClient::DTcpClient_Impl
    {
    public:
        struct _TaskInfo
        {
            DTcpClient_Impl*        m_pClient;
            DHashString             m_SocketName;
            DString                 m_SocketIp;
            Int                     m_SocketPort;
        };

        static Int ThreadWorker(void* i_pParam)
        {
            _TaskInfo* l_pTaskInfo = (_TaskInfo*)i_pParam;
            DTcpSocket* l_pTcpSocket = DOME_New(DTcpSocket);
            asio::error_code ec;
            asio::ip::tcp::socket* l_pSocket = l_pTcpSocket->getAsioTcpSocket();

            asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(l_pTaskInfo->m_SocketIp.c_str()), (U16)l_pTaskInfo->m_SocketPort);
            l_pSocket->connect(endpoint, ec);

            if (!ec)
            {
                DOME_ASSERT(DM_SUCC(l_pTaskInfo->m_pClient->addSocket(l_pTaskInfo->m_SocketName, l_pTcpSocket)));
            }
            else
            {
                DOME_Del(l_pTcpSocket);
                DOME_ASSERT(DM_SUCC(l_pTaskInfo->m_pClient->addSocket(l_pTaskInfo->m_SocketName, DM_NULL)));
            }
            DOME_Del(l_pTaskInfo);
            return 0;
        }

        DTcpClient_Impl()
        {
            m_SocketMapLock.Init(&m_LockU32);
        }

        ~DTcpClient_Impl()
        {

        }

        DResult         connectAsync(const DHashString& i_SocketName, const DString& i_ip, Int i_Port)
        {
            OSHandle l_hThread;

            _TaskInfo* l_pTask = DOME_New(_TaskInfo);
            l_pTask->m_pClient = this;
            l_pTask->m_SocketName = i_SocketName;
            l_pTask->m_SocketIp = i_ip;
            l_pTask->m_SocketPort = i_Port;

            DResult l_Result = OS_Thread::ThreadCreate(l_hThread, &ThreadWorker, l_pTask);
            OS_Thread::ThreadDestroy(l_hThread);
            if (DM_SUCC(l_Result))
            {
                return R_SUCCESS;
            }
            else
            {
                DOME_Del(l_pTask);
                return R_FAILED;
            }
        }

        ITcpSocket*     getSocket(const DHashString& i_SocketName, Bool& o_bFinished)
        {
            ITcpSocket* l_pTcpSocket = DM_NULL;
            m_SocketMapLock.Lock(&m_LockU32);
            ConnectedSocketMap::iterator it = m_SocketMap.find(i_SocketName);

            if(it == m_SocketMap.end())
                o_bFinished = DM_FALSE;
            else
            {
                o_bFinished = DM_TRUE;
                l_pTcpSocket = it->second;
            }
            m_SocketMapLock.Unlock(&m_LockU32);
            return l_pTcpSocket;
        }

        DResult         addSocket(const DHashString& i_SocketName, ITcpSocket* i_pSocket)
        {
            DResult hr;
            m_SocketMapLock.Lock(&m_LockU32);
            ConnectedSocketMap::iterator it = m_SocketMap.find(i_SocketName);
            DOME_ERROR2(it == m_SocketMap.end(), "ERROR: sockets have same name.");

            if(it == m_SocketMap.end())
                hr = R_FAILED;
            else
            {
                m_SocketMap[i_SocketName] = i_pSocket;
                hr = R_SUCCESS;
            }
            m_SocketMapLock.Unlock(&m_LockU32);
            return hr;
        }


    private:
        typedef TMap<DHashString, ITcpSocket*>      ConnectedSocketMap;
        U32                                         m_LockU32;
        DBitSpinLock0                               m_SocketMapLock;
        ConnectedSocketMap                          m_SocketMap;
    };


    DTcpClient::DTcpClient()
    {
        me = DOME_New(DTcpClient_Impl);
    }

    DTcpClient::~DTcpClient()
    {
        DOME_Del(me);
    }

    ITcpSocket*     DTcpClient::connect(const DString& i_ip, Int i_Port)
    {
        DTcpSocket* l_pTcpSocket = DOME_New(DTcpSocket);
        asio::error_code ec;
        asio::ip::tcp::socket* l_pSocket = l_pTcpSocket->getAsioTcpSocket();

        asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(i_ip.c_str()), (U16)i_Port);
        l_pSocket->connect(endpoint, ec);

        if (!ec)
        {
            l_pTcpSocket->start();
            return l_pTcpSocket;
        }
        else
        {
            DOME_Del(l_pTcpSocket);
            return DM_NULL;
        }
    }

    DResult         DTcpClient::connectAsync(const DHashString& i_SocketName, const DString& i_ip, Int i_Port)
    {
        return me->connectAsync(i_SocketName, i_ip, i_Port);
    }

    ITcpSocket*     DTcpClient::getSocket(const DHashString& i_SocketName, Bool& o_bFinished)
    {
        return me->getSocket(i_SocketName, o_bFinished);
    }


}


DOME_NAMESPACE_END