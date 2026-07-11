#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

template <class T> bool read(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
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

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: maxiyatzy-inspect FILE.mytz\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    char magic[8];
    std::uint32_t version, flags, mask_count, upper_slots, encoding;
    std::uint64_t value_count, expected_checksum;
    if (!in.read(magic, 8) || std::memcmp(magic, "MYTZDP1\0", 8) != 0 ||
        !read(in, version) || !read(in, flags) || !read(in, mask_count) ||
        !read(in, upper_slots) || !read(in, encoding) || !read(in, value_count) ||
        !read(in, expected_checksum)) {
        std::cerr << "invalid or truncated header\n";
        return 1;
    }
    if (version != 1 || mask_count != (1u << 20) || upper_slots != 77 || encoding != 2) {
        std::cerr << "unsupported format parameters\n";
        return 1;
    }
    std::array<std::uint8_t,64> counts{};
    for (int mask = 0; mask < 64; ++mask) {
        counts[mask] = static_cast<std::uint8_t>(in.get());
        in.ignore(77);
    }

    std::uint64_t actual_count = 0;
    std::uint64_t checksum = 14695981039346656037ULL;
    float initial_value = 0.0f;
    bool have_initial = false;
    for (std::uint32_t mask = 0; mask < mask_count; ++mask) {
        std::uint32_t previous = 0;
        for (int index = 0; index < counts[mask & 63]; ++index) {
            std::uint32_t encoded;
            if (!read(in, encoded)) {
                std::cerr << "truncated value payload\n";
                return 1;
            }
            checksum = fnv1a(checksum, &encoded, sizeof(encoded));
            previous ^= encoded;
            if (!have_initial) {
                std::memcpy(&initial_value, &previous, sizeof(initial_value));
                have_initial = true;
            }
            ++actual_count;
        }
    }
    if (actual_count != value_count || checksum != expected_checksum || in.peek() != EOF) {
        std::cerr << "payload validation failed\n";
        return 1;
    }
    std::cout << "format_version=" << version
              << " voluntary_crossing=" << ((flags & 1) != 0)
              << " values=" << actual_count
              << " checksum=ok initial_expected_score="
              << std::fixed << std::setprecision(6) << initial_value << '\n';
}

