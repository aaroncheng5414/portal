CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
CMAKE ?= cmake
CMAKE_ARGS ?=
CMAKE_BUILD_DIR ?= build/service
BUILD_JOBS ?= 2

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
CREATE_BUILD := if not exist build mkdir build
CMAKE_GENERATOR_ARG := -G "MinGW Makefiles"
else
EXEEXT :=
CREATE_BUILD := mkdir -p build
endif

TEST_BIN := build/tracker_tests$(EXEEXT)

.PHONY: all test service

all: $(TEST_BIN)

$(TEST_BIN): src/tracker.cpp include/portal/tracker.hpp include/portal/config.h tests/tracker_tests.cpp
	$(CREATE_BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/tracker.cpp tests/tracker_tests.cpp -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

service:
	$(CMAKE) -S . -B $(CMAKE_BUILD_DIR) $(CMAKE_GENERATOR_ARG) -DCMAKE_BUILD_TYPE=Release -DBUILD_SERVICE=ON $(CMAKE_ARGS)
	$(CMAKE) --build $(CMAKE_BUILD_DIR) --target portal_cv --parallel $(BUILD_JOBS)
