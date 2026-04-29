#include<cstring>
#include<cstdio>
#include<unistd.h>
#include<iostream>
#include"Types.hpp"
#include"Session.hpp"
#include"Order.hpp"
#include "Wire.hpp"

std::unique_ptr<Session> OrderSessionFactory::create(int fd) {
    return  std::make_unique<OrderSession>(fd, exchange_, bufsize_);
}

bool Session::OnRecvData(char* buf, int n) {
    // suppose the first 4 bytes contains the total size of packet
    // totalsize should be a member variable otherwise it can not keep its state?

    buf_.insert(buf_.end(), buf, buf + n);

    // buffer based
    // 因為可能一次收到兩筆訊息，所以不能用 if 要用 while 把訊息都處理完
    uint32_t headerSize = sizeof(MsgHeader);
    while (buf_.size() >= headerSize) {
        MsgHeader* header = reinterpret_cast<MsgHeader*>(buf_.data());
        uint32_t msgSize = header->size;
        // Request 尚未到齊
        if (buf_.size() < headerSize + msgSize)
            break;
        ProcessMessage();
        buf_.erase(buf_.begin(), buf_.begin() + headerSize + msgSize);  // 移除已處理的
    }
    return true;
}

void OrderSession::ProcessMessage() {
    printf("解讀完成");
    ReportList ret;
    MsgHeader* hdr = reinterpret_cast<MsgHeader*>(buf_.data());
    
    if (hdr->msgType == MsgType::OrderNew) {
        OrderNewRequest* req = reinterpret_cast<OrderNewRequest*>(buf_.data());
        OrderNewBody bdy = req->body;
        Order requestNew(bdy.symbId, (bdy.side == 'B') ? Side::Buy : Side::Sell, bdy.price, bdy.qty);
        printf("收到新單: ordId=%u price=%lu qty=%u\n", 
        requestNew.ordId_, requestNew.price_, requestNew.iniQty_);
        ret = exchange_.sendNew(requestNew);
    }
    else if (hdr->msgType == MsgType::OrderChg) {
        OrderChgRequest* req = reinterpret_cast<OrderChgRequest*>(buf_.data());
        OrderChgBody bdy = req->body;
    }

    std::cout << "回報數量: " << ret.size() << std::endl;
    int bytes = write(fd_, ret.data(), ret.size() * sizeof(ExecReport));
    if (bytes < 0)
        perror("write");
    std::cout << "寫入 " << bytes << "Bytes" << std::endl;
}

