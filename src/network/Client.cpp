#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include "Client.hpp"
#include "Wire.hpp"


void Client::Run() {
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
        std::string symb_id;
        SymbId id{};
        uint32_t price, qty;
        std::cin >> symb_id >> type >> price >> qty >> side;
        memcpy(id.data(), symb_id.data(), std::min(symb_id.size(), id.size()));

        Side side_enum = (side == 'B') ? Side::kBuy : Side::kSell;

        OrderNewRequest req;

        MsgHeader header;
        header.size = sizeof(OrderNewBody);
        header.msg_type = MsgType::OrderNew;
        req.header = header;

        OrderNewBody msg;
        msg.symb_id = id;
        msg.side = side;
        msg.price = price;
        msg.qty = qty;
        req.body = msg;

        SendNew(req);
    }
}

bool Client::SendNew(OrderNewRequest& req) {
        // printf("送出: ordId=%u price=%lu qty=%u side=%d\n", 
        //    req.price, req.qty, (int)req.side);
    if (write(fd_, &req, sizeof(req)) < 0) { 
        perror("write"); 
        return false; 
    }

    ExecReport rpt;
    if (read(fd_, &rpt, sizeof(rpt)) < 0) { 
        perror("read"); 
        return false; 
    }
    printf("回報: execType=%c ordId=%u qty=%u\n", rpt.exec_type, rpt.ord_id, rpt.qty);

    return true;
}
