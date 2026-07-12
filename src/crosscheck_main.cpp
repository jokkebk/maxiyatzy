// Non-interactive EV dumper used by the JS-vs-C++ engine cross-check
// (web/test/crosscheck.mjs). It reads one command per line from stdin and
// writes one response line per command, so a Node driver can feed sampled
// states through the C++ Advisor and diff the results against web/engine.js.
//
// Protocol (all EVs printed with %.10g):
//   S <mask> <upper_total>      -> "<preroll_ev>"
//   K <rerolls_left> <6 faces>  -> "<counts6>:<ev> ..." (every distinct keep)
//   C <6 faces>                 -> "<cat>,<score>,<ev> ..." (every open category)
// where <6 faces> is six digits 1-6 describing the current roll and <counts6>
// is six digits giving the kept count of each face 1..6.
#include "maxiyatzy/policy.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using maxiyatzy::Advisor;
using maxiyatzy::Dice;
using maxiyatzy::ValueTable;
using maxiyatzy::category_count;

namespace {

Dice dice_from_faces(const std::string& faces) {
    Dice dice;
    for (char c : faces)
        if (c >= '1' && c <= '6') ++dice.count[c - '1'];
    return dice;
}

std::string counts6(const Dice& dice) {
    std::string out(6, '0');
    for (int f = 0; f < 6; ++f) out[f] = static_cast<char>('0' + dice.count[f]);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string table_path = argc > 1 ? argv[1] : "maxiyatzy-values.mytz";
    ValueTable table(table_path);
    Advisor advisor(table);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        char cmd;
        if (!(in >> cmd)) { std::printf("\n"); std::fflush(stdout); continue; }
        if (cmd == 'S') {
            std::uint32_t mask;
            int upper;
            in >> mask >> upper;
            advisor.begin_turn(mask, upper);
            std::printf("%.10g\n", advisor.preroll_value());
        } else if (cmd == 'K') {
            int rerolls;
            std::string faces;
            in >> rerolls >> faces;
            const auto options = advisor.keep_options(dice_from_faces(faces), rerolls, 462);
            bool first = true;
            for (const auto& o : options) {
                std::printf("%s%s:%.10g", first ? "" : " ", counts6(o.keep).c_str(), o.ev);
                first = false;
            }
            std::printf("\n");
        } else if (cmd == 'C') {
            std::string faces;
            in >> faces;
            const auto options = advisor.category_options(dice_from_faces(faces), category_count);
            bool first = true;
            for (const auto& o : options) {
                std::printf("%s%d,%d,%.10g", first ? "" : " ",
                            static_cast<int>(o.category), o.score, o.ev);
                first = false;
            }
            std::printf("\n");
        } else {
            std::printf("\n");
        }
        std::fflush(stdout);
    }
    return 0;
}
