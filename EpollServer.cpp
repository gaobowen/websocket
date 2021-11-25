#include "Collector.h"

#include "EpollServer.h"
#include <sys/ioctl.h>

#define MAXEVENTS 64

using namespace GWS;

static int make_socket_non_blocking(int sfd);

//函数:
//功能:创建和绑定一个TCP socket
//参数:端口
//返回值:创建的socket
static int create_and_bind(char* port)
{
    struct addrinfo hints;
    struct addrinfo* result, * rp;
    int s, sfd;

    // //创建listen socket  
    // if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    //     perror("sockfd\n");
    //     exit(1);
    // }
    // make_socket_non_blocking(sfd);
    // bzero(&local, sizeof(local));
    // local.sin_family = AF_INET;
    // local.sin_addr.s_addr = htonl(INADDR_ANY);;
    // local.sin_port = htons(9001);
    // if (bind(sfd, (struct sockaddr*)&local, sizeof(local)) < 0) {
    //     perror("bind\n");
    //     exit(1);
    // }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            /* We managed to bind successfully! */
            break;
        }

        close(sfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}


//函数
//功能:设置socket为非阻塞的
static int make_socket_non_blocking(int sfd) {
    int flags, s;
    //得到文件状态标志
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get");
        return -1;
    }

    //设置文件状态标志
    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl set");
        int on = 1;
        if (ioctl(sfd, FIONBIO, &on) == -1)
        {
            perror("ioctl FIONBIO:");
            return -1;
        }
    }

    return 0;
}

void handle_close(SockHandle* handle) {
    if (handle) {
        SockReadAccessor accessor;
        if (collector.find(accessor, handle)) {
            auto h = accessor->second;
            close(h->fd);
            h->is_closing = true;
            if (h->OnClosed && !h->is_closed.load()) h->OnClosed(h);
            h->is_closed = true;
        }
        accessor.release();
        collector.erase(handle);
    }
}

namespace GWS
{
    OpenFunc EpollServer::openHandle = nullptr;
    MessageFunc EpollServer::messageHandle = nullptr;
    CloseFunc EpollServer::closedHandle = nullptr;

    EpollServer::EpollServer(OpenFunc openHandle, MessageFunc messageHandle, CloseFunc closedHandle) {
        EpollServer::openHandle = openHandle;
        EpollServer::messageHandle = messageHandle;
        EpollServer::closedHandle = closedHandle;
    }


