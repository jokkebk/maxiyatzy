#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace maxiyatzy {

struct Dice {
    std::array<std::uint8_t, 6> count{};

    [[nodiscard]] int size() const;
    [[nodiscard]] int sum() const;
    [[nodiscard]] int encode() const;
    [[nodiscard]] bool contains(const Dice& keeper) const;
};

[[nodiscard]] std::vector<Dice> enumerate_dice(int size);
[[nodiscard]] std::vector<Dice> enumerate_dice_up_to(int maximum_size);

} // namespace maxiyatzy

