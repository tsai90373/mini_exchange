#pragma once

#include<map>
#include"Types.hpp"
#include"OrderBook.hpp"


using OrderBooks = std::map<Symbol, OrderBook*>;

class Exchange {
private:
    OrderBooks books_;
public:
    bool SendNew(Order&);

};