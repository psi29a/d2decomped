// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: iso9660
// Check generated ISO 9660 and Joliet images, plus the install disc if present.

#include <iso9660.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <string>
#include <string_view>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "FAIL: %s (line %d)\n", #x, __LINE__); return 1; } \
} while (0)

namespace {
using namespace std::string_view_literals;
constexpr std::uint32_t kSector = 2048;
constexpr std::uint32_t kBlocks = 24;
constexpr auto kContents = "ISO test payload\n"sv;

void both_endian(unsigned char* target, std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto octet = static_cast<unsigned char>(value >> (8 * i));
        target[i] = octet;
        target[2 * width - i - 1] = octet;
    }
}

std::size_t record(unsigned char* target, std::uint32_t extent, std::uint32_t size,
                   bool directory, std::string_view name) {
    const auto length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
    target[0] = static_cast<unsigned char>(length);
    both_endian(target + 2, extent, 4);
    both_endian(target + 10, size, 4);
    target[25] = directory ? 2 : 0;
    both_endian(target + 28, 1, 2);
    target[32] = static_cast<unsigned char>(name.size());
    std::memcpy(target + 33, name.data(), name.size());
    return length;
}

int check_generated_image(bool joliet) {
    namespace fs = std::filesystem;
    std::array<unsigned char, kBlocks * kSector> image{};
    const auto block = [&](std::size_t sector) { return image.data() + sector * kSector; };
    const auto descriptor = [&](std::size_t sector, unsigned char type, std::uint32_t root) {
        auto* desc = block(sector);
        desc[0] = type;
        std::memcpy(desc + 1, "CD001", 5);
        desc[6] = 1;
        if (type == 255) return;
        both_endian(desc + 80, kBlocks, 4);
        both_endian(desc + 128, kSector, 2);
        record(desc + 156, root, kSector, true, "\0"sv);
    };
    const auto directory = [&](std::uint32_t sector, std::uint32_t parent) {
        auto* cursor = block(sector);
        cursor += record(cursor, sector, kSector, true, "\0"sv);
        cursor += record(cursor, parent, kSector, true, "\1"sv);
        return cursor;
    };
    descriptor(16, 1, 19);
    descriptor(joliet ? 18 : 17, 255, 0);
    record(directory(19, 19), 23, kContents.size(), false, "README.DAT;1"sv);
    if (joliet) {
        descriptor(17, 2, 20);
        std::memcpy(block(17) + 88, "%/E", 3);
        record(directory(20, 20), 21, kSector, true, "\0" "D\0" "A\0" "T\0" "A"sv);
        record(directory(21, 20), 23, kContents.size(), false, "\0\xe9\0.\0D\0A\0T\0;\0" "1"sv);
    }
    std::memcpy(block(23), kContents.data(), kContents.size());

    const auto scratch = fs::temp_directory_path() / ("d2d-test-iso-" + std::to_string(std::random_device{}()));
    CHECK(fs::create_directory(scratch));
    const auto path = scratch / "fixture.iso";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    auto reader = iso9660::Reader::open(path);
    bool matches = false;
    if (reader && reader->entries().size() == (joliet ? 2u : 1u)) {
        const auto& entry = reader->entries().back();
        const auto bytes = reader->read(entry);
        matches = (!joliet || (reader->entries().front().directory && reader->entries().front().path == "/DATA"))
            && entry.path == (joliet ? "/DATA/\xc3\xa9.DAT" : "/README.DAT") && !entry.directory
            && bytes.size() == kContents.size() && std::memcmp(bytes.data(), kContents.data(), bytes.size()) == 0;
    }
    reader.reset();
    fs::remove(path);
    fs::remove(scratch);
    CHECK(matches);
    return 0;
}

int check_install_disc() {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    if (!home) return 0;

    const fs::path iso = fs::path(home) / "Downloads"
        / "Diablo II + LoD" / "1. INSTALL DISC.ISO";
    if (!fs::exists(iso)) return 0;

    auto reader = iso9660::Reader::open(iso);
    CHECK(reader.has_value());

    const auto& entries = reader->entries();
    CHECK(entries.size() > 10);

    std::size_t dirs = 0, files = 0;
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        if (entry.directory) ++dirs;
        else { ++files; total += entry.size; }
    }
    CHECK(dirs > 0);
    CHECK(files > 0);
    CHECK(total > 100ull * 1024 * 1024);   // install disc is ~500 MB

    // Round-trip: read the first small-ish file, confirm we got its bytes.
    for (const auto& entry : entries) {
        if (!entry.directory && entry.size > 0 && entry.size < 4096) {
            auto bytes = reader->read(entry);
            CHECK(bytes.size() == entry.size);
            std::printf("test_iso9660: OK — %zu entries, %llu bytes total, "
                        "sample=%s (%u B)\n",
                        entries.size(), (unsigned long long)total,
                        entry.path.c_str(), entry.size);
            return 0;
        }
    }
    std::puts("test_iso9660: OK (no small file to round-trip, still valid)");
    return 0;
}
} // namespace

int main() try {
    if (check_generated_image(false) != 0 || check_generated_image(true) != 0) return 1;
    std::puts("test_iso9660: generated ISO 9660 and Joliet fixtures passed");
    return check_install_disc();
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_iso9660: %s\n", error.what());
    return 1;
}
