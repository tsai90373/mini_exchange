#pragma once
#include<cstdint>
#include<unordered_map>
#include<memory>

// forward declaration
class Session;
class SessionFactory;

class Server {
private:
    uint32_t port_;
    // TODO: 改成 reference
    SessionFactory* factory_;
    std::unordered_map<int, std::unique_ptr<Session>> sessions_;

public:
    Server(uint32_t port, SessionFactory* factory) : port_(port), factory_(factory) {};
    ~Server();
    bool run();
};
