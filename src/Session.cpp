#include<cstring>
#include<cstdio>
#include<unistd.h>
#include"Types.hpp"
#include"Session.hpp"
#include"Order.hpp"
#include<iostream>





bool Session::OnRecvData(char* buf, int n) {
    // suppose the first 4 bytes contains the total size of packet
    // totalsize should be a member variable otherwise it can not keep its state?
    if (recvSt_ == RevState::WaitingHeader) {
        memcpy(&totalSize_, buf, 4);
        recvSt_ = RevState::WaitingBody;
    }
    // push_back to buf_
    buf_.insert(buf_.end(), buf, buf + n);
    totalSize_ -= n;
    // process data after packet is fully received
    if (totalSize_ == 0) {
        printf("解讀完成");
        Order requestNew = *reinterpret_cast<Order*>(buf_.data());
        printf("收到新單: ordId=%u price=%lu qty=%u\n", 
        requestNew.ordId_, requestNew.price_, requestNew.iniQty_);
        
        ReportList ret = exchange_.SendNew(requestNew);
        std::cout << "回報數量: " << ret.size() << std::endl;
        int bytes = write(fd_, ret.data(), ret.size() * sizeof(ExecReport));
        std::cout << "寫入 " << bytes << "Bytes" << std::endl;
        // maybe send data to exchange and receivce report from exchange

        // and send report back to client: write(fd, )
        buf_.clear();
        recvSt_ = RevState::WaitingHeader;
    }
    return true;
}



