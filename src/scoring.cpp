#include "maxiyatzy/scoring.hpp"

#include <algorithm>
#include <vector>

namespace maxiyatzy {
namespace {

int best_n_of_kind(const Dice& dice, int needed) {
    for (int face = 5; face >= 0; --face)
        if (dice.count[face] >= needed) return needed * (face + 1);
    return 0;
}

int best_pairs(const Dice& dice, int needed) {
    int result = 0;
    int found = 0;
    for (int face = 5; face >= 0 && found < needed; --face) {
        if (dice.count[face] >= 2) {
            result += 2 * (face + 1);
            ++found;
        }
    }
    return found == needed ? result : 0;
}

int full_house(const Dice& dice) {
    int best = 0;
    for (int triple = 0; triple < 6; ++triple) {
        if (dice.count[triple] < 3) continue;
        for (int pair = 0; pair < 6; ++pair) {
            if (pair != triple && dice.count[pair] >= 2)
                best = std::max(best, 3 * (triple + 1) + 2 * (pair + 1));
        }
    }
    return best;
}

int tower(const Dice& dice) {
    int best = 0;
    for (int four = 0; four < 6; ++four) {
        if (dice.count[four] < 4) continue;
        for (int pair = 0; pair < 6; ++pair) {
            if (pair != four && dice.count[pair] >= 2)
                best = std::max(best, 4 * (four + 1) + 2 * (pair + 1));
        }
    }
    return best;
}

} // namespace

bool is_upper(Category category) {
    return static_cast<std::size_t>(category) <= static_cast<std::size_t>(Category::Sixes);
}

int score(Category category, const Dice& dice) {
    const auto index = static_cast<std::size_t>(category);
    if (is_upper(category)) return static_cast<int>(index + 1) * dice.count[index];

    switch (category) {
    case Category::OnePair: return best_pairs(dice, 1);
    case Category::TwoPairs: return best_pairs(dice, 2);
    case Category::ThreePairs: return best_pairs(dice, 3);
    case Category::ThreeOfAKind: return best_n_of_kind(dice, 3);
    case Category::FourOfAKind: return best_n_of_kind(dice, 4);
    case Category::FiveOfAKind: return best_n_of_kind(dice, 5);
    case Category::SmallStraight:
        return std::all_of(dice.count.begin(), dice.count.begin() + 5,
                           [](auto n) { return n >= 1; }) ? 15 : 0;
    case Category::LargeStraight:
        return std::all_of(dice.count.begin() + 1, dice.count.end(),
                           [](auto n) { return n >= 1; }) ? 20 : 0;
    case Category::FullStraight:
        return std::all_of(dice.count.begin(), dice.count.end(),
                           [](auto n) { return n == 1; }) ? 21 : 0;
    case Category::FullHouse: return full_house(dice);
    case Category::SuperHouse:
        for (int first = 0; first < 6; ++first)
            for (int second = first + 1; second < 6; ++second)
                if (dice.count[first] == 3 && dice.count[second] == 3)
                    return 3 * (first + second + 2);
        return 0;
    case Category::Tower: return tower(dice);
    case Category::Chance: return dice.sum();
    case Category::MaxiYatzy:
        return std::any_of(dice.count.begin(), dice.count.end(),
                           [](auto n) { return n == 6; }) ? 100 : 0;
    default: return 0;
    }
}

} // namespace maxiyatzy

