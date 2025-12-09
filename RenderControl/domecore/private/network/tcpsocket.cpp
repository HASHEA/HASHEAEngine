/*
    filename:       tcpsocket.cpp
    author:         Ming Dong
    date:           2016-Mar-28
    description:    
*/

#include "tcpsocket.h"

DOME_NAMESPACE_BEGIN

namespace tcp
{

DSocketWorker::DSocketWorker(asio::io_service* i_pService, asio::ip::tcp::socket* i_pSocket)
    : DThread(DM_TRUE)
{
    m_pAsioIoService = i_pService;
    m_pAsioTcpSocket = i_pSocket;

    m_ReceiveBufferLock.Init(&m_LockU32);
    m_SendBufferLock.Init(&m_LockU32);
    m_StateLock.Init(&m_LockU32);

    m_bClosed = DM_FALSE;

    m_SendMessageHandler.m_pObject = this;
    m_SendMessageSize = 0;
    m_SendMessageBuff = DM_NULL;

    m_ReadSizeHandler.m_pObject = this;
    m_ReadMessageHandler.m_pObject = this;
    m_ReceivedMessageSize = 0;
    m_ReceivedMessageBuff = DM_NULL;

    start();
}

DSocketWorker::~DSocketWorker()
{
    DOME_ASSERT(!m_ReceivedMessageBuff);
    DOME_ASSERT(!m_SendMessageBuff);

    for (Int i = 0; i < m_SendMessagePool.size(); ++i)
    {
        DOME_Del(m_SendMessagePool[i]);
    }

    for (Int i = 0; i < m_ReceivedMessagePool.size(); ++i)
    {
        DOME_Del(m_ReceivedMessagePool[i]);
    }
}

Int DSocketWorker::execute()
{
    asio::async_read(*m_pAsioTcpSocket, asio::buffer(&m_ReceivedMessageSize, sizeof(S32)), m_ReadSizeHandler);

    m_pAsioIoService->run();

    return 0;
}

void DSocketWorker::readMessageSize_Callback(const asio::error_code& ec, std::size_t bytes_transferred)
{
    if (!ec)
    {
        DOME_ASSERT(bytes_transferred == sizeof(S32));
        DOME_ASSERT(m_ReceivedMessageBuff == DM_NULL);

        m_ReceivedMessageBuff = (U8*)DOME_Alloc(m_ReceivedMessageSize);
        asio::async_read(*m_pAsioTcpSocket, 
            asio::buffer(m_ReceivedMessageBuff, m_ReceivedMessageSize), 
            m_ReadMessageHandler);
    }
    else
    {
        // error happend, maybe the socket is closed by peer
        setSocketClosed();
    }
}

void DSocketWorker::readMessage_Callback(const asio::error_code& ec, std::size_t bytes_transferred)
{
    if (!ec)
    {
        DOME_ASSERT(bytes_transferred == m_ReceivedMessageSize);
        DSimpleMessage* l_pMessage = DOME_New(DSimpleMessage);
        Int l_BufferRead = 0;
        l_pMessage->deserialize(m_ReceivedMessageSize, m_ReceivedMessageBuff, l_BufferRead);
        DOME_ASSERT(m_ReceivedMessageSize == l_BufferRead);

        addMessageToReceivePool(l_pMessage);

        DOME_Free(m_ReceivedMessageBuff);
        m_ReceivedMessageBuff = DM_NULL;
        m_ReceivedMessageSize = 0;
        asio::async_read(*m_pAsioTcpSocket, 
            asio::buffer(&m_ReceivedMessageSize, sizeof(S32)), m_ReadSizeHandler);
    }
    else
    {
        // error happend, maybe the socket is closed by peer
        if (m_ReceivedMessageBuff)
        {
            DOME_Free(m_ReceivedMessageBuff);
            m_ReceivedMessageBuff = DM_NULL;
            m_ReceivedMessageSize = 0;
        }
        setSocketClosed();
    }
}

void DSocketWorker::sendMessage_Callback(const asio::error_code& ec, std::size_t bytes_transferred)
{
    if (!ec)
    {
        m_SendBufferLock.Lock(&m_LockU32);

        DOME_ASSERT(bytes_transferred == m_SendMessageSize);
        DOME_ASSERT(m_SendMessageBuff);

        DOME_Free(m_SendMessageBuff);
        m_SendMessageBuff = DM_NULL;
        m_SendMessageSize = 0;

        // if there is still message in sending pool, peek one and send it
        DSimpleMessage* l_pMessage = getMessageFromSendPool(DM_TRUE);
        if (l_pMessage)
        {
            Int l_MessageBufferSize = l_pMessage->getMessageBufferSize();
            DOME_ASSERT(l_MessageBufferSize > 0);

            Int l_BufferWrite = 0;
            m_SendMessageBuff = (U8*)DOME_Alloc(l_MessageBufferSize + sizeof(S32));
            *((S32*)m_SendMessageBuff) = l_MessageBufferSize;
            l_pMessage->serialize(l_MessageBufferSize, m_SendMessageBuff + sizeof(S32), l_BufferWrite);
            DOME_ASSERT(l_MessageBufferSize == l_BufferWrite);

            m_SendMessageSize = (S32)l_MessageBufferSize + sizeof(S32);

            DOME_Del(l_pMessage);

            asio::async_write(*m_pAsioTcpSocket, 
                asio::buffer(m_SendMessageBuff, m_SendMessageSize), 
                m_SendMessageHandler);
        }


        m_SendBufferLock.Unlock(&m_LockU32);
    }
    else
    {
        if (m_SendMessageBuff)
        {
            DOME_Free(m_SendMessageBuff);
            m_SendMessageBuff = DM_NULL;
            m_SendMessageSize = 0;
        }
    }
}

DSimpleMessage* DSocketWorker::receiveMessage()
{
    return getMessageFromReceivePool();
}

DResult         DSocketWorker::sendMessage(DSimpleMessage* i_pMessage)
{
    if (!isSocketClosed())
    {
        m_SendBufferLock.Lock(&m_LockU32);

        addMessageToSendPool(i_pMessage, DM_TRUE);

        // if there is no message sending, I should trigger one here
        if (m_SendMessageSize == 0 && !m_SendMessageBuff)
        {
            DSimpleMessage* l_pMessage = getMessageFromSendPool(DM_TRUE);
            Int l_MessageBufferSize = l_pMessage->getMessageBufferSize();
            DOME_ASSERT(l_MessageBufferSize > 0);

            Int l_BufferWrite = 0;
            m_SendMessageBuff = (U8*)DOME_Alloc(l_MessageBufferSize + sizeof(S32));
            *((S32*)m_SendMessageBuff) = l_MessageBufferSize;
            l_pMessage->serialize(l_MessageBufferSize, m_SendMessageBuff + sizeof(S32), l_BufferWrite);
            DOME_ASSERT(l_MessageBufferSize == l_BufferWrite);

            m_SendMessageSize = (S32)l_MessageBufferSize + sizeof(S32);

            DOME_Del(l_pMessage);

            asio::async_write(*m_pAsioTcpSocket, 
                asio::buffer(m_SendMessageBuff, m_SendMessageSize), 
                m_SendMessageHandler);
        }

        m_SendBufferLock.Unlock(&m_LockU32);
        return R_SUCCESS;
    }
    else
    {
        DOME_Del(i_pMessage);
        return R_SOCKETCLOSED;
    }
}

void DSocketWorker::setSocketClosed()
{
    m_StateLock.Lock(&m_LockU32);
    m_bClosed = DM_TRUE;
    m_StateLock.Unlock(&m_LockU32);
}

Bool DSocketWorker::isSocketClosed()
{
    Bool r;
    m_StateLock.Lock(&m_LockU32);
    r = m_bClosed;
    m_StateLock.Unlock(&m_LockU32);
    return r;
}

void DSocketWorker::addMessageToReceivePool(DSimpleMessage* i_pMessage)
{
    m_ReceiveBufferLock.Lock(&m_LockU32);
    m_ReceivedMessagePool.push_back(i_pMessage);
    m_ReceiveBufferLock.Unlock(&m_LockU32);
}

DSimpleMessage* DSocketWorker::getMessageFromReceivePool()
{
    DSimpleMessage* l_pMessage = DM_NULL;
    m_ReceiveBufferLock.Lock(&m_LockU32);
    if (m_ReceivedMessagePool.size() > 0)
    {
        l_pMessage = m_ReceivedMessagePool[0];
        m_ReceivedMessagePool.remove(0);
    }
    m_ReceiveBufferLock.Unlock(&m_LockU32);
    return l_pMessage;
}

void DSocketWorker::addMessageToSendPool(DSimpleMessage* i_pMessage, Bool i_bManuallyLock)
{
    if(!i_bManuallyLock)
        m_SendBufferLock.Lock(&m_LockU32);

    m_SendMessagePool.push_back(i_pMessage);

    if(!i_bManuallyLock)
        m_SendBufferLock.Unlock(&m_LockU32);
}

DSimpleMessage* DSocketWorker::getMessageFromSendPool(Bool i_bManuallyLock)
{
    DSimpleMessage* l_pMessage = DM_NULL;

    if(!i_bManuallyLock)
        m_SendBufferLock.Lock(&m_LockU32);

    if (m_SendMessagePool.size() > 0)
    {
        l_pMessage = m_SendMessagePool[0];
        m_SendMessagePool.remove(0);
    }

    if(!i_bManuallyLock)
        m_SendBufferLock.Unlock(&m_LockU32);

    return l_pMessage;
}


void DTcpSocket::start()
{
    DOME_ASSERT(!m_pWorker);
    m_pWorker = DOME_New(DSocketWorker)(m_pAsioIoService, m_pAsioTcpSocket);
}


}

DOME_NAMESPACE_END