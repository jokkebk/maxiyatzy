#include "maxiyatzy/solver.hpp"

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    maxiyatzy::SolverOptions options;
    std::string output = "maxiyatzy-values.mytz";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--threads" && i + 1 < argc) options.threads = std::atoi(argv[++i]);
        else if (argument == "--chunk" && i + 1 < argc)
            options.chunk_size = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--quiet") options.progress = false;
        else {
            std::cerr << "usage: maxiyatzy-solve [--threads N] [--chunk N] "
                         "[--output FILE] [--quiet]\n";
            return 2;
        }
    }
    try {
        maxiyatzy::Solver solver(options);
        solver.solve();
        std::cout << "optimal expected score: " << std::fixed << std::setprecision(9)
                  << solver.initial_expected_score() << '\n';
        solver.write_compact(output);
        std::cout << "wrote " << output << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
