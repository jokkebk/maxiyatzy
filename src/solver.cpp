#include "maxiyatzy/solver.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace maxiyatzy {
namespace {

constexpr std::uint32_t all_categories_mask = (std::uint32_t{1} << 20) - 1;
constexpr std::size_t mask_count = std::size_t{1} << 20;
constexpr int roll_outcomes = 46656; // 6^6

int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) result *= i;
    return result;
}

void write_u32(std::ostream& out, std::uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_u64(std::ostream& out, std::uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::uint64_t fnv1a(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

Solver::Solver(SolverOptions options) : options_(options) {
    if (options_.threads < 1) options_.threads = 1;
    if (options_.chunk_size < 1) options_.chunk_size = 1;
    initialize_tables();
}

void Solver::initialize_tables() {
    dice_ = enumerate_dice_up_to(6);
    constexpr int encoded_dice_count = 117649; // 7^6
    std::vector<int> id_of(encoded_dice_count, -1);
    for (int id = 0; id < static_cast<int>(dice_.size()); ++id) {
        id_of[dice_[id].encode()] = id;
        by_size_[dice_[id].size()].push_back(id);
    }

    add_face_.resize(dice_.size());
    keepers_.resize(dice_.size());
    multiplicity_.resize(dice_.size());
    for (int id = 0; id < static_cast<int>(dice_.size()); ++id) {
        add_face_[id].fill(-1);
        if (dice_[id].size() < 6) {
            for (int face = 0; face < 6; ++face) {
                auto next = dice_[id];
                ++next.count[face];
                add_face_[id][face] = id_of[next.encode()];
            }
        }
    }
    for (int full : by_size_[6]) {
        int denominator = 1;
        for (auto count : dice_[full].count) denominator *= factorial(count);
        multiplicity_[full] = factorial(6) / denominator;
        for (int keeper = 0; keeper < static_cast<int>(dice_.size()); ++keeper)
            if (dice_[full].contains(dice_[keeper])) keepers_[full].push_back(keeper);
    }

    for (std::size_t category = 0; category < category_count; ++category) {
        scores_[category].assign(dice_.size(), 0.0);
        for (int full : by_size_[6])
            scores_[category][full] = score(static_cast<Category>(category), dice_[full]);
    }
    for (std::uint32_t mask = 0; mask <= all_categories_mask; ++mask)
        masks_by_filled_[std::popcount(mask)].push_back(mask);
}

std::size_t Solver::value_index(std::uint32_t mask, std::uint8_t upper_state) const {
    return static_cast<std::size_t>(mask) * upper_state_slots + upper_state;
}

void Solver::reroll(const std::vector<double>& input, std::vector<double>& output,
                    std::vector<double>& keeper) const {
    for (int full : by_size_[6]) keeper[full] = input[full];
    for (int size = 5; size >= 0; --size) {
        for (int partial : by_size_[size]) {
            double total = 0.0;
            for (int face = 0; face < 6; ++face)
                total += keeper[add_face_[partial][face]];
            keeper[partial] = total / 6.0;
        }
    }
    for (int full : by_size_[6]) {
        double best = -std::numeric_limits<double>::infinity();
        for (int held : keepers_[full]) best = std::max(best, keeper[held]);
        output[full] = best;
    }
}

void Solver::solve_state(std::uint32_t mask, std::uint8_t upper_state, Scratch& scratch) {
    std::array<std::array<double, 7>, category_count> continuation{};
    std::array<bool, category_count> open{};
    for (std::size_t category = 0; category < category_count; ++category) {
        if (mask & (std::uint32_t{1} << category)) continue;
        open[category] = true;
        const auto next_mask = mask | (std::uint32_t{1} << category);
        if (category < 6) {
            for (int count = 0; count <= 6; ++count) {
                const auto transition = upper_.score_upper(
                    mask & 63, upper_state, static_cast<int>(category) + 1, count);
                continuation[category][count] = transition.bonus +
                    values_[value_index(next_mask, transition.state)];
            }
        } else {
            const double future = values_[value_index(next_mask, upper_state)];
            continuation[category].fill(future);
        }
    }

    for (int full : by_size_[6]) {
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t category = 0; category < category_count; ++category) {
            if (!open[category]) continue;
            const int count = category < 6 ? dice_[full].count[category] : 0;
            best = std::max(best, scores_[category][full] + continuation[category][count]);
        }
        scratch.first[full] = best;
    }
    reroll(scratch.first, scratch.second, scratch.keeper);
    reroll(scratch.second, scratch.first, scratch.keeper);

    double expected = 0.0;
    for (int full : by_size_[6]) expected += multiplicity_[full] * scratch.first[full];
    values_[value_index(mask, upper_state)] = expected / roll_outcomes;
}

void Solver::solve() {
    const auto started = std::chrono::steady_clock::now();
    values_.assign(mask_count * upper_state_slots,
                   std::numeric_limits<double>::quiet_NaN());
    for (auto state : upper_.states(63))
        values_[value_index(all_categories_mask, state)] = 0.0;

    std::size_t completed = upper_.states(63).size();
    for (int filled = 19; filled >= 0; --filled) {
        std::vector<std::uint32_t> tasks;
        std::size_t layer_size = 0;
        for (auto mask : masks_by_filled_[filled]) layer_size += upper_.states(mask & 63).size();
        tasks.reserve(layer_size);
        for (auto mask : masks_by_filled_[filled])
            for (auto state : upper_.states(mask & 63))
                tasks.push_back((mask << 7) | state);

        std::atomic<std::size_t> cursor{0};
        const int worker_count = std::min<int>(options_.threads, static_cast<int>(tasks.size()));
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (int worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&, this] {
                Scratch scratch{
                    std::vector<double>(dice_.size()),
                    std::vector<double>(dice_.size()),
                    std::vector<double>(dice_.size())};
                while (true) {
                    const auto begin = cursor.fetch_add(options_.chunk_size, std::memory_order_relaxed);
                    if (begin >= tasks.size()) break;
                    const auto end = std::min(tasks.size(), begin + options_.chunk_size);
                    for (std::size_t index = begin; index < end; ++index) {
                        const auto task = tasks[index];
                        solve_state(task >> 7, static_cast<std::uint8_t>(task & 127), scratch);
                    }
                }
            });
        }
        for (auto& worker : workers) worker.join();
        completed += tasks.size();
        if (options_.progress) {
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            std::cerr << "filled=" << std::setw(2) << filled
                      << " layer_states=" << tasks.size()
                      << " completed=" << completed
                      << " elapsed=" << std::fixed << std::setprecision(1) << seconds << "s\n";
        }
    }
    solved_ = true;
}

