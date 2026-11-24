CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic
LDFLAGS  ?= -pthread

all: test bench

test: test_spsc.cpp spsc_ring.hpp
	$(CXX) $(CXXFLAGS) -O2 test_spsc.cpp -o test_spsc $(LDFLAGS)
	./test_spsc

# ThreadSanitizer: reports data races. Try changing release/acquire to relaxed
# in spsc_ring.hpp and re-run this to see what it catches.
test-tsan: test_spsc.cpp spsc_ring.hpp
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread test_spsc.cpp -o test_spsc_tsan $(LDFLAGS)
	./test_spsc_tsan

# AddressSanitizer + UBSan: reports out-of-bounds and undefined behaviour.
test-asan: test_spsc.cpp spsc_ring.hpp
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined test_spsc.cpp -o test_spsc_asan $(LDFLAGS)
	./test_spsc_asan

bench: bench.cpp spsc_ring.hpp
	$(CXX) $(CXXFLAGS) -O2 bench.cpp -o bench $(LDFLAGS)

run-bench: bench
	./bench 100000 5 --pin

clean:
	rm -f test_spsc test_spsc_tsan test_spsc_asan bench

.PHONY: all test test-tsan test-asan run-bench clean
