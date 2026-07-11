#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/scoring.hpp"
#include "maxiyatzy/upper_states.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

using maxiyatzy::Category;
using maxiyatzy::Dice;

namespace {

Dice dice(std::initializer_list<int> faces) {
    Dice result;
    for (int face : faces) ++result.count.at(static_cast<std::size_t>(face - 1));
    return result;
}

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    expect(maxiyatzy::enumerate_dice(6).size() == 462, "462 unordered full rolls");
    expect(maxiyatzy::enumerate_dice_up_to(6).size() == 924, "924 partial rolls");

    const auto straight = dice({1,2,3,4,5,6});
    expect(maxiyatzy::score(Category::SmallStraight, straight) == 15, "small straight");
    expect(maxiyatzy::score(Category::LargeStraight, straight) == 20, "large straight");
    expect(maxiyatzy::score(Category::FullStraight, straight) == 21, "full straight");
    expect(maxiyatzy::score(Category::ThreePairs, dice({2,2,4,4,6,6})) == 24, "three pairs");
    expect(maxiyatzy::score(Category::FullHouse, dice({2,2,6,6,6,1})) == 22, "full house ignores extra die");
    expect(maxiyatzy::score(Category::SuperHouse, dice({3,3,3,5,5,5})) == 24, "super house");
    expect(maxiyatzy::score(Category::Tower, dice({1,1,4,4,4,4})) == 18, "tower");
    expect(maxiyatzy::score(Category::MaxiYatzy, dice({6,6,6,6,6,6})) == 100, "maxi yatzy");

    const auto analysis = maxiyatzy::analyze_upper_states();
    expect(analysis.reachable_mask_total_pairs == 3346, "reachable upper states");
    expect(analysis.effective_mask_total_states == 2250, "coalesced upper states");
    expect(analysis.effective_boundary_states == 36864000, "effective boundary states");

    const maxiyatzy::UpperStateModel upper;
    expect(upper.canonicalize(56, 38) == maxiyatzy::bonus_impossible_state,
           "subtotal collapses once bonus is impossible");
    expect(upper.canonicalize(56, 39) == 39,
           "subtotal remains exact while bonus is attainable");
    const auto earned = upper.score_upper(31, 40, 6, 6);
    expect(earned.state == maxiyatzy::bonus_achieved_state && earned.bonus == 50,
           "bonus is awarded on threshold crossing");
    const auto impossible = upper.score_upper(56, maxiyatzy::bonus_impossible_state, 1, 6);
    expect(impossible.state == maxiyatzy::bonus_impossible_state && impossible.bonus == 0,
           "impossible bonus remains impossible");
    std::cout << "all tests passed\n";
}
