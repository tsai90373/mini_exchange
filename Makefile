all:
	cmake -S . -B build && cmake --build build

test: all
	./build/out/test_exchange

clean:
	rm -rf build
