/*
    filename:       tcp.h
    author:         Ming Dong
    date:           2016-Mar-29
    description:    
*/
#pragma once

#include "simplemessage.h"

DOME_NAMESPACE_BEGIN

namespace tcp
{
    class ITcpSocket
    {
    public:
        virtual ~ITcpSocket(){}

        /*
            return true if current socket is closed by other peer
        */
        virtual Bool            isClosed() const = 0;

        /*
            close the tcp connection
        */
        virtual DResult         closeConnection() = 0;

        /*
            destroy the socket

        */
        virtual DResult         destroy() = 0;

        /*
            get a message from the queue, if there is no message, return NULL
        */
        virtual DSimpleMessage* receiveMessage() = 0;

        /*
            put a message to the sending queue
        */
        virtual DResult         sendMessage(DSimpleMessage* i_pMessage) = 0;

    };

    class DOME_CORE_API DTcpClient
    {
    public:
        DTcpClient();
        ~DTcpClient();

        ITcpSocket*     connect(const DString& i_ip, Int i_Port);

        DResult         connectAsync(const DHashString& i_SocketName, const DString& i_ip, Int i_Port);

        ITcpSocket*     getSocket(const DHashString& i_SocketName, Bool& o_bFinished);

    private:
        class DTcpClient_Impl;
        DTcpClient_Impl*   me;
    };

    class DOME_CORE_API DTcpServer
    {
    public:
        DTcpServer(Int i_Port);
        ~DTcpServer();

        DResult         stop();

        ITcpSocket*     getSocket();

    private:
        class DTcpServer_Impl;
        DTcpServer_Impl*   me;
    };
}



DOME_NAMESPACE_END