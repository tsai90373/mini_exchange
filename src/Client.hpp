#pragma once
#include"Types.hpp"
#include"Order.hpp"
#include<string>

class Client {
private:
    // connection param
    struct Config {
        std::string ip_;
        uint32_t port_;   
    };
    Config cfg_;
public:
    Client(Config cfg) : cfg_(std::move(cfg)) {};
    // Send() needs fd
    int fd_;
    void run();
    bool sendNew(Order&);
    bool sendChg(ChgRequest&);
    bool sendDel(OrdId);
};