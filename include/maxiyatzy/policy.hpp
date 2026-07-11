#pragma once

#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/scoring.hpp"
#include "maxiyatzy/upper_states.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace maxiyatzy {

// Read-only access to a solved .mytz lookup table. The file is mmapped and
// value records are decoded on demand: one scorecard mask's values are a
// contiguous XOR-delta float32 run of at most 77 words, so lookups need no
// up-front decode of the 141 MiB payload.
class ValueTable {
public:
    explicit ValueTable(const std::string& path);
    ~ValueTable();
    ValueTable(const ValueTable&) = delete;
    ValueTable& operator=(const ValueTable&) = delete;

    // canonical upper state for an exact subtotal, as stored in the table
    [[nodiscard]] int canonical_state(std::uint32_t mask, int upper_total) const;
    [[nodiscard]] double value(std::uint32_t mask, int state) const;
    [[nodiscard]] double value_from_total(std::uint32_t mask, int upper_total) const;
    [[nodiscard]] double initial_expected_score() const { return value_from_total(0, 0); }

private:
    const unsigned char* map_ = nullptr;
    std::size_t map_size_ = 0;
    const unsigned char* payload_ = nullptr;
    std::array<std::vector<std::uint8_t>, 64> states_;
    std::array<std::array<int, 128>, 64> slot_;
    std::array<int, 64> future_max_{};
    std::vector<std::uint64_t> offsets_;
};

struct KeepOption {
    Dice keep;
    double ev;
};

struct CategoryOption {
    Category category;
    int score;
    double ev;
};

// Optimal-play advice for one turn. begin_turn() evaluates the whole turn
// (category continuations and both keeper-expectation levels); the option
// queries then rank the concrete choices for an actual roll.
class Advisor {
public:
    explicit Advisor(const ValueTable& table);

    void begin_turn(std::uint32_t mask, int upper_total);
    [[nodiscard]] double preroll_value() const { return preroll_; }

    // ranked distinct keeps for a full roll with 1 or 2 rerolls left;
    // keeping all six dice means stopping early
    [[nodiscard]] std::vector<KeepOption> keep_options(
        const Dice& roll, int rerolls_left, std::size_t top_k) const;
    // ranked category choices for the final dice
    [[nodiscard]] std::vector<CategoryOption> category_options(
        const Dice& roll, std::size_t top_k) const;

    [[nodiscard]] int dice_id(const Dice& dice) const;

private:
    const ValueTable& table_;
    std::vector<Dice> dice_;
    std::vector<int> id_of_;
    std::array<std::vector<int>, 7> by_size_;
    std::vector<std::array<int, 6>> add_face_;
    std::vector<std::vector<int>> keepers_;
    std::vector<double> multiplicity_;
    std::array<std::vector<int>, category_count> scores_;

    std::uint32_t mask_ = 0;
    int upper_total_ = 0;
    std::array<std::array<double, 7>, category_count> cont_{};
    std::array<bool, category_count> open_{};
    std::vector<double> f3_, k2_, k1_;
    double preroll_ = 0.0;

    void keeper_expectation(const std::vector<double>& finals, std::vector<double>& k) const;
};

} // namespace maxiyatzy
