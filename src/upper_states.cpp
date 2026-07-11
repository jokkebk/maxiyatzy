#include "maxiyatzy/upper_states.hpp"

#include <algorithm>
#include <bit>
#include <set>

namespace maxiyatzy {
namespace {

std::size_t choose(int n, int k) {
    if (k < 0 || k > n) return 0;
    k = std::min(k, n - k);
    std::size_t result = 1;
    for (int i = 1; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}

} // namespace

UpperStateModel::UpperStateModel() {
    std::array<std::set<int>, 64> reachable;
    reachable[0].insert(0);
    for (int face = 1; face <= 6; ++face) {
        const int bit = 1 << (face - 1);
        auto next = reachable;
        for (int mask = 0; mask < 64; ++mask) {
            if (mask & bit) continue;
            for (int total : reachable[mask])
                for (int count = 0; count <= 6; ++count)
                    next[mask | bit].insert(std::min(
                        upper_bonus_threshold, total + face * count));
        }
        reachable = std::move(next);
    }
    for (int mask = 0; mask < 64; ++mask) {
        std::set<std::uint8_t> canonical;
        for (int total : reachable[mask]) canonical.insert(canonicalize(mask, total));
        states_[mask].assign(canonical.begin(), canonical.end());
    }
}

const std::vector<std::uint8_t>& UpperStateModel::states(int upper_mask) const {
    return states_.at(static_cast<std::size_t>(upper_mask));
}

std::uint8_t UpperStateModel::canonicalize(int upper_mask, int total) const {
    if (total >= upper_bonus_threshold) return bonus_achieved_state;
    int future_maximum = 0;
    for (int face = 1; face <= 6; ++face)
        if ((upper_mask & (1 << (face - 1))) == 0) future_maximum += 6 * face;
    if (total + future_maximum < upper_bonus_threshold)
        return bonus_impossible_state;
    return static_cast<std::uint8_t>(total);
}

UpperTransition UpperStateModel::score_upper(
    int upper_mask, std::uint8_t state, int face, int face_count) const {
    const int next_mask = upper_mask | (1 << (face - 1));
    if (state == bonus_achieved_state) return {bonus_achieved_state, 0};
    if (state == bonus_impossible_state) return {bonus_impossible_state, 0};
    const int total = state + face * face_count;
    if (total >= upper_bonus_threshold) return {bonus_achieved_state, 50};
    return {canonicalize(next_mask, total), 0};
}

UpperStateAnalysis analyze_upper_states() {
    UpperStateModel model;

    std::array<std::set<int>, 64> reachable;
    reachable[0].insert(0);
    for (int face = 1; face <= 6; ++face) {
        const int bit = 1 << (face - 1);
        auto next = reachable;
        for (int mask = 0; mask < 64; ++mask) {
            if (mask & bit) continue;
            for (int total : reachable[mask])
                for (int count = 0; count <= 6; ++count)
                    next[mask | bit].insert(std::min(
                        upper_bonus_threshold, total + face * count));
        }
        reachable = std::move(next);
    }

    UpperStateAnalysis result;
    std::array<std::size_t, 64> effective_by_mask{};
    for (int mask = 0; mask < 64; ++mask) {
        result.reachable_mask_total_pairs += reachable[mask].size();
        effective_by_mask[mask] = model.states(mask).size();
        result.effective_mask_total_states += effective_by_mask[mask];
    }

    constexpr std::size_t lower_masks = std::size_t{1} << lower_category_count;
    result.reachable_boundary_states = result.reachable_mask_total_pairs * lower_masks;
    result.effective_boundary_states = result.effective_mask_total_states * lower_masks;

    for (int filled = 0; filled <= 20; ++filled) {
        for (int upper_mask = 0; upper_mask < 64; ++upper_mask) {
            const int upper_filled = std::popcount(static_cast<unsigned>(upper_mask));
            result.states_by_filled_count[filled] += effective_by_mask[upper_mask] *
                choose(lower_category_count, filled - upper_filled);
        }
    }
    for (int filled = 0; filled < 20; ++filled) {
        result.peak_adjacent_layer_states = std::max(
            result.peak_adjacent_layer_states,
            result.states_by_filled_count[filled] + result.states_by_filled_count[filled + 1]);
    }
    return result;
}

} // namespace maxiyatzy