    int EpollServer::Run(char* port) {
        int sfd, s;
        int efd;
        struct epoll_event event;
        struct epoll_event* events;

        sfd = create_and_bind(port);
        if (sfd == -1) abort();

        s = make_socket_non_blocking(sfd);
        if (s == -1) abort();

        s = listen(sfd, SOMAXCONN);
        if (s == -1)
        {
            perror("listen");
            abort();
        }

        printf("EpollServer Listening Port %s\n", port);

        //除了参数size被忽略外,此函数和epoll_create完全相同
        efd = epoll_create1(0);
        if (efd == -1)
        {
            perror("epoll_create");
            abort();
        }

        event.data.fd = sfd;
        event.events = EPOLLIN | EPOLLET;//读入,边缘触发方式
        s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
        if (s == -1)
        {
            perror("epoll_ctl");
            abort();
        }

        /* Buffer where events are returned */
        events = (epoll_event*)calloc(MAXEVENTS, sizeof(event));

        /* The event loop */
        while (1)
        {
            printf("start epoll_wait.\n");
            int n = epoll_wait(efd, events, MAXEVENTS, -1);
            for (int i = 0; i < n; i++)
            {
                //异常关闭 ||  (!(events[i].events & EPOLLIN))
                if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
                {
                    /* An error has occured on this fd, or the socket is not
                       ready for reading (why were we notified then?) */

                    if (sfd == events[i].data.fd) {
                        fprintf(stderr, "epoll error\n");
                        close(events[i].data.fd);
                    }
                    else if (auto handle = reinterpret_cast<GWS::SockHandle*>(events[i].data.ptr)) {
                        epoll_ctl(efd, EPOLL_CTL_DEL, handle->fd, 0);
                        handle_close(handle);
                        events[i].data.ptr = nullptr;
                    }
                    continue;
                }
                else if (sfd == events[i].data.fd) //有新建连接
                {
                    //多个连接过来时，epoll-wait只触发一次，需要循环accept
                    while (1) {
                        struct sockaddr in_addr;
                        socklen_t in_len;
                        int infd; //客户端句柄
                        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                        in_len = sizeof in_addr;
                        infd = accept(sfd, &in_addr, &in_len);
                        if (infd == -1) {
                            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                                break; /* We have processed all incoming connections. */
                            }
                            else {
                                perror("accept");
                                break;
                            }
                        }

                        //将地址转化为主机名或者服务名 flag参数:以数字名返回主机地址和服务地址
                        s = getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
                        if (s == 0) {
                            printf("Accepted connection on descriptor %d "
                                "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                        }

                        /* Make the incoming socket non-blocking and add it to the list of fds to monitor. */
                        s = make_socket_non_blocking(infd);
                        if (s == -1) {
                            //abort();
                            printf("connection non_blocking error. \n");
                            close(infd);
                            break;
                        }


                        auto handle = new GWS::SockHandle();
                        handle->fd = infd;
                        handle->efd = efd;

                        handle->OnOpen = EpollServer::openHandle;
                        handle->OnMessage = EpollServer::messageHandle;
                        handle->OnClosed = EpollServer::closedHandle;
                        SockAccessor accessor;
                        SockPair pair(handle, shared_ptr<SockHandle>(handle));
                        if (!GWS::collector.insert(accessor, pair)) {
                            close(infd);
                        }
                        else {

                        }
                        accessor.release();

                        event.data.ptr = handle;
                        event.events = EPOLLIN | EPOLLET;
                        s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event); //客户端句柄添加到epoll-wait循环
                        if (s == -1) {
                            perror("epoll_ctl");
                            abort();
                        }

                    }
                    continue;
                }

                auto handle = (SockHandle*)events[i].data.ptr;
                if (!handle) continue;
                if (handle->is_closing) {
                    epoll_ctl(efd, EPOLL_CTL_DEL, handle->fd, 0);
                    handle_close(handle);
                    events[i].data.ptr = nullptr;
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    int closed = 0;
                    ssize_t count = 0;
                    size_t buflen = 1024 * 32;
                    char buf[buflen];

                    int rdfd = handle->fd;

                    while (1) {
                        count = read(rdfd, buf, buflen);
                        if (count > 0) {
                            handle->ReadBuffer(buf, count);
                        }

                        if (count == -1) {
                            if (errno != EAGAIN) {
                                perror("read error");
                                closed = 1;
                            } //errno == EAGAIN 数据读完
                            break;
                        }
                        else if (count == 0) { //远程客户端关闭
                            printf("client closed.\n");
                            closed = 1;
                            break;
                        }

                        printf("recv %d total=%d\n", rdfd, count);
                    }

                    if (closed || handle->is_closing) {
                        //printf("Closed connection on descriptor\n");
                        epoll_ctl(efd, EPOLL_CTL_DEL, handle->fd, 0);
                        handle_close(handle);
                        events[i].data.ptr = nullptr;
                        continue;
                    }
                }

                if (events[i].events & EPOLLOUT) {
                    if (handle->is_closing || handle->is_closed) {
                        handle->is_closing = true;
                        epoll_ctl(efd, EPOLL_CTL_DEL, handle->fd, 0);
                        handle_close(handle);
                        events[i].data.ptr = nullptr;
                        continue;
                    }

                    //printf("ready to write.\n");
                    char* buf = std::get<0>(handle->remainSend);
                    size_t total = std::get<1>(handle->remainSend);
                    size_t offset = std::get<2>(handle->remainSend);
                    if (!buf && handle->sendQueue.size_approx() > 0) {
                        tuple<char*, size_t> t;
                        if (handle->sendQueue.try_dequeue(t)) {
                            buf = std::get<0>(t);
                            total = std::get<1>(t);
                            offset = 0;
                            handle->remainSend = tuple<char*, size_t, size_t>(buf, total, offset);
                        }
                    }

                    //printf("aaaaa total %d, offset %d\n", total, offset);

                    while (offset < total) {
                        size_t left = total - offset;
                        size_t sendn = left > 1024 * 32 ? 1024 * 32 : left;
                        size_t nwrite = write(handle->fd, buf + offset, sendn);
                        printf("write len=%d\n", nwrite);
                        if (nwrite < 0) {
                            perror("write error");
                            if (nwrite == -1 && errno != EAGAIN) {
                                handle->is_closing = true;
                                event.data.ptr = handle;
                                event.events = EPOLLIN | EPOLLET;
                                epoll_ctl(efd, EPOLL_CTL_MOD, handle->fd, &event);
                            }
                            else if (nwrite == -1 && errno == EAGAIN) { //errno == EAGAIN buf 没有一次性发完
                                printf("write EAGAIN\n");
                            }
                            break;
                        }
                        offset += nwrite;
                        std::get<2>(handle->remainSend) = offset;
                        //printf("bbbbb total %d, offset %d\n", total, offset);
                    };

                    if (buf && offset == total) {
                        //printf("cccccc total %d, offset %d\n", total, offset);
                        handle->remainSend = tuple<char*, size_t, size_t>(nullptr, 0, 0);
                        free(buf);
                        buf = nullptr;
                        if (!handle->isHandshaked) {
                            handle->isHandshaked = true;
                            //printf("handle->OnOpen %p\n", handle->OnOpen);
                            if (handle->OnOpen != nullptr) {
                                SockReadAccessor accessor;
                                if (collector.find(accessor, handle)) {
                                    auto h = accessor->second;
                                    if (h->OnOpen) h->OnOpen(h);
                                }
                                accessor.release();
                            }
                        }
                    }

                    //还有数据等待发送
                    if (buf || handle->sendQueue.size_approx() > 0) {
                        event.data.ptr = handle;
                        event.events = EPOLLIN | EPOLLET | EPOLLOUT;
                        epoll_ctl(efd, EPOLL_CTL_MOD, handle->fd, &event);
                        //printf("22222222222\n");
                    }

                    //printf("write=%d, total=%d, data=%s\n", offset, total, std::string(buf, total).c_str());
                }
            }
        }

        free(events);
        close(efd);
        close(sfd);

        return EXIT_SUCCESS;
    }

    EpollServer::~EpollServer() {
    }

} // namespace GWS



