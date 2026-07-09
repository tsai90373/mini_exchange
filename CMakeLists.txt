cmake_minimum_required(VERSION 3.14)
project(tickengine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----- Build types ---------------------------------------------------------
# Default to Debug if user did not specify
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type: Debug | RelWithDebInfo | Release | ASan | UBSan | TSan" FORCE)
endif()

# Custom sanitizer build types (ASan + TSan are mutually exclusive — separate types)
set(CMAKE_CXX_FLAGS_ASAN
    "-O1 -g -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined"
    CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_ASAN
    "-fsanitize=address -fsanitize=undefined"
    CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_UBSAN
    "-O1 -g -fno-omit-frame-pointer -fsanitize=undefined"
    CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_UBSAN
    "-fsanitize=undefined"
    CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_TSAN
    "-O1 -g -fno-omit-frame-pointer -fsanitize=thread"
    CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_TSAN
    "-fsanitize=thread"
    CACHE STRING "" FORCE)

mark_as_advanced(
    CMAKE_CXX_FLAGS_ASAN  CMAKE_EXE_LINKER_FLAGS_ASAN
    CMAKE_CXX_FLAGS_UBSAN CMAKE_EXE_LINKER_FLAGS_UBSAN
    CMAKE_CXX_FLAGS_TSAN  CMAKE_EXE_LINKER_FLAGS_TSAN)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# ----- Dependencies --------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    hdr_histogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG        0.11.8
)
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_LOG_REQUIRED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(hdr_histogram)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)

FetchContent_Declare(
    cppzmq
    GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
    GIT_TAG        v4.10.0
)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cppzmq)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ----- Targets ------------------------------------------------------------
add_executable(bridge_demo
    src/bridge/main.cc
    src/core/Engine.cpp
    src/core/Backend.cpp
    src/core/Order.cpp
    src/adapter/FileLogSubscriber.cpp
    src/adapter/PythonFeedAdapter.cpp
    src/adapter/PythonOrderAdapter.cpp
    src/core/InboundEvents.hpp)
target_link_libraries(bridge_demo PRIVATE cppzmq nlohmann_json::nlohmann_json)
target_include_directories(bridge_demo PRIVATE src)

# Smoke test for PythonFeedAdapter — no ZMQ link, hand-crafted JSON fixtures.
add_executable(adapter_smoke
    tests/adapter_smoke.cc
    src/core/Engine.cpp
    src/core/Backend.cpp
    src/core/Order.cpp
    src/adapter/PythonFeedAdapter.cpp
    src/core/InboundEvents.hpp)
target_link_libraries(adapter_smoke PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(adapter_smoke PRIVATE src)

# Smoke test for the order state machine (Engine::OnOrderAck / OnFill) —
# core only, no adapters, no JSON. Asserts OrderRaw chains via Backend::GetBySno.
add_executable(order_lifecycle_smoke
    tests/order_lifecycle_smoke.cc
    src/core/Engine.cpp
    src/core/Backend.cpp
    src/core/Order.cpp)
target_include_directories(order_lifecycle_smoke PRIVATE src)

# Smoke test for FileLogSubscriber — full lifecycle through Engine, then reads
# the jsonl back from disk and verifies every line.
add_executable(logwriter_smoke
    tests/logwriter_smoke.cc
    src/core/Engine.cpp
    src/core/Backend.cpp
    src/core/Order.cpp
    src/adapter/FileLogSubscriber.cpp)
target_link_libraries(logwriter_smoke PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(logwriter_smoke PRIVATE src)

# Smoke test for PythonOrderAdapter — no ZMQ link, drives Engine.SendNew/SendChg
# with a recording lambda and inspects the JSON envelopes.
add_executable(order_adapter_smoke
    tests/order_adapter_smoke.cc
    src/core/Engine.cpp
    src/core/Backend.cpp
    src/core/Order.cpp
    src/adapter/PythonOrderAdapter.cpp)
target_link_libraries(order_adapter_smoke PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(order_adapter_smoke PRIVATE src)
