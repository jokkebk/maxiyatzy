#pragma once

#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/scoring.hpp"
#include "maxiyatzy/upper_states.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace maxiyatzy {

struct SolverOptions {
    int threads = 5;
    std::size_t chunk_size = 256;
    bool progress = true;
};

class Solver {
public:
    explicit Solver(SolverOptions options = {});

    void solve();
    void write_compact(const std::string& path) const;

    [[nodiscard]] double initial_expected_score() const;
    [[nodiscard]] const UpperStateModel& upper_states() const { return upper_; }

private:
    struct Scratch {
        std::vector<double> first;
        std::vector<double> second;
        std::vector<double> keeper;
    };

    void initialize_tables();
    void solve_state(std::uint32_t mask, std::uint8_t upper_state, Scratch& scratch);
    void reroll(const std::vector<double>& input, std::vector<double>& output,
                std::vector<double>& keeper) const;
    [[nodiscard]] std::size_t value_index(std::uint32_t mask, std::uint8_t upper_state) const;

    SolverOptions options_;
    UpperStateModel upper_;
    std::vector<Dice> dice_;
    std::array<std::vector<int>, 7> by_size_;
    std::vector<std::array<int, 6>> add_face_;
    std::vector<std::vector<int>> keepers_;
    std::array<std::vector<double>, category_count> scores_;
    std::vector<int> multiplicity_;
    std::array<std::vector<std::uint32_t>, 21> masks_by_filled_;
    std::vector<double> values_;
    bool solved_ = false;
};

} // namespace maxiyatzy

