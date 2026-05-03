// Types.hpp should include the most used library
#pragma once
#include"engine/Types.hpp"
#include"engine/Exchange.hpp"
#include<vector>

class Session {
public:
    Session(int fd) : fd_(fd) {};
    bool OnRecvData(char* buf, int n); 
    // base class needs virtual desctructor
    virtual ~Session() = default;
    virtual void ProcessMessage() = 0;
    std::vector<char> buf_;
    int fd_;
};

class SessionFactory {
public:
    virtual ~SessionFactory() = default;
    virtual std::unique_ptr<Session> Create(int fd) = 0;
};

class OrderSession : public Session {
public:
    OrderSession(int fd, Exchange& exchange, uint32_t bufsize) : Session(fd), exchange_(exchange) 
        { buf_.reserve(bufsize); };
    void ProcessMessage() override;
    Exchange& exchange_;
};

class OrderSessionFactory : public SessionFactory {
public:
    OrderSessionFactory(Exchange& exchange) : exchange_(exchange) {};
    Exchange& exchange_;
    uint32_t bufsize_ = 1024;
    // The entrance for every request from a client
    std::unique_ptr<Session> Create(int fd) override;


};