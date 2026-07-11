#pragma once

#include "maxiyatzy/dice.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace maxiyatzy {

enum class Category : std::size_t {
    Ones, Twos, Threes, Fours, Fives, Sixes,
    OnePair, TwoPairs, ThreePairs,
    ThreeOfAKind, FourOfAKind, FiveOfAKind,
    SmallStraight, LargeStraight, FullStraight,
    FullHouse, SuperHouse, Tower, Chance, MaxiYatzy,
    Count
};

inline constexpr std::size_t category_count =
    static_cast<std::size_t>(Category::Count);

inline constexpr std::array<std::string_view, category_count> category_names{
    "ones", "twos", "threes", "fours", "fives", "sixes",
    "one pair", "two pairs", "three pairs", "three of a kind",
    "four of a kind", "five of a kind", "small straight",
    "large straight", "full straight", "full house", "super house",
    "tower", "chance", "maxi yatzy"
};

[[nodiscard]] int score(Category category, const Dice& dice);
[[nodiscard]] bool is_upper(Category category);

} // namespace maxiyatzy

