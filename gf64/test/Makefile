# GF64 Test Makefile
# Builds and runs GF(2^64) arithmetic and SIMD tests

CC := gcc
CXX := g++
CFLAGS := -std=c99 -Wall -Wextra -O2 -march=native -D_POSIX_C_SOURCE=200112L -D_GNU_SOURCE
CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -march=native -D_POSIX_C_SOURCE=200112L -D_GNU_SOURCE
LDFLAGS :=

# Source files
GF64_SOURCES := ../gf64_single.c ../gf64_region_scalar.c ../gf64_region_ssse3.c ../gf64_region_ssse3_arr.c ../gf64_region_avx2.c ../gf64_region_avx2_arr.c ../gf64_region_avx512.c ../gf64_region_avx512_arr.c ../gf64_dispatch.c ../gf64_invert.c
GF64_INCLUDES := -I..

# Test sources
TEST_SOURCES := test.c
TEST_ALL_SOURCES := test_all.c

.PHONY: all test clean

all: test test_all

test: $(TEST_SOURCES) $(GF64_SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_SOURCES) $(GF64_SOURCES)

test_all: $(TEST_ALL_SOURCES) $(GF64_SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_ALL_SOURCES) $(GF64_SOURCES)

clean:
	rm -f test test_all