double Solver::initial_expected_score() const {
    if (!solved_) throw std::logic_error("solver has not run");
    return values_[value_index(0, 0)];
}

void Solver::write_compact(const std::string& path) const {
    if (!solved_) throw std::logic_error("solver has not run");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open output file: " + path);

    const char magic[8] = {'M','Y','T','Z','D','P','1','\0'};
    out.write(magic, sizeof(magic));
    write_u32(out, 1); // format version
    write_u32(out, 1); // rules flags: voluntary crossing allowed
    write_u32(out, static_cast<std::uint32_t>(mask_count));
    write_u32(out, upper_state_slots);
    write_u32(out, 2); // value encoding: per-mask XOR-delta IEEE-754 float32
    write_u64(out, analyze_upper_states().effective_boundary_states);
    const auto checksum_position = out.tellp();
    write_u64(out, 0);
    for (int upper_mask = 0; upper_mask < 64; ++upper_mask) {
        const auto& states = upper_.states(upper_mask);
        out.put(static_cast<char>(states.size()));
        for (int index = 0; index < upper_state_slots; ++index)
            out.put(static_cast<char>(index < static_cast<int>(states.size()) ? states[index] : 255));
    }

    std::uint64_t checksum = 14695981039346656037ULL;
    for (std::uint32_t mask = 0; mask <= all_categories_mask; ++mask) {
        std::uint32_t previous_bits = 0;
        for (auto state : upper_.states(mask & 63)) {
            const float value = static_cast<float>(values_[value_index(mask, state)]);
            if (!std::isfinite(value)) throw std::runtime_error("non-finite value while writing");
            std::uint32_t value_bits;
            std::memcpy(&value_bits, &value, sizeof(value));
            const std::uint32_t encoded = value_bits ^ previous_bits;
            previous_bits = value_bits;
            out.write(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
            checksum = fnv1a(checksum, &encoded, sizeof(encoded));
        }
    }
    out.seekp(checksum_position);
    write_u64(out, checksum);
    if (!out) throw std::runtime_error("failed while writing output file: " + path);
}

} // namespace maxiyatzy
