// Types.hpp should include the most used library
#pragma once
#include"Types.hpp"
#include"Exchange.hpp"
#include<vector>


enum class RevState {
    WaitingHeader,
    WaitingBody
};

class Session {
public:
    Session(int fd, Exchange& exchange, uint32_t bufsize = 1024) 
        : fd_(fd), exchange_(exchange) { buf_.reserve(bufsize); };
    // Using vector instead of char array to avoid buffer overflow
    std::vector<char> buf_;
    Exchange& exchange_;
    // Session needs to know its fd so that is can write(fd, ) after processing data
    int fd_;
    // The entrance for every request from a client
    bool OnRecvData(char* buf, int n);
    int totalSize_;

    RevState recvSt_ = RevState::WaitingHeader;

};