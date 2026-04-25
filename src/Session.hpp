// Types.hpp should include the most used library
#pragma once
#include"Types.hpp"
#include"Exchange.hpp"
#include<vector>

class Session {
public:
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
    virtual std::unique_ptr<Session> create(int fd, uint32_t bufsize) = 0;
};

class OrderSession : public Session {
public:
    OrderSession(int fd, Exchange& exchange, uint32_t bufsize) : fd_(fd), exchange_(exchange) 
        { buf_.reserve(bufsize); };
    void ProcessMessage() override;
    Exchange& exchange_;
};

class OrderSessionFactory : public SessionFactory {
public:
    OrderSessionFactory(Exchange& exchange) : exchange_(exchange) {};
    Exchange& exchange_;
    // The entrance for every request from a client
    std::unique_ptr<Session> create(int fd, uint32_t bufsize = 1024) override;


};