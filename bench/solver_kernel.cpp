#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/scoring.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 100000;
    const auto dice = maxiyatzy::enumerate_dice_up_to(6);
    constexpr int code_count = 117649; // 7^6
    std::vector<int> id_of(code_count, -1);
    std::array<std::vector<int>, 7> by_size;
    for (int id = 0; id < static_cast<int>(dice.size()); ++id) {
        id_of[dice[id].encode()] = id;
        by_size[dice[id].size()].push_back(id);
    }

    std::vector<std::array<int, 6>> add_face(dice.size());
    for (int id = 0; id < static_cast<int>(dice.size()); ++id) {
        add_face[id].fill(-1);
        if (dice[id].size() == 6) continue;
        for (int face = 0; face < 6; ++face) {
            auto next = dice[id];
            ++next.count[face];
            add_face[id][face] = id_of[next.encode()];
        }
    }

    std::vector<std::vector<int>> keepers(dice.size());
    for (int full : by_size[6])
        for (int keeper = 0; keeper < static_cast<int>(dice.size()); ++keeper)
            if (dice[full].contains(dice[keeper])) keepers[full].push_back(keeper);

    std::array<std::vector<double>, maxiyatzy::category_count> scores;
    for (std::size_t category = 0; category < maxiyatzy::category_count; ++category) {
        scores[category].resize(dice.size());
        for (int full : by_size[6])
            scores[category][full] = maxiyatzy::score(
                static_cast<maxiyatzy::Category>(category), dice[full]);
    }

    std::vector<double> work(dice.size()), roll_value(dice.size());
    double checksum = 0.0;
    const auto started = Clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        double successor[maxiyatzy::category_count][7];
        for (std::size_t category = 0; category < maxiyatzy::category_count; ++category)
            for (int count = 0; count <= 6; ++count)
                successor[category][count] =
                    ((iteration * 13 + static_cast<int>(category) * 37 + count * 11) % 997) / 997.0;

        // Ten open categories is representative of the large middle layers.
        for (int full : by_size[6]) {
            double best = -1e100;
            for (std::size_t category = 0; category < 10; ++category) {
                const int count = dice[full].count[category % 6];
                best = std::max(best, scores[category][full] + successor[category][count]);
            }
            roll_value[full] = best;
        }

        for (int rerolls = 0; rerolls < 2; ++rerolls) {
            for (int full : by_size[6]) work[full] = roll_value[full];
            for (int size = 5; size >= 0; --size) {
                for (int partial : by_size[size]) {
                    double total = 0.0;
                    for (int face = 0; face < 6; ++face)
                        total += work[add_face[partial][face]];
                    work[partial] = total / 6.0;
                }
            }
            for (int full : by_size[6]) {
                double best = -1e100;
                for (int keeper : keepers[full]) best = std::max(best, work[keeper]);
                roll_value[full] = best;
            }
        }
        checksum += roll_value[by_size[6][iteration % by_size[6].size()]];
    }

    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    std::cout << "iterations=" << iterations
              << " seconds=" << seconds
              << " states_per_second=" << iterations / seconds
              << " checksum=" << checksum << '\n';
}

