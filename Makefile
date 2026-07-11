CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -march=native -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
BUILD := build
COMMON := src/dice.cpp src/scoring.cpp src/upper_states.cpp
SOLVER := $(COMMON) src/solver.cpp

.PHONY: all test bench clean

all: $(BUILD)/maxiyatzy-info $(BUILD)/maxiyatzy-tests $(BUILD)/maxiyatzy-bench $(BUILD)/maxiyatzy-solve $(BUILD)/maxiyatzy-inspect $(BUILD)/maxiyatzy-verify $(BUILD)/maxiyatzy-advise

$(BUILD):
	mkdir -p $@

$(BUILD)/maxiyatzy-info: $(COMMON) src/main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/maxiyatzy-tests: $(COMMON) tests/test_main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/maxiyatzy-bench: src/dice.cpp src/scoring.cpp bench/solver_kernel.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/maxiyatzy-solve: $(SOLVER) src/solve_main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/maxiyatzy-inspect: src/inspect_main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

# Independent table verifier; deliberately excludes solver.cpp/upper_states.cpp.
$(BUILD)/maxiyatzy-verify: src/dice.cpp src/scoring.cpp verify/verify_main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/maxiyatzy-advise: $(COMMON) src/policy.cpp src/advise_main.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/maxiyatzy-tests
	./$(BUILD)/maxiyatzy-tests

bench: $(BUILD)/maxiyatzy-bench
	./$(BUILD)/maxiyatzy-bench 100000

clean:
	rm -rf $(BUILD)
