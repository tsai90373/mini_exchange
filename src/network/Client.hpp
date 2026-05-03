#pragma once
#include<string>
#include"engine/Types.hpp"
#include"engine/Order.hpp"
#include"Wire.hpp"

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
    void Run();
    bool SendNew(OrderNewRequest&);
    bool SendChg(ChgRequest&);
    bool SendDel(OrdId);
};