#pragma once
#include<cstdint>
#include<unordered_map>
#include"Session.hpp"
#include"Exchange.hpp"
// An Exchange Server
/* 
my thought on creating a class
1. Need to think of what function is this class providing
2. To provide these functions, what member var and func is needed
3. visibility of vars and funcs
4. maybe think of abstraction? should it be a baseclass?

*/

/* 
what I want for my server class
1. only listen and accept client

maybe this server class should only handle connection
the logic of processing request should be another class called session?
I think the libfon9 framework has a "device" and "session" arcitechure
device is a wrapper of socket, and it owns sessions?


3. 
*/


class Server {
private:
    uint32_t port_;
    Exchange& exchange_;
    std::unordered_map<int, std::unique_ptr<Session>> sesList_;

public:
    Server(uint32_t port, Exchange& exchange) : port_(port), exchange_(exchange) {};
    bool run();

/* 
process of creating a server
1. create socket
2. bind to a port
3. listen
*/

};