cmake_minimum_required(VERSION 3.14)
project(mini_exchange)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

add_executable(exchange
    src/main.cpp
    src/Exchange.cpp
    src/Client.cpp
    src/Order.cpp
    src/OrderBook.cpp
    src/Server.cpp
    src/Session.cpp
)

add_executable(test_exchange
    test/exchange_test.cpp
    src/Exchange.cpp
    src/Order.cpp
    src/OrderBook.cpp
)

find_package(GTest REQUIRED)
target_link_libraries(test_exchange GTest::GTest GTest::Main)
