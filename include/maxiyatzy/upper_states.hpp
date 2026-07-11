#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace maxiyatzy {

inline constexpr int upper_bonus_threshold = 75;
inline constexpr int upper_category_count = 6;
inline constexpr int lower_category_count = 14;
inline constexpr std::uint8_t bonus_achieved_state = 75;
inline constexpr std::uint8_t bonus_impossible_state = 76;
inline constexpr int upper_state_slots = 77;

struct UpperTransition {
    std::uint8_t state{};
    int bonus{};
};

class UpperStateModel {
public:
    UpperStateModel();

    [[nodiscard]] const std::vector<std::uint8_t>& states(int upper_mask) const;
    [[nodiscard]] std::uint8_t canonicalize(int upper_mask, int total) const;
    [[nodiscard]] UpperTransition score_upper(
        int upper_mask, std::uint8_t state, int face, int face_count) const;

private:
    std::array<std::vector<std::uint8_t>, 64> states_;
};

struct UpperStateAnalysis {
    std::size_t reachable_mask_total_pairs{};
    std::size_t effective_mask_total_states{};
    std::size_t reachable_boundary_states{};
    std::size_t effective_boundary_states{};
    std::array<std::size_t, 21> states_by_filled_count{};
    std::size_t peak_adjacent_layer_states{};
};

// Exact subtotals remain separate only while the 75-point bonus is still
// attainable. All bonus-achieved totals share one state, and all subtotals
// from which the bonus is impossible share another state for that upper mask.
[[nodiscard]] UpperStateAnalysis analyze_upper_states();

} // namespace maxiyatzy
