// Interactive optimal-play advisor. Walks one scorecard through a game:
// the user types actual dice, the advisor recommends keeps and the category
// to fill, and reports the expected-value cost of any deviation.
#include "maxiyatzy/policy.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using maxiyatzy::Advisor;
using maxiyatzy::Category;
using maxiyatzy::Dice;
using maxiyatzy::ValueTable;
using maxiyatzy::category_count;
using maxiyatzy::category_names;

namespace {

constexpr std::uint32_t kFullMask = (1u << category_count) - 1;

struct GameState {
    std::uint32_t mask = 0;
    int upper_total = 0;
    int score = 0;                 // includes bonus once awarded
    bool bonus = false;
    std::array<int, category_count> card{};
};

std::string dice_text(const Dice& dice) {
    std::string out;
    for (int face = 0; face < 6; ++face)
        for (int i = 0; i < dice.count[face]; ++i) {
            if (!out.empty()) out += ' ';
            out += static_cast<char>('1' + face);
        }
    return out.empty() ? "nothing" : out;
}

std::optional<Dice> parse_dice(const std::string& text, int expected) {
    Dice dice;
    int n = 0;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c < '1' || c > '6') return std::nullopt;
        ++dice.count[c - '1'];
        ++n;
    }
    if (expected >= 0 && n != expected) return std::nullopt;
    return dice;
}

std::optional<Category> parse_category(std::string name, std::uint32_t mask) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::optional<Category> match;
    for (std::size_t c = 0; c < category_count; ++c) {
        if (mask & (1u << c)) continue;
        const std::string full{category_names[c]};
        if (full == name) return static_cast<Category>(c);
        if (full.rfind(name, 0) == 0) {
            if (match) return std::nullopt;  // ambiguous prefix
            match = static_cast<Category>(c);
        }
    }
    return match;
}

void print_card(const GameState& game) {
    std::printf("+------------------------+\n");
    for (std::size_t c = 0; c < category_count; ++c) {
        if (game.mask & (1u << c))
            std::printf("| %-16s %5d |\n", std::string(category_names[c]).c_str(),
                        game.card[c]);
        else
            std::printf("| %-16s %5s |\n", std::string(category_names[c]).c_str(), "-");
        if (c == 5) {
            std::printf("| %-16s %5d |\n", "upper total", game.upper_total);
            std::printf("| %-16s %5s |\n", "bonus", game.bonus ? "50" : "-");
        }
    }
    std::printf("| %-16s %5d |\n", "TOTAL", game.score);
    std::printf("+------------------------+\n");
}

// reads one line; returns false on EOF
bool prompt(const char* label, std::string& line) {
    std::printf("%s> ", label);
    std::fflush(stdout);
    return static_cast<bool>(std::getline(std::cin, line));
}

void print_keeps(const std::vector<maxiyatzy::KeepOption>& options) {
    if (options.empty()) return;
    if (options[0].keep.size() == 6)
        std::printf("Keep all (stop)");
    else
        std::printf("Keep %s", dice_text(options[0].keep).c_str());
    std::printf("   [EV %.1f]\n", options[0].ev);
    for (std::size_t i = 1; i < options.size(); ++i) {
        const auto& o = options[i];
        std::printf("  next: %s (%+.2f)\n",
                    o.keep.size() == 6 ? "keep all (stop)" : ("keep " + dice_text(o.keep)).c_str(),
                    o.ev - options[0].ev);
    }
}

void print_categories(const std::vector<maxiyatzy::CategoryOption>& options) {
    if (options.empty()) return;
    std::printf("Fill %s = %d   [EV %.1f]\n",
                std::string(category_names[(std::size_t)options[0].category]).c_str(),
                options[0].score, options[0].ev);
    for (std::size_t i = 1; i < options.size(); ++i) {
        const auto& o = options[i];
        std::printf("  next: %s = %d (%+.2f)\n",
                    std::string(category_names[(std::size_t)o.category]).c_str(),
                    o.score, o.ev - options[0].ev);
    }
}

void apply_fill(GameState& game, Category category, int score) {
    const auto c = static_cast<std::size_t>(category);
    game.card[c] = score;
    game.score += score;
    if (c < 6) {
        game.upper_total += score;
        if (!game.bonus && game.upper_total >= 75) {
            game.bonus = true;
            game.score += 50;
        }
    }
    game.mask |= 1u << c;
}

} // namespace

