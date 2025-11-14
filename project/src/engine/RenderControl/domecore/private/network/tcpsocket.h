/*
    filename:       tcpsocket.h
    author:         Ming Dong
    date:           2016-Mar-28
    description:    
*/
#pragma once

#include "../../public/typedefs.h"
#include "../../public/network/tcp.h"
#include "../../public/thread/thread.h"
#include "../../public/thread/bitspinlock.h"
#include "../../public/external.h"

DOME_NAMESPACE_BEGIN

namespace tcp
{
    class DTcpSocket;
    class DSocketWorker : public DThread
    {
    public:
        struct DReadMessageSizeHandler
        {
            void operator()(const asio::error_code& ec, std::size_t bytes_transferred)
            {
                m_pObject->readMessageSize_Callback(ec, bytes_transferred);
            }

            DSocketWorker*          m_pObject;
        };

        struct DReadMessageHandler
        {
            void operator()(const asio::error_code& ec, std::size_t bytes_transferred)
            {
                m_pObject->readMessage_Callback(ec, bytes_transferred);
            }

            DSocketWorker*          m_pObject;
        };

        struct DSendMessageHandler
        {
            void operator()(const asio::error_code& ec, std::size_t bytes_transferred)
            {
                m_pObject->sendMessage_Callback(ec, bytes_transferred);
            }

            DSocketWorker*          m_pObject;
        };

        DSocketWorker(asio::io_service* i_pService, asio::ip::tcp::socket* i_pSocket);
        ~DSocketWorker();

        virtual Int     execute();

        void readMessageSize_Callback(const asio::error_code& ec, std::size_t bytes_transferred);
        void readMessage_Callback(const asio::error_code& ec, std::size_t bytes_transferred);
        void sendMessage_Callback(const asio::error_code& ec, std::size_t bytes_transferred);

        DSimpleMessage* receiveMessage();
        DResult         sendMessage(DSimpleMessage* i_pMessage);

        Bool isSocketClosed();

    private:
        void setSocketClosed();

        void addMessageToReceivePool(DSimpleMessage* i_pMessage);
        DSimpleMessage* getMessageFromReceivePool();

        void addMessageToSendPool(DSimpleMessage* i_pMessage, Bool i_bManuallyLock = DM_FALSE);
        DSimpleMessage* getMessageFromSendPool(Bool i_bManuallyLock = DM_FALSE);

    private:
        asio::io_service*       m_pAsioIoService;
        asio::ip::tcp::socket*  m_pAsioTcpSocket;

        volatile U32            m_LockU32;
        DBitSpinLock0           m_ReceiveBufferLock;
        DBitSpinLock1           m_SendBufferLock;
        DBitSpinLock2           m_StateLock;

        Bool                    m_bClosed;

        typedef TArray<DSimpleMessage*>     _MessageArray;
        DSendMessageHandler     m_SendMessageHandler;
        _MessageArray           m_SendMessagePool;
        S32                     m_SendMessageSize;
        U8*                     m_SendMessageBuff;

        DReadMessageSizeHandler m_ReadSizeHandler;
        DReadMessageHandler     m_ReadMessageHandler;
        _MessageArray           m_ReceivedMessagePool;
        S32                     m_ReceivedMessageSize;
        U8*                     m_ReceivedMessageBuff;
    };

    class DTcpSocket : public ITcpSocket
    {
    public:
        DTcpSocket()
            : m_pWorker(DM_NULL)
        {
            m_pAsioIoService = DOME_New(asio::io_service);
            m_pAsioTcpSocket = DOME_New(asio::ip::tcp::socket)(*m_pAsioIoService);
        }
        virtual ~DTcpSocket()
        {
            closeConnection();
            DOME_Del(m_pAsioTcpSocket);
            DOME_Del(m_pAsioIoService);
        }

        asio::io_service*       getAsioIoService()
        {
            return m_pAsioIoService;
        }

        asio::ip::tcp::socket*  getAsioTcpSocket()
        {
            return m_pAsioTcpSocket;
        }

        /*
            return true if current socket is closed by other peer
        */
        virtual Bool            isClosed() const
        {
            if(m_pWorker)
                return m_pWorker->isSocketClosed();
            else
                return DM_TRUE;
        }

        /*
            close the tcp connection
        */
        virtual DResult         closeConnection()
        {
            if (m_pWorker)
            {
                asio::error_code ec;
                m_pAsioTcpSocket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                m_pAsioTcpSocket->close(ec);
                m_pAsioIoService->stop();
                m_pWorker->waitFinish();
                DOME_Del(m_pWorker);
                m_pWorker = DM_NULL;
            }
            return R_SUCCESS;
        }

        /*
            destroy the socket

        */
        virtual DResult         destroy()
        {
            closeConnection();
            DOME_Del(this);
            return R_SUCCESS;
        }

        /*
            get a message from the queue, if there is no message, return NULL
        */
        virtual DSimpleMessage* receiveMessage()
        {
            if(m_pWorker)
                return m_pWorker->receiveMessage();
            else
                return DM_NULL;
        }

        /*
            put a message to the sending queue
        */
        virtual DResult         sendMessage(DSimpleMessage* i_pMessage)
        {
            if(m_pWorker)
                return m_pWorker->sendMessage(i_pMessage);
            else
            {
                DOME_Del(i_pMessage);
                return R_SOCKETCLOSED;
            }
        }

    private:
        friend class DTcpClient;
        friend class DTcpServer;
        void                    start();

        DSocketWorker*          m_pWorker;
        asio::io_service*       m_pAsioIoService;
        asio::ip::tcp::socket*  m_pAsioTcpSocket;
    };

}


DOME_NAMESPACE_END