cmake_minimum_required(VERSION 3.14)
project(mini_exchange)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    hdr_histogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG        0.11.8
)
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_LOG_REQUIRED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(hdr_histogram)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# Debug build flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer  # ← 為了 stack trace 漂亮
        -g
    )
    add_link_options(
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

set(EXCHANGE_SOURCES
    src/engine/Exchange.cpp
    src/engine/Order.cpp
    src/engine/OrderBook.cpp
)

set(NETWORK_SOURCES
    src/network/Client.cpp
    src/network/Server.cpp
    src/network/Session.cpp
)

add_executable(exchange
    src/main.cpp
    src/main.cpp
    ${EXCHANGE_SOURCES}
    ${NETWORK_SOURCES}
)

add_executable(test_exchange
    test/exchange_test.cpp
    ${EXCHANGE_SOURCES}
)

target_link_libraries(exchange hdr_histogram_static)
target_include_directories(exchange PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${hdr_histogram_SOURCE_DIR}/include)

target_link_libraries(test_exchange hdr_histogram_static)
target_include_directories(test_exchange PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${hdr_histogram_SOURCE_DIR}/include)

find_package(GTest QUIET)
if(GTest_FOUND)
    target_link_libraries(test_exchange GTest::GTest GTest::Main)
else()
    message(STATUS "GTest not found — skipping test_exchange")
endif()
