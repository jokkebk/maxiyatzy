#include "maxiyatzy/dice.hpp"
#include "maxiyatzy/upper_states.hpp"

#include <iomanip>
#include <iostream>

int main() {
    const auto analysis = maxiyatzy::analyze_upper_states();
    std::cout << "complete unordered rolls: " << maxiyatzy::enumerate_dice(6).size() << '\n'
              << "partial dice states (0..6): " << maxiyatzy::enumerate_dice_up_to(6).size() << '\n'
              << "reachable upper mask/total pairs: " << analysis.reachable_mask_total_pairs << '\n'
              << "effective upper states: " << analysis.effective_mask_total_states << '\n'
              << "reachable boundary states: " << analysis.reachable_boundary_states << '\n'
              << "effective boundary states: " << analysis.effective_boundary_states << '\n'
              << "all boundary values (double): " << std::fixed << std::setprecision(1)
              << analysis.effective_boundary_states * sizeof(double) / 1048576.0 << " MiB\n"
              << "peak adjacent layers (double): "
              << analysis.peak_adjacent_layer_states * sizeof(double) / 1048576.0 << " MiB\n";
}

