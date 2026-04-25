#include<cstring>
#include<cstdio>
#include<unistd.h>
#include<iostream>
#include"Types.hpp"
#include"Session.hpp"
#include"Order.hpp"
#include "Wire.hpp"

std::unique_ptr<Session> OrderSessionFactory::create(int fd, uint32_t bufsize) {
    return  std::make_unique<OrderSession>(fd, exchange_, bufsize);
}

bool Session::OnRecvData(char* buf, int n) {
    // suppose the first 4 bytes contains the total size of packet
    // totalsize should be a member variable otherwise it can not keep its state?

    buf_.insert(buf_.end(), buf, buf + n);
    // buffer based
    while (buf_.size() >= 4) {
        uint32_t msgSize;
        memcpy(&msgSize, buf_.data(), 4);
        if (buf_.size() < msgSize)
            break;
        ProcessMessage();
        buf_.erase(buf_.begin(), buf_.begin() + msgSize);  // 移除已處理的
    }
    return true;
}

void OrderSession::ProcessMessage() {
    printf("解讀完成");
    OrderNewMsg* msg = reinterpret_cast<OrderNewMsg*>(buf_.data());
    Order requestNew(msg->ordId, msg->symbId, (msg->side == 'B') ? Side::Buy : Side::Sell, msg->price, msg->qty);

    printf("收到新單: ordId=%u price=%lu qty=%u\n", 
    requestNew.ordId_, requestNew.price_, requestNew.iniQty_);
    
    ReportList ret = exchange_.sendNew(requestNew);
    std::cout << "回報數量: " << ret.size() << std::endl;
    int bytes = write(fd_, ret.data(), ret.size() * sizeof(ExecReport));
    if (bytes < 0)
        perror("write");
    std::cout << "寫入 " << bytes << "Bytes" << std::endl;
}

