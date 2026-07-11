#include "maxiyatzy/dice.hpp"

#include <stdexcept>

namespace maxiyatzy {
namespace {

void generate(int face, int left, Dice& dice, std::vector<Dice>& result) {
    if (face == 5) {
        dice.count[face] = static_cast<std::uint8_t>(left);
        result.push_back(dice);
        return;
    }
    for (int n = 0; n <= left; ++n) {
        dice.count[face] = static_cast<std::uint8_t>(n);
        generate(face + 1, left - n, dice, result);
    }
}

} // namespace

int Dice::size() const {
    int result = 0;
    for (auto n : count) result += n;
    return result;
}

int Dice::sum() const {
    int result = 0;
    for (int face = 0; face < 6; ++face) result += (face + 1) * count[face];
    return result;
}

int Dice::encode() const {
    int result = 0;
    int multiplier = 1;
    for (auto n : count) {
        result += n * multiplier;
        multiplier *= 7;
    }
    return result;
}

bool Dice::contains(const Dice& keeper) const {
    for (int face = 0; face < 6; ++face)
        if (keeper.count[face] > count[face]) return false;
    return true;
}

std::vector<Dice> enumerate_dice(int size) {
    if (size < 0 || size > 6) throw std::invalid_argument("dice size must be 0..6");
    std::vector<Dice> result;
    Dice dice;
    generate(0, size, dice, result);
    return result;
}

std::vector<Dice> enumerate_dice_up_to(int maximum_size) {
    if (maximum_size < 0 || maximum_size > 6)
        throw std::invalid_argument("maximum dice size must be 0..6");
    std::vector<Dice> result;
    for (int size = 0; size <= maximum_size; ++size) {
        auto exact = enumerate_dice(size);
        result.insert(result.end(), exact.begin(), exact.end());
    }
    return result;
}

} // namespace maxiyatzy

