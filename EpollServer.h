#ifndef _GWS_EPOLL_SERVER_H_
#define _GWS_EPOLL_SERVER_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>

#include "SockHandle.h"

namespace GWS
{
    typedef std::function<void(std::shared_ptr<SockHandle> sock)> OpenFunc;
    typedef std::function<void(std::shared_ptr<SockHandle> sock, const char* msg, size_t len, int opCode)> MessageFunc;
    typedef std::function<void(std::shared_ptr<SockHandle> sock)> CloseFunc;
    class EpollServer
    {
    private:

    public:
        static OpenFunc openHandle;
        static MessageFunc messageHandle;
        static CloseFunc closedHandle;
        EpollServer(OpenFunc openHandle, MessageFunc messageHandle, CloseFunc closedHandle);
        int Run(char* port);
        ~EpollServer();
    };

} // namespace GWS






#endif