#include"Server.hpp"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <iostream>

bool Server::run() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        perror("socket");


    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
        perror("setsockopt");
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        perror("bind");
    if (listen(listen_fd, SOMAXCONN) < 0)
        perror("listen");

    int epfd = epoll_create1(0);

    /* 
        struct epoll_event {
            uint32_t events;   // 你要監控什麼事件（EPOLLIN、EPOLLOUT 等）
            epoll_data_t data; // 你自己存的資料，通常用 data.fd
        };
        union epoll_data_t {
            int      fd;
            uint32_t u32;
            uint64_t u64;
            void*    ptr;  // 進階用法，可以存任何東西
        };
    */
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        perror("epoll_ctl");
        return false;
    }

    epoll_event events[64];
    while (true) {
        printf("等待事件...\n");
        // 1. listen for both new connection and new data
        int n = epoll_wait(epfd, events, 64, -1);
        if (n < 0) {
            perror("epoll wait");
            return false;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                // new connection
                int client_fd = accept(listen_fd, nullptr, nullptr);
                printf("新連線: client_fd = %d\n", client_fd);
                // Q: 為什麼這裡一定要傳入1024，我不是OrdersessionFactory有default?
                sessions_[client_fd] = factory_->create(client_fd, 1024);
                // sessions_[client_fd] = std::make_unique<OrderSession>(client_fd, exchange_);

                /* 
                    TODO: Factory
                    new connection -> new session
                    how to create a new session?
                    maybe use a session factory? session factory is derived from a base factory class?
                    and this should be a template class such as Factory<Session>, Factory<Order> etc.
                */

                epoll_event ev;
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                    perror("epoll ctl");
                    return false;
                }
            }
            else {
                // connected
                char buf[1024];
                int n = read(fd, buf, sizeof(buf));

                // client 斷線
                if (n == 0) {
                    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
                        perror("epoll ctl");
                        return false;
                    }
                    sessions_.erase(fd);
                    close(fd);
                }
                else {
                    std::cout << "n=" << n << std::endl;
                    sessions_[fd]->OnRecvData(buf, n);
                    printf("收到： %s\n", buf);
                }
            }
        }
        
    }


};