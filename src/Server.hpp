#pragma once
#include<cstdint>
#include<unordered_map>
#include"Session.hpp"
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
    // TODO: 改成 reference
    SessionFactory* factory_;
    std::unordered_map<int, std::unique_ptr<Session>> sessions_;

public:
    // Q: 為什麼一開始傳入 SessionFactory 物件不可以，指標就可以？因為不能建立 abstract type，但是 pointer 可能是指向 subclass 是實體物件？
    Server(uint32_t port, SessionFactory* factory) : port_(port), factory_(factory) {};
    bool run();

/* 
process of creating a server
1. create socket
2. bind to a port
3. listen
*/

};