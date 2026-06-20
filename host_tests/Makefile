CXX ?= g++
CORE := ../components/eybond_collector
CXXFLAGS ?= -std=c++17 -g -O1 -Wall -Wextra -Werror -I..
CXXFLAGS += -I../components/eybond_collector

SRCS := $(wildcard $(CORE)/*.cpp) $(wildcard test_*.cpp) main.cpp

build/run_tests: $(SRCS) $(wildcard $(CORE)/*.h) minitest.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@

build/vector_dump: vector_dump.cpp $(wildcard $(CORE)/*.cpp) $(wildcard $(CORE)/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) vector_dump.cpp $(wildcard $(CORE)/*.cpp) -o $@

build/host_sim: host_sim.cpp $(wildcard $(CORE)/*.cpp) $(wildcard $(CORE)/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) host_sim.cpp $(filter-out test_%.cpp main.cpp vector_dump.cpp,$(wildcard $(CORE)/*.cpp)) -lutil -o $@

.PHONY: test clean
test: build/run_tests
	./build/run_tests

clean:
	rm -rf build
