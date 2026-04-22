// Types.hpp should include the most used library
#pragma once
#include"Types.hpp"
#include"Exchange.hpp"
#include<vector>


enum class RecvState {
    WaitingHeader,
    WaitingBody
};

class Session {
public:
   virtual bool OnRecvData(char* buf, int n) = 0; 
};

class SessionFactory {
public:
    virtual ~SessionFactory() = default;
    virtual std::unique_ptr<Session> create(int fd, uint32_t bufsize) = 0;
};

class OrderSession : public Session {
public:
    OrderSession(int fd, Exchange& exchange, uint32_t bufsize) : fd_(fd), exchange_(exchange) 
        { buf_.reserve(bufsize); };
    bool OnRecvData(char* buf, int n) override;
    int totalSize_;
    int fd_;
    // Using vector instead of char array to avoid buffer overflow
    // Q: 雖然可以避免 buffer overflow，但是仍然是在 heap 上面的操作，仍然可能 relocate，是否直接用 stack 上面的 array ?
    std::vector<char> buf_;
    Exchange& exchange_;
    RecvState recvSt_ = RecvState::WaitingHeader;
};

class OrderSessionFactory : public SessionFactory {
public:
    OrderSessionFactory(Exchange& exchange) : exchange_(exchange) {};
    Exchange& exchange_;
    // The entrance for every request from a client
    std::unique_ptr<Session> create(int fd, uint32_t bufsize = 1024) override;


};