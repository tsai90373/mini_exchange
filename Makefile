CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g

SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)
TARGET = out/exchange

$(TARGET): $(OBJS)
	mkdir -p out
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

build/%.o: src/%.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
