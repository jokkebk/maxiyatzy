// Independent verifier for the solved Maxi Yatzy lookup table.
//
// This program is NOT part of the solver. It was written separately to check
// the solver's output and deliberately does not link src/solver.cpp or
// src/upper_states.cpp. It reimplements from the rules: the .mytz decoder,
// the upper-section bonus accounting (exact subtotals, +50 exactly when the
// running total crosses 75), the canonical-state mapping, the keeper
// expectation recursion, and full game playouts. Only the dice multiset
// enumeration (src/dice.cpp) and the category scoring functions
// (src/scoring.cpp) are shared with the solver; both are covered by the unit
// tests and are straightforward to audit by hand.
//
// Two modes:
//
//   maxiyatzy-verify table.mytz bellman [samples]
//     Samples random (scorecard mask, upper state) pairs and recomputes each
//     stored value with one independent backward-induction step from the
//     stored next-layer values. Residuals should stay at float32 rounding
//     level (~2e-5); any modelling bug shows up as a large residual.
//
//   maxiyatzy-verify table.mytz mc [games] [threads]
//     Plays complete games using the table as policy while keeping score with
//     this file's own bookkeeping. The empirical mean must match the stored
//     initial expected value within Monte Carlo error. This exercises the
//     state encoding, the bonus coalescing and the keeper decisions
//     end-to-end against actual play.
//
// Results are recorded in docs/verification-v1.md.
#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/scoring.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using maxiyatzy::Category;
using maxiyatzy::Dice;