int main(int argc, char** argv) {
    std::string table_path = "maxiyatzy-values.mytz";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--table" && i + 1 < argc) table_path = argv[++i];
        else { std::fprintf(stderr, "usage: %s [--table values.mytz]\n", argv[0]); return 1; }
    }
    std::optional<ValueTable> table;
    try {
        table.emplace(table_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\nDecompress the solved table first, e.g.:\n"
                     "  zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o maxiyatzy-values.mytz\n",
                     e.what());
        return 1;
    }
    Advisor advisor(*table);
    std::printf("Maxi Yatzy advisor. Optimal-play average: %.2f\n",
                table->initial_expected_score());
    std::printf("Type dice as digits (e.g. 115432). Commands: keep <dice>, fill <category>,\n"
                "card, undo, quit. Empty reroll keeps everything.\n\n");

    GameState game;
    std::vector<GameState> history;
    std::string line;

    while (game.mask != kFullMask) {
        const int turn = 1 + __builtin_popcount(game.mask);
        advisor.begin_turn(game.mask, game.upper_total);
        std::printf("[turn %d/20 · upper %d/75 · score %d · EV %.1f]\n",
                    turn, game.upper_total, game.score,
                    game.score + advisor.preroll_value());

        Dice roll;
        bool restart = false;
        // roll 1
        while (true) {
            if (!prompt("roll", line)) return 0;
            if (line == "card") { print_card(game); continue; }
            if (line == "quit") return 0;
            if (line == "undo") {
                if (history.empty()) { std::printf("nothing to undo\n"); continue; }
                game = history.back();
                history.pop_back();
                restart = true;
                break;
            }
            if (auto parsed = parse_dice(line, 6)) { roll = *parsed; break; }
            std::printf("enter exactly six digits 1-6\n");
        }
        if (restart) continue;

        history.push_back(game);
        for (int rerolls = 2; rerolls >= 1; --rerolls) {
            const auto options = advisor.keep_options(roll, rerolls, 3);
            print_keeps(options);
            Dice keep = options[0].keep;
            bool turn_over = false;
            while (true) {
                if (!prompt(rerolls == 2 ? "reroll 1" : "reroll 2", line)) return 0;
                if (line == "card") { print_card(game); continue; }
                if (line == "quit") return 0;
                if (line.empty() || line == ".") { turn_over = true; break; }
                if (line.rfind("keep", 0) == 0) {
                    auto parsed = parse_dice(line.substr(4), -1);
                    if (parsed && roll.contains(*parsed)) {
                        keep = *parsed;
                        const auto all = advisor.keep_options(roll, rerolls, 462);
                        const double best = all.front().ev;
                        double keep_ev = 0.0;
                        for (const auto& o : all)
                            if (o.keep.encode() == keep.encode()) { keep_ev = o.ev; break; }
                        std::printf("keeping %s (EV %.1f, %+.2f vs best)\n",
                                    dice_text(keep).c_str(), keep_ev, keep_ev - best);
                        continue;
                    }
                    std::printf("those dice are not in the roll\n");
                    continue;
                }
                auto parsed = parse_dice(line, 6 - keep.size());
                if (parsed) {
                    roll = keep;
                    for (int face = 0; face < 6; ++face)
                        roll.count[face] = static_cast<std::uint8_t>(
                            roll.count[face] + parsed->count[face]);
                    break;
                }
                std::printf("enter %d digits (the rerolled dice), 'keep <dice>' to change "
                            "the keep, or empty to stop\n", 6 - keep.size());
            }
            if (turn_over) break;
        }

        const auto options = advisor.category_options(roll, 3);
        print_categories(options);
        Category chosen = options[0].category;
        int chosen_score = options[0].score;
        while (true) {
            if (!prompt("fill (empty = accept)", line)) return 0;
            if (line == "card") { print_card(game); continue; }
            if (line == "quit") return 0;
            if (line.empty()) break;
            std::string name = line.rfind("fill", 0) == 0 ? line.substr(4) : line;
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (auto cat = parse_category(name, game.mask)) {
                const auto all = advisor.category_options(roll, category_count);
                for (const auto& o : all)
                    if (o.category == *cat) {
                        std::printf("filling %s = %d (%+.2f vs best)\n",
                                    std::string(category_names[(std::size_t)*cat]).c_str(),
                                    o.score, o.ev - all.front().ev);
                        chosen = o.category;
                        chosen_score = o.score;
                    }
                break;
            }
            std::printf("no unique open category matches '%s'\n", name.c_str());
        }
        apply_fill(game, chosen, chosen_score);
        print_card(game);
        std::printf("\n");
    }
    std::printf("Game over. Final score: %d\n", game.score);
    return 0;
}
