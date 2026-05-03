#include<cstring>
#include<cstdio>
#include<unistd.h>
#include<iostream>
#include"engine/Types.hpp"
#include"engine/Order.hpp"
#include"Session.hpp"
#include"Wire.hpp"
#include"tools/Timestamp.hpp"

std::unique_ptr<Session> OrderSessionFactory::Create(int fd) {
    return  std::make_unique<OrderSession>(fd, exchange_, bufsize_);
}

bool Session::OnRecvData(char* buf, int n) {
    // suppose the first 4 bytes contains the total size of packet
    // totalsize should be a member variable otherwise it can not keep its state?

    buf_.insert(buf_.end(), buf, buf + n);

    // buffer based
    // 因為可能一次收到兩筆訊息，所以不能用 if 要用 while 把訊息都處理完
    uint32_t header_size = sizeof(MsgHeader);
    while (buf_.size() >= header_size) {
        MsgHeader* header = reinterpret_cast<MsgHeader*>(buf_.data());
        uint32_t msg_size = header->size;
        // Request 尚未到齊
        if (buf_.size() < header_size + msg_size)
            break;
        ProcessMessage();
        buf_.erase(buf_.begin(), buf_.begin() + header_size + msg_size);  // 移除已處理的
    }
    return true;
}

void OrderSession::ProcessMessage() {
    // ts0: header 解完、body 尚未處理，是訂單進系統最早的時間點
    const uint64_t recv_ts = NowNs();

    printf("解讀完成");
    ReportList ret;
    MsgHeader* hdr = reinterpret_cast<MsgHeader*>(buf_.data());

    if (hdr->msg_type == MsgType::OrderNew) {
        OrderNewRequest* req = reinterpret_cast<OrderNewRequest*>(buf_.data());
        OrderNewBody bdy = req->body;
        Order request_new(bdy.symb_id, (bdy.side == 'B') ? Side::kBuy : Side::kSell, bdy.price, bdy.qty);
        request_new.recv_ts = recv_ts;
        printf("收到新單: ordId=%u price=%lu qty=%u\n",
            request_new.ord_id_, request_new.price_, request_new.ini_qty_);
        ret = exchange_.SendNew(request_new);
    }
    else if (hdr->msg_type == MsgType::OrderChg) {
        OrderChgRequest* req = reinterpret_cast<OrderChgRequest*>(buf_.data());
        OrderChgBody bdy = req->body;
    }

    // ts_send: write() 前打，與 recv_ts 差值 = e2e exchange latency
    const uint64_t send_ts = NowNs();
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

