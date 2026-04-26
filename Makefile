CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g

SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)
TARGET = out/exchange

$(TARGET): $(OBJS)
	mkdir -p out
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

test: $(TEST_OBJS)
	g++ -std=c++17 test/exchange_test.cpp src/Exchange.cpp src/Order.cpp src/OrderBook.cpp -lgtest -lgtest_main -o test_exchange


build/%.o: src/%.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
