#include "maxiyatzy/policy.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace maxiyatzy {
namespace {

constexpr std::uint32_t all_categories_mask = (std::uint32_t{1} << category_count) - 1;
constexpr std::size_t mask_count = std::size_t{1} << category_count;

std::uint32_t read_u32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::uint64_t read_u64(const unsigned char* p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

} // namespace

ValueTable::ValueTable(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open table: " + path);
    struct stat st{};
    if (::fstat(fd, &st) != 0) { ::close(fd); throw std::runtime_error("cannot stat table"); }
    map_size_ = static_cast<std::size_t>(st.st_size);
    void* map = ::mmap(nullptr, map_size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) throw std::runtime_error("cannot mmap table");
    map_ = static_cast<const unsigned char*>(map);

    constexpr std::size_t fixed_header = 8 + 5 * 4 + 2 * 8;
    constexpr std::size_t state_records = 64 * (1 + upper_state_slots);
    if (map_size_ < fixed_header + state_records ||
        std::memcmp(map_, "MYTZDP1\0", 8) != 0 ||
        read_u32(map_ + 8) != 1 ||                       // format version
        read_u32(map_ + 16) != mask_count ||
        read_u32(map_ + 20) != upper_state_slots ||
        read_u32(map_ + 24) != 2) {                      // XOR-delta float32
        throw std::runtime_error("unsupported table header: " + path);
    }
    const std::uint64_t value_count = read_u64(map_ + 28);

    const unsigned char* record = map_ + fixed_header;
    for (int m = 0; m < 64; ++m) {
        const int count = record[0];
        slot_[m].fill(-1);
        for (int i = 0; i < count; ++i) {
            states_[m].push_back(record[1 + i]);
            slot_[m][record[1 + i]] = i;
        }
        record += 1 + upper_state_slots;
        for (int face = 1; face <= 6; ++face)
            if (!(m & (1 << (face - 1)))) future_max_[m] += 6 * face;
    }
    payload_ = record;

    offsets_.resize(mask_count + 1);
    offsets_[0] = 0;
    for (std::size_t mask = 0; mask < mask_count; ++mask)
        offsets_[mask + 1] = offsets_[mask] + states_[mask & 63].size();
    if (offsets_[mask_count] != value_count ||
        map_size_ < fixed_header + state_records + value_count * 4) {
        throw std::runtime_error("table value payload has unexpected size");
    }
}

ValueTable::~ValueTable() {
    if (map_ != nullptr) ::munmap(const_cast<unsigned char*>(map_), map_size_);
}

int ValueTable::canonical_state(std::uint32_t mask, int upper_total) const {
    if (upper_total >= upper_bonus_threshold) return bonus_achieved_state;
    if (upper_total + future_max_[mask & 63] < upper_bonus_threshold)
        return bonus_impossible_state;
    return upper_total;
}

double ValueTable::value(std::uint32_t mask, int state) const {
    if (mask == all_categories_mask) return 0.0;
    const int slot = slot_[mask & 63][state];
    if (slot < 0) throw std::logic_error("state not stored for mask");
    const unsigned char* record = payload_ + offsets_[mask] * 4;
    std::uint32_t bits = 0;
    for (int i = 0; i <= slot; ++i) bits ^= read_u32(record + i * 4);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

double ValueTable::value_from_total(std::uint32_t mask, int upper_total) const {
    if (mask == all_categories_mask) return 0.0;
    return value(mask, canonical_state(mask, upper_total));
}

Advisor::Advisor(const ValueTable& table) : table_(table) {
    dice_ = enumerate_dice_up_to(6);
    id_of_.assign(117649, -1);
    for (int id = 0; id < static_cast<int>(dice_.size()); ++id) {
        id_of_[dice_[id].encode()] = id;
        by_size_[dice_[id].size()].push_back(id);
    }
    add_face_.resize(dice_.size());
    for (int id = 0; id < static_cast<int>(dice_.size()); ++id) {
        add_face_[id].fill(-1);
        if (dice_[id].size() < 6) {
            for (int face = 0; face < 6; ++face) {
                Dice next = dice_[id];
                ++next.count[face];
                add_face_[id][face] = id_of_[next.encode()];
            }
        }
    }
    keepers_.resize(dice_.size());
    multiplicity_.assign(dice_.size(), 0.0);
    auto factorial = [](int n) { int r = 1; for (int i = 2; i <= n; ++i) r *= i; return r; };
    for (int full : by_size_[6]) {
        int denominator = 1;
        for (auto count : dice_[full].count) denominator *= factorial(count);
        multiplicity_[full] = factorial(6) / denominator;
        for (int keeper = 0; keeper < static_cast<int>(dice_.size()); ++keeper)
            if (dice_[full].contains(dice_[keeper])) keepers_[full].push_back(keeper);
    }
    for (std::size_t category = 0; category < category_count; ++category) {
        scores_[category].assign(dice_.size(), 0);
        for (int full : by_size_[6])
            scores_[category][full] = score(static_cast<Category>(category), dice_[full]);
    }
    f3_.resize(dice_.size());
    k2_.resize(dice_.size());
    k1_.resize(dice_.size());
}

int Advisor::dice_id(const Dice& dice) const {
    return id_of_[dice.encode()];
}

void Advisor::keeper_expectation(const std::vector<double>& finals,
                                 std::vector<double>& k) const {
    for (int full : by_size_[6]) k[full] = finals[full];
    for (int size = 5; size >= 0; --size)
        for (int id : by_size_[size]) {
            double total = 0.0;
            for (int face = 0; face < 6; ++face) total += k[add_face_[id][face]];
            k[id] = total / 6.0;
        }
}

void Advisor::begin_turn(std::uint32_t mask, int upper_total) {
    mask_ = mask;
    upper_total_ = upper_total;
    for (std::size_t category = 0; category < category_count; ++category) {
        open_[category] = !(mask & (std::uint32_t{1} << category));
        if (!open_[category]) continue;
        const std::uint32_t next_mask = mask | (std::uint32_t{1} << category);
        if (category < 6) {
            for (int count = 0; count <= 6; ++count) {
                const int next_total = upper_total + static_cast<int>(category + 1) * count;
                const double bonus = (upper_total < upper_bonus_threshold &&
                                      next_total >= upper_bonus_threshold) ? 50.0 : 0.0;
                cont_[category][count] = bonus + table_.value_from_total(next_mask, next_total);
            }
        } else {
            cont_[category].fill(table_.value_from_total(next_mask, upper_total));
        }
    }
    for (int full : by_size_[6]) {
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t category = 0; category < category_count; ++category) {
            if (!open_[category]) continue;
            const int count = category < 6 ? dice_[full].count[category] : 0;
            best = std::max(best, scores_[category][full] + cont_[category][count]);
        }
        f3_[full] = best;
    }
    keeper_expectation(f3_, k2_);
    std::vector<double> f2(dice_.size());
    for (int full : by_size_[6]) {
        double best = -std::numeric_limits<double>::infinity();
        for (int held : keepers_[full]) best = std::max(best, k2_[held]);
        f2[full] = best;
    }
    keeper_expectation(f2, k1_);
    double expected = 0.0;
    for (int full : by_size_[6]) {
        double best = -std::numeric_limits<double>::infinity();
        for (int held : keepers_[full]) best = std::max(best, k1_[held]);
        expected += multiplicity_[full] * best;
    }
    preroll_ = expected / 46656.0;
}

std::vector<KeepOption> Advisor::keep_options(
    const Dice& roll, int rerolls_left, std::size_t top_k) const {
    const auto& k = rerolls_left == 2 ? k1_ : k2_;
    const int full = id_of_[roll.encode()];
    std::vector<KeepOption> options;
    options.reserve(keepers_[full].size());
    for (int held : keepers_[full]) options.push_back({dice_[held], k[held]});
    std::sort(options.begin(), options.end(),
              [](const auto& a, const auto& b) { return a.ev > b.ev; });
    if (options.size() > top_k) options.resize(top_k);
    return options;
}

std::vector<CategoryOption> Advisor::category_options(
    const Dice& roll, std::size_t top_k) const {
    const int full = id_of_[roll.encode()];
    std::vector<CategoryOption> options;
    for (std::size_t category = 0; category < category_count; ++category) {
        if (!open_[category]) continue;
        const int count = category < 6 ? dice_[full].count[category] : 0;
        options.push_back({static_cast<Category>(category), scores_[category][full],
                           scores_[category][full] + cont_[category][count]});
    }
    std::sort(options.begin(), options.end(),
              [](const auto& a, const auto& b) { return a.ev > b.ev; });
    if (options.size() > top_k) options.resize(top_k);
    return options;
}

} // namespace maxiyatzy
