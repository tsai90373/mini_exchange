#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include "Client.hpp"


void Client::run() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) { 
        perror("socket"); 
        return; 
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg_.port_);
    inet_pton(AF_INET, cfg_.ip_.c_str(), &addr.sin_addr);

    if (connect(fd_, (sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("connect"); 
        return; 
    }

    while (true) {
        char type, side;
        std::string symbId;
        SymbId id{};
        uint32_t ordId, price, qty;
        std::cin >> symbId >> type >> ordId >> price >> qty >> side;
        memcpy(id.data(), symbId.data(), std::min(symbId.size(), id.size()));

        Side side_enum = (side == 'B') ? Side::Buy : Side::Sell;
        Order ord(ordId, id, side_enum, price, qty);
        ord.size_ = sizeof(Order);
        sendNew(ord);
    }
}

bool Client::sendNew(Order& ord) {
        printf("送出: ordId=%u price=%lu qty=%u side=%d\n", 
           ord.ordId_, ord.price_, ord.iniQty_, (int)ord.side_);
    // OrderNewMsg msg;
    // msg.size   = sizeof(OrderNewMsg) - sizeof(msg.size);
    // msg.ordId  = ord.ordId_;
    // msg.symbId = ord.symbId_;
    // msg.side   = (ord.side_ == Side::Buy) ? 'B' : 'S';
    // msg.price  = ord.price_;
    // msg.qty    = ord.iniQty_;
    if (write(fd_, &ord, sizeof(ord)) < 0) { 
        perror("write"); return false; 
    }

    ExecReport rpt;
    if (read(fd_, &rpt, sizeof(rpt)) < 0) { 
        perror("read"); 
        return false; 
    }
    printf("回報: execType=%c ordId=%u qty=%u\n", rpt.execType, rpt.ordId, rpt.qty);

    return true;
}
