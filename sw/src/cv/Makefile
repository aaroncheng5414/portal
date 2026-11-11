CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
CREATE_BUILD := if not exist build mkdir build
else
EXEEXT :=
CREATE_BUILD := mkdir -p build
endif

TEST_BIN := build/tracker_tests$(EXEEXT)

.PHONY: all test

all: $(TEST_BIN)

$(TEST_BIN): src/tracker.cpp include/portal/tracker.hpp include/portal/config.h tests/tracker_tests.cpp
	$(CREATE_BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/tracker.cpp tests/tracker_tests.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)
