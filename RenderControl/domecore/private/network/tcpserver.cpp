/*
    filename:       tcpserver.cpp
    author:         Ming Dong
    date:           2016-Mar-29
    description:    
*/

#include "../../public/network/tcp.h"
#include "tcpsocket.h"

DOME_NAMESPACE_BEGIN

namespace tcp
{

    class DTcpServer::DTcpServer_Impl : public DThread
    {
    public:
        struct DAcceptHandler
        {
            void operator() (const asio::error_code& error)
            {
                m_pTcpServer->accept_Callback(error);
            }

            DTcpServer_Impl*        m_pTcpServer;
        };

        DTcpServer_Impl(Int i_Port)
            : DThread(DM_TRUE)
            , m_PortNumber(i_Port)
        {
            m_pAsioIoService = DOME_New(asio::io_service);
            m_pAsioAcceptor = DOME_New(asio::ip::tcp::acceptor)(*m_pAsioIoService, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), m_PortNumber));


            m_SocketArrayLock.Init(&m_LockU32);
            m_StateLock.Init(&m_LockU32);

            m_AcceptHandler.m_pTcpServer = this;

            m_AcceptingSocket = DM_NULL;

            m_bStopped = DM_FALSE;

            start();
        }

        ~DTcpServer_Impl()
        {
            stop();
            for (Int i = 0; i < m_SocketArray.size(); ++i)
            {
                ITcpSocket* l_pTcpSocket = m_SocketArray[i];
                l_pTcpSocket->destroy();
            }

            if (m_AcceptingSocket)
            {
                m_AcceptingSocket->destroy();
            }

            DOME_Del(m_pAsioAcceptor);
            DOME_Del(m_pAsioIoService);
        }

        virtual Int     execute()
        {
            asyncAcceptConnection();

            m_pAsioIoService->run();

            return 0;
        }

        void accept_Callback(const asio::error_code& error)
        {
            if (!error)
            {
                m_AcceptingSocket->start();

                addTcpSocket(m_AcceptingSocket);
                m_AcceptingSocket = DM_NULL;

                asyncAcceptConnection();
            }
            else
            {
                setStoppedState();
            }
        }

        DResult         stop()
        {
            m_pAsioIoService->stop();
            waitFinish();
            return R_SUCCESS;
        }

        ITcpSocket*     getSocket()
        {
            return getTcpSocket();
        }

    private:
        DResult         asyncAcceptConnection()
        {
            DTcpSocket* l_pTcpSocket = DOME_New(DTcpSocket);

            m_AcceptingSocket = l_pTcpSocket;
            m_pAsioAcceptor->async_accept(*l_pTcpSocket->getAsioTcpSocket(),
                    m_AcceptHandler);

            return R_SUCCESS;

        }

        DResult         addTcpSocket(ITcpSocket* i_pTcpSocket)
        {
            DResult l_Result = R_SUCCESS;
            m_SocketArrayLock.Lock(&m_LockU32);
            m_SocketArray.push_back(i_pTcpSocket);
            m_SocketArrayLock.Unlock(&m_LockU32);
            return l_Result;
        }

        ITcpSocket*     getTcpSocket()
        {
            ITcpSocket* l_pTcpSocket = DM_NULL;
            m_SocketArrayLock.Lock(&m_LockU32);

            if (m_SocketArray.size() > 0)
            {
                l_pTcpSocket = m_SocketArray[0];
                m_SocketArray.remove(0);
            }

            m_SocketArrayLock.Unlock(&m_LockU32);
            return l_pTcpSocket;
        }

        void setStoppedState()
        {
            m_StateLock.Lock(&m_LockU32);
            m_bStopped = DM_TRUE;
            m_StateLock.Unlock(&m_LockU32);
        }

        Bool getStoppedState()
        {
            Bool l_Result = DM_FALSE;
            m_StateLock.Lock(&m_LockU32);
            l_Result = m_bStopped;
            m_StateLock.Unlock(&m_LockU32);
            return l_Result;
        }

    private:
        asio::io_service*        m_pAsioIoService;
        asio::ip::tcp::acceptor* m_pAsioAcceptor;
        Int                     m_PortNumber;
        DAcceptHandler          m_AcceptHandler;
        DTcpSocket*             m_AcceptingSocket;

        Bool                    m_bStopped;

        U32                     m_LockU32;
        DBitSpinLock0           m_SocketArrayLock;
        DBitSpinLock1           m_StateLock;

        typedef TArray<ITcpSocket*>     _SocketArray;
        _SocketArray            m_SocketArray;

    };


    DTcpServer::DTcpServer(Int i_Port)
    {
        me = DOME_New(DTcpServer_Impl)(i_Port);
    }

    DTcpServer::~DTcpServer()
    {
        DOME_Del(me);
    }

    DResult         DTcpServer::stop()
    {
        return me->stop();
    }

    ITcpSocket*     DTcpServer::getSocket()
    {
        return me->getSocket();
    }

}

DOME_NAMESPACE_END