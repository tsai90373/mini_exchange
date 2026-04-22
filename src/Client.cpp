#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include "Client.hpp"
#include "Wire.hpp"


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
        OrderNewMsg msg;
        msg.size = sizeof(OrderNewMsg);
        msg.ordId = ordId;
        msg.symbId = id;
        msg.side = side;
        msg.price = price;
        msg.qty = qty;
        sendNew(msg);
    }
}

bool Client::sendNew(OrderNewMsg& msg) {
        printf("送出: ordId=%u price=%lu qty=%u side=%d\n", 
           msg.ordId, msg.price, msg.qty, (int)msg.side);
    if (write(fd_, &msg, sizeof(msg)) < 0) { 
        perror("write"); 
        return false; 
    }

    ExecReport rpt;
    if (read(fd_, &rpt, sizeof(rpt)) < 0) { 
        perror("read"); 
        return false; 
    }
    printf("回報: execType=%c ordId=%u qty=%u\n", rpt.execType, rpt.ordId, rpt.qty);

    return true;
}
