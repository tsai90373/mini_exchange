#include"Server.hpp"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>


bool Server::run() {
    // Q: what is 0?
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    // make sure the data is cleaned
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));

    listen(listen_fd, SOMAXCONN);

    // Q: 這啥？epoll也是fd?
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
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    epoll_event events[64];
    while (true) {
        printf("等待事件...\n");
        // 1. listen for both new connection and new data
        int n = epoll_wait(epfd, events, 64, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                // new connection
                int client_fd = accept(listen_fd, nullptr, nullptr);
                printf("新連線: client_fd = %d\n", client_fd);
                epoll_event ev;
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                // Q: 重複使用變數名稱？
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);

            }
            else {

                // connected
                // int total_bytes = parse_header();
                char buf[1024];

                // 代表有資料，需要先知道資料有多少bytes，並且要拿到確認大小的bytes才算結束
                // while (total_bytes > 0) {
                int n = read(fd, buf, sizeof(buf));

                // client 斷線
                if (n == 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
                else {
                    buf[n] = '\0';
                    printf("收到： %s\n", buf);
                }

                    // memcpy(fd.buffer, buf, n);
                    // total_bytes -= n;
                    // memset(buf, 0, sizeof(buf));
                // }
            }
        }
        
    }


};