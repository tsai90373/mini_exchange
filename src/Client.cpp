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

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg_.port_);
    inet_pton(AF_INET, cfg_.Ip_.c_str(), &addr.sin_addr);

    connect(fd_, (sockaddr*)&addr, sizeof(addr));

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
        SendNew(ord);
    }
}

bool Client::SendNew(Order& ord) {
        printf("送出: ordId=%u price=%lu qty=%u side=%d\n", 
           ord.ordId_, ord.price_, ord.iniQty_, (int)ord.side_);
    // OrderNewMsg msg;
    // msg.size   = sizeof(OrderNewMsg) - sizeof(msg.size);
    // msg.ordId  = ord.ordId_;
    // msg.symbId = ord.symbId_;
    // msg.side   = (ord.side_ == Side::Buy) ? 'B' : 'S';
    // msg.price  = ord.price_;
    // msg.qty    = ord.iniQty_;
    write(fd_, &ord, sizeof(ord));
    return true;
}