namespace {

constexpr int kCategories = 20;
constexpr std::uint32_t kFullMask = (1u << kCategories) - 1;
constexpr int kBonusThreshold = 75;
constexpr int kBonus = 50;

std::vector<Dice> g_dice;                       // all multisets size 0..6
std::vector<int> g_id_of;                       // 7^6 encode -> id
std::array<std::vector<int>, 7> g_by_size;
std::vector<std::array<int, 6>> g_add_face;     // id(size<6) + face -> id
std::vector<std::vector<int>> g_keepers;        // full id -> contained ids
std::vector<double> g_multiplicity;             // full id -> permutations
std::array<std::vector<int>, kCategories> g_scores; // cat -> full id -> score

std::array<std::vector<std::uint8_t>, 64> g_states;   // upper mask -> states
std::array<std::array<int, 256>, 64> g_slot;          // upper mask, state -> slot
std::array<int, 64> g_futmax;                         // upper mask -> 6*sum(open faces)
std::vector<std::uint64_t> g_offset;                  // mask -> value index
std::vector<float> g_values;

void build_dice_tables() {
    g_dice = maxiyatzy::enumerate_dice_up_to(6);
    g_id_of.assign(117649, -1);
    for (int id = 0; id < (int)g_dice.size(); ++id) {
        g_id_of[g_dice[id].encode()] = id;
        g_by_size[g_dice[id].size()].push_back(id);
    }
    g_add_face.resize(g_dice.size());
    for (int id = 0; id < (int)g_dice.size(); ++id) {
        g_add_face[id].fill(-1);
        if (g_dice[id].size() < 6) {
            for (int f = 0; f < 6; ++f) {
                Dice next = g_dice[id];
                ++next.count[f];
                g_add_face[id][f] = g_id_of[next.encode()];
            }
        }
    }
    g_keepers.resize(g_dice.size());
    g_multiplicity.assign(g_dice.size(), 0.0);
    auto fact = [](int n) { int r = 1; for (int i = 2; i <= n; ++i) r *= i; return r; };
    for (int full : g_by_size[6]) {
        int denom = 1;
        for (auto c : g_dice[full].count) denom *= fact(c);
        g_multiplicity[full] = fact(6) / denom;
        for (int k = 0; k < (int)g_dice.size(); ++k)
            if (g_dice[full].contains(g_dice[k])) g_keepers[full].push_back(k);
    }
    for (int c = 0; c < kCategories; ++c) {
        g_scores[c].assign(g_dice.size(), 0);
        for (int full : g_by_size[6])
            g_scores[c][full] = maxiyatzy::score((Category)c, g_dice[full]);
    }
}

bool load_table(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    char magic[8];
    in.read(magic, 8);
    if (std::memcmp(magic, "MYTZDP1\0", 8) != 0) { std::fprintf(stderr, "bad magic\n"); return false; }
    std::uint32_t version, flags, mask_count, slots, encoding;
    std::uint64_t value_count, checksum;
    in.read((char*)&version, 4); in.read((char*)&flags, 4);
    in.read((char*)&mask_count, 4); in.read((char*)&slots, 4);
    in.read((char*)&encoding, 4); in.read((char*)&value_count, 8);
    in.read((char*)&checksum, 8);
    if (version != 1 || mask_count != (1u << 20) || slots != 77 || encoding != 2) {
        std::fprintf(stderr, "unexpected header\n");
        return false;
    }
    (void)flags;
    for (int m = 0; m < 64; ++m) {
        unsigned char count;
        in.read((char*)&count, 1);
        std::array<unsigned char, 77> raw;
        in.read((char*)raw.data(), 77);
        g_slot[m].fill(-1);
        for (int i = 0; i < count; ++i) {
            g_states[m].push_back(raw[i]);
            g_slot[m][raw[i]] = i;
        }
        g_futmax[m] = 0;
        for (int f = 1; f <= 6; ++f)
            if (!(m & (1 << (f - 1)))) g_futmax[m] += 6 * f;
    }
    g_offset.assign(mask_count + 1, 0);
    for (std::uint32_t mask = 0; mask < mask_count; ++mask)
        g_offset[mask + 1] = g_offset[mask] + g_states[mask & 63].size();
    if (g_offset[mask_count] != value_count) {
        std::fprintf(stderr, "value count mismatch: %llu vs %llu\n",
                     (unsigned long long)g_offset[mask_count], (unsigned long long)value_count);
        return false;
    }
    g_values.resize(value_count);
    std::vector<std::uint32_t> raw(value_count);
    in.read((char*)raw.data(), value_count * 4);
    if (!in) { std::fprintf(stderr, "short read\n"); return false; }
    std::uint64_t sum = 14695981039346656037ULL;
    for (std::uint64_t i = 0; i < value_count; ++i) {
        const auto* b = (const unsigned char*)&raw[i];
        for (int j = 0; j < 4; ++j) { sum ^= b[j]; sum *= 1099511628211ULL; }
    }
    if (sum != checksum) { std::fprintf(stderr, "checksum mismatch\n"); return false; }
    std::uint64_t idx = 0;
    for (std::uint32_t mask = 0; mask < mask_count; ++mask) {
        std::uint32_t prev = 0;
        for (std::size_t s = 0; s < g_states[mask & 63].size(); ++s, ++idx) {
            std::uint32_t bits = raw[idx] ^ prev;
            prev = bits;
            float v;
            std::memcpy(&v, &bits, 4);
            g_values[idx] = v;
        }
    }
    return true;
}

// canonical state from an exact upper total, independent of the solver code
int canonical_state(std::uint32_t mask, int upper_total) {
    if (upper_total >= kBonusThreshold) return 75;
    if (upper_total + g_futmax[mask & 63] < kBonusThreshold) return 76;
    return upper_total;
}

double table_value(std::uint32_t mask, int state) {
    if (mask == kFullMask) return 0.0;
    int slot = g_slot[mask & 63][state];
    if (slot < 0) { std::fprintf(stderr, "missing state %d for mask %u\n", state, mask); std::abort(); }
    return g_values[g_offset[mask] + slot];
}

double value_from_total(std::uint32_t mask, int upper_total) {
    if (mask == kFullMask) return 0.0;
    return table_value(mask, canonical_state(mask, upper_total));
}

struct TurnEval {
    std::array<std::array<double, 7>, kCategories> cont;
    std::array<bool, kCategories> open;
    std::vector<double> f3;  // value of each final roll, by dice id
    std::vector<double> k2;  // keeper expectation toward f3
    std::vector<double> k1;  // keeper expectation toward f2
};

// expected value of completing each partial keeper to six dice under `finals`
void keeper_expectation(const std::vector<double>& finals, std::vector<double>& k) {
    for (int full : g_by_size[6]) k[full] = finals[full];
    for (int size = 5; size >= 0; --size)
        for (int id : g_by_size[size]) {
            double t = 0.0;
            for (int f = 0; f < 6; ++f) t += k[g_add_face[id][f]];
            k[id] = t / 6.0;
        }
}

// evaluate one turn's decision tables from an exact upper total, with bonus
// accounting done here from scratch
void eval_turn(std::uint32_t mask, int upper_total, TurnEval& ev) {
    for (int c = 0; c < kCategories; ++c) {
        ev.open[c] = !(mask & (1u << c));
        if (!ev.open[c]) continue;
        const std::uint32_t next_mask = mask | (1u << c);
        if (c < 6) {
            for (int n = 0; n <= 6; ++n) {
                const int nt = upper_total + (c + 1) * n;
                const double bonus =
                    (upper_total < kBonusThreshold && nt >= kBonusThreshold) ? kBonus : 0.0;
                ev.cont[c][n] = bonus + value_from_total(next_mask, nt);
            }
        } else {
            const double v = value_from_total(next_mask, upper_total);
            ev.cont[c].fill(v);
        }
    }
    ev.f3.assign(g_dice.size(), 0.0);
    for (int full : g_by_size[6]) {
        double best = -1e300;
        for (int c = 0; c < kCategories; ++c) {
            if (!ev.open[c]) continue;
            const int n = c < 6 ? g_dice[full].count[c] : 0;
            const double v = g_scores[c][full] + ev.cont[c][n];
            if (v > best) best = v;
        }
        ev.f3[full] = best;
    }
    ev.k2.assign(g_dice.size(), 0.0);
    keeper_expectation(ev.f3, ev.k2);
    std::vector<double> f2(g_dice.size(), 0.0);
    for (int full : g_by_size[6]) {
        double best = -1e300;
        for (int held : g_keepers[full]) best = std::max(best, ev.k2[held]);
        f2[full] = best;
    }
    ev.k1.assign(g_dice.size(), 0.0);
    keeper_expectation(f2, ev.k1);
}

// recompute the pre-roll value of (mask, state) from stored next-layer values
double bellman_value(std::uint32_t mask, int state) {
    TurnEval ev;
    for (int c = 0; c < kCategories; ++c) {
        ev.open[c] = !(mask & (1u << c));
        if (!ev.open[c]) continue;
        const std::uint32_t next_mask = mask | (1u << c);
        if (c < 6) {
            for (int n = 0; n <= 6; ++n) {
                double bonus = 0.0;
                int next_state;
                if (state == 75) next_state = 75;
                else if (state == 76) next_state = 76;
                else {
                    const int nt = state + (c + 1) * n;
                    if (nt >= kBonusThreshold) { next_state = 75; bonus = kBonus; }
                    else next_state = canonical_state(next_mask, nt);
                }
                ev.cont[c][n] = bonus + table_value(next_mask, next_state);
            }
        } else {
            const double v = table_value(next_mask, state);
            ev.cont[c].fill(v);
        }
    }
    ev.f3.assign(g_dice.size(), 0.0);
    for (int full : g_by_size[6]) {
        double best = -1e300;
        for (int c = 0; c < kCategories; ++c) {
            if (!ev.open[c]) continue;
            const int n = c < 6 ? g_dice[full].count[c] : 0;
            best = std::max(best, g_scores[c][full] + ev.cont[c][n]);
        }
        ev.f3[full] = best;
    }
    std::vector<double> k(g_dice.size()), f(g_dice.size());
    keeper_expectation(ev.f3, k);
    for (int full : g_by_size[6]) {
        double best = -1e300;
        for (int held : g_keepers[full]) best = std::max(best, k[held]);
        f[full] = best;
    }
    keeper_expectation(f, k);
    for (int full : g_by_size[6]) {
        double best = -1e300;
        for (int held : g_keepers[full]) best = std::max(best, k[held]);
        f[full] = best;
    }
    double expected = 0.0;
    for (int full : g_by_size[6]) expected += g_multiplicity[full] * f[full];
    return expected / 46656.0;
}

int roll_dice(int n, std::mt19937_64& rng, const Dice& base) {
    Dice d = base;
    std::uniform_int_distribution<int> die(0, 5);
    for (int i = 0; i < n; ++i) ++d.count[die(rng)];
    return g_id_of[d.encode()];
}

struct GameResult { double score; double upper; bool bonus; };

GameResult play_game(std::mt19937_64& rng) {
    std::uint32_t mask = 0;
    int upper_total = 0;
    int score_total = 0;
    bool bonus_awarded = false;
    TurnEval ev;
    static const Dice empty{};
    for (int turn = 0; turn < kCategories; ++turn) {
        eval_turn(mask, upper_total, ev);
        int roll = roll_dice(6, rng, empty);
        int best_keep = -1; double best_v = -1e300;
        for (int held : g_keepers[roll])
            if (ev.k1[held] > best_v) { best_v = ev.k1[held]; best_keep = held; }
        roll = roll_dice(6 - g_dice[best_keep].size(), rng, g_dice[best_keep]);
        best_keep = -1; best_v = -1e300;
        for (int held : g_keepers[roll])
            if (ev.k2[held] > best_v) { best_v = ev.k2[held]; best_keep = held; }
        roll = roll_dice(6 - g_dice[best_keep].size(), rng, g_dice[best_keep]);
        int best_cat = -1; best_v = -1e300;
        for (int c = 0; c < kCategories; ++c) {
            if (!ev.open[c]) continue;
            const int n = c < 6 ? g_dice[roll].count[c] : 0;
            const double v = g_scores[c][roll] + ev.cont[c][n];
            if (v > best_v) { best_v = v; best_cat = c; }
        }
        score_total += g_scores[best_cat][roll];
        if (best_cat < 6) {
            upper_total += (best_cat + 1) * g_dice[roll].count[best_cat];
            if (!bonus_awarded && upper_total >= kBonusThreshold) {
                bonus_awarded = true;
                score_total += kBonus;
            }
        }
        mask |= 1u << best_cat;
    }
    return {double(score_total), double(upper_total), bonus_awarded};
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s table.mytz mc [games] [threads]\n"
                     "       %s table.mytz bellman [samples]\n",
                     argv[0], argv[0]);
        return 1;
    }
    build_dice_tables();
    if (!load_table(argv[1])) return 1;
    std::printf("table loaded, checksum ok. root value V(empty, 0) = %.6f\n",
                value_from_total(0, 0));

    const std::string mode = argv[2];
    if (mode == "bellman") {
        const long samples = argc > 3 ? std::atol(argv[3]) : 20000;
        std::mt19937_64 rng(12345);
        std::uniform_int_distribution<std::uint32_t> mask_dist(0, kFullMask - 1);
        double max_err = 0.0, sum_err = 0.0;
        std::uint32_t worst_mask = 0; int worst_state = 0;
        for (long i = 0; i < samples; ++i) {
            std::uint32_t mask = mask_dist(rng);
            const auto& states = g_states[mask & 63];
            int state = states[std::uniform_int_distribution<int>(0, (int)states.size() - 1)(rng)];
            const double recomputed = bellman_value(mask, state);
            const double stored = table_value(mask, state);
            const double err = std::fabs(recomputed - stored);
            sum_err += err;
            if (err > max_err) { max_err = err; worst_mask = mask; worst_state = state; }
        }
        std::printf("bellman residual over %ld sampled states: mean=%.3e max=%.3e (mask=%u state=%d)\n",
                    samples, sum_err / samples, max_err, worst_mask, worst_state);
        std::printf("root recomputed via bellman: %.6f (stored %.6f)\n",
                    bellman_value(0, 0), table_value(0, 0));
        return 0;
    }

    if (mode == "mc") {
        const long games = argc > 3 ? std::atol(argv[3]) : 100000;
        const int threads = argc > 4 ? std::atoi(argv[4]) : 8;
        std::atomic<long> next{0};
        std::vector<double> sums(threads, 0.0), sqs(threads, 0.0), uppers(threads, 0.0);
        std::vector<long> bonuses(threads, 0), counts(threads, 0);
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                std::mt19937_64 rng(0xC0FFEE ^ (t * 0x9E3779B97F4A7C15ULL));
                while (true) {
                    long g = next.fetch_add(1);
                    if (g >= games) break;
                    const auto r = play_game(rng);
                    sums[t] += r.score; sqs[t] += r.score * r.score;
                    uppers[t] += r.upper; bonuses[t] += r.bonus ? 1 : 0;
                    ++counts[t];
                }
            });
        }
        for (auto& th : pool) th.join();
        double sum = 0, sq = 0, up = 0; long bn = 0, n = 0;
        for (int t = 0; t < threads; ++t) {
            sum += sums[t]; sq += sqs[t]; up += uppers[t]; bn += bonuses[t]; n += counts[t];
        }
        const double mean = sum / n;
        const double var = sq / n - mean * mean;
        const double se = std::sqrt(var / n);
        std::printf("games=%ld mean=%.4f sd=%.2f se=%.4f  bonus%%=%.2f mean_upper=%.2f\n",
                    n, mean, std::sqrt(var), se, 100.0 * bn / n, up / n);
        std::printf("stored root value: %.6f  (diff %.4f = %.2f se)\n",
                    table_value(0, 0), mean - table_value(0, 0),
                    (mean - table_value(0, 0)) / se);
        return 0;
    }
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 1;
}
