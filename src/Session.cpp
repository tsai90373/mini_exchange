#include<cstring>
#include<cstdio>
#include<unistd.h>
#include<iostream>
#include"Types.hpp"
#include"Session.hpp"
#include"Order.hpp"
#include"Wire.hpp"
#include"Timestamp.hpp"

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
    // ts0: header 解完、body 尚未處理，是訂單進系統最早的時間點
    const uint64_t recv_ts = now_ns();

    printf("解讀完成");
    ReportList ret;
    MsgHeader* hdr = reinterpret_cast<MsgHeader*>(buf_.data());

    if (hdr->msgType == MsgType::OrderNew) {
        OrderNewRequest* req = reinterpret_cast<OrderNewRequest*>(buf_.data());
        OrderNewBody bdy = req->body;
        Order requestNew(bdy.symbId, (bdy.side == 'B') ? Side::Buy : Side::Sell, bdy.price, bdy.qty);
        requestNew.recv_ts = recv_ts;
        printf("收到新單: ordId=%u price=%lu qty=%u\n",
            requestNew.ordId_, requestNew.price_, requestNew.iniQty_);
        ret = exchange_.sendNew(requestNew);
    }
    else if (hdr->msgType == MsgType::OrderChg) {
        OrderChgRequest* req = reinterpret_cast<OrderChgRequest*>(buf_.data());
        OrderChgBody bdy = req->body;
    }

    // ts_send: write() 前打，與 recv_ts 差值 = e2e exchange latency
    const uint64_t send_ts = now_ns();
    for (auto& rpt : ret) {
        rpt.recv_ts = recv_ts;
        rpt.send_ts = send_ts;
    }
    if (!ret.empty())
        exchange_.e2e_latency_.record(send_ts - recv_ts);

    std::cout << "回報數量: " << ret.size() << std::endl;
    int bytes = write(fd_, ret.data(), ret.size() * sizeof(ExecReport));
    if (bytes < 0)
        perror("write");
    std::cout << "寫入 " << bytes << "Bytes" << std::endl;
}

