// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: d2gs d2s
// game.exe's bit buffer (FUN_00410e40, 00410e90, 00410eb0, 00410f60 /
// 00411020, 00411030, 00411070) and the save checksum (FUN_00411130)
// against stock 1.14d's own, run by tools/emu/bit_buffer.py:
// d2d::net::d2gs::BitReader / BitWriter and d2d::d2s::save_checksum. No
// game data needed at runtime. With a file argument (bit_buffer.py's `game`
// output, or its `gen` cases) it prints each line with d2d's answer
// instead, to diff.
#include <d2gs/bit_buffer.hpp>
#include <d2s_write.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using d2d::net::d2gs::BitCursor;
using d2d::net::d2gs::BitReader;
using d2d::net::d2gs::BitWriter;

constexpr const char* kFixture[] = {
#include "bit_buffer_cases.inc"
};

auto from_hex(const std::string& text) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> bytes;
    if (text == "-") return bytes;
    for (std::size_t at = 0; at + 1 < text.size(); at += 2) bytes.push_back(std::uint8_t(std::stoul(text.substr(at, 2), nullptr, 16)));
    return bytes;
}

auto to_hex(const std::vector<std::uint8_t>& bytes) -> std::string {
    if (bytes.empty()) return "-";
    std::string text;
    char pair[3] = {};
    for (const std::uint8_t byte : bytes) {
        std::snprintf(pair, sizeof pair, "%02x", unsigned(byte));
        text += pair;
    }
    return text;
}

// "W,-5,7" as its numbers after the letter.
auto numbers(const std::string& step) -> std::vector<std::int32_t> {
    std::vector<std::int32_t> out;
    std::stringstream stream(step.substr(step.find(',') == std::string::npos ? step.size() : step.find(',') + 1));
    std::string part;
    while (std::getline(stream, part, ',')) out.push_back(std::int32_t(std::stoll(part)));
    return out;
}

auto cursor_fields(const BitCursor& state, std::size_t pointer, const std::vector<std::uint8_t>& bytes) -> std::vector<std::string> {
    return { std::to_string(state.byte), std::to_string(state.bit), std::to_string(state.overflow ? 1 : 0), std::to_string(pointer), to_hex(bytes) };
}

// One case's answers, as bit_buffer.py's `game` prints them.
auto answer(const std::vector<std::string>& fields) -> std::vector<std::string> {
    const std::string& kind = fields[1];
    std::vector<std::uint8_t> bytes = from_hex(fields[3]);
    std::vector<std::string> out;
    if (kind == "c") {
        std::vector<std::byte> raw(bytes.size());
        for (std::size_t at = 0; at < bytes.size(); ++at) raw[at] = std::byte(bytes[at]);
        out.push_back(std::to_string(std::int32_t(d2d::d2s::save_checksum(raw))));
        return out;
    }
    if (kind == "r") {
        BitReader reader(bytes);
        for (std::size_t at = 4; at < fields.size(); ++at) {
            const std::string& step = fields[at];
            if (step == "U") out.push_back(std::to_string(reader.cursor().bytes_used()));
            else if (step[0] == 'R') out.push_back(std::to_string(reader.read(numbers(step)[0])));
            else if (step[0] == 'S') out.push_back(std::to_string(reader.read_signed(numbers(step)[0])));
        }
        // A reader's pointer is its byte: they only part on a write's overflow.
        for (auto& field : cursor_fields(reader.cursor(), std::size_t(reader.cursor().byte), bytes)) out.push_back(field);
        return out;
    }
    BitWriter writer(bytes);
    for (std::size_t at = 4; at < fields.size(); ++at) {
        const std::string& step = fields[at];
        if (step == "U") out.push_back(std::to_string(writer.cursor().bytes_used()));
        else if (step == "A") writer.align();
        else if (step[0] == 'W') {
            const auto args = numbers(step);
            writer.write(args[0], args[1]);
        }
    }
    for (auto& field : cursor_fields(writer.cursor(), writer.pointer(), bytes)) out.push_back(field);
    return out;
}

// The case part of a line (before `|`) and the whole line as d2d answers it.
auto line_of(const std::string& text) -> std::string {
    std::stringstream stream(text);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field && field != "|") fields.push_back(field);
    std::string line;
    for (const auto& part : fields) line += part + '\t';
    line += '|';
    for (const auto& part : answer(fields)) line += '\t' + part;
    return line;
}

auto spaced(std::string text) -> std::string {
    for (char& letter : text)
        if (letter == '\t') letter = ' ';
    return text;
}

void hand_checks() {
    // Two bytes, LSB first: 0xb4 = 1011 0100, 0x01.
    const std::vector<std::uint8_t> packet = { 0xb4, 0x01 };
    BitReader reader(packet);
    assert(reader.read(2) == 0 && reader.read(3) == 5 && reader.cursor().bytes_used() == 1);
    assert(reader.read_signed(3) == -3);                           // 101: bit 2 set, sign-extended
    assert(reader.read(12) == 1 && reader.cursor().overflow);      // 12 asked, 8 left: those 8 come back
    assert(reader.cursor().byte == 2 && reader.cursor().bit == 0);
    // A count of INT32_MIN wraps game.exe's 32-bit past-the-end sum to
    // 0x7ffffff0: all 16 bits come back, bit 31 clear, so no sign.
    BitReader whole(packet);
    assert(whole.read_signed(INT32_MIN) == 0x1b4 && whole.cursor().overflow);

    // A write zeroes the byte it starts and the next whole one; a write
    // that doesn't fit writes nothing and jumps to the end.
    std::vector<std::uint8_t> out = { 0xff, 0xff, 0xff };
    BitWriter writer(out);
    writer.write(5, 3);
    assert(out[0] == 5 && out[1] == 0xff);
    writer.write(0x1ff, 9);                                         // fills byte 0, 4 bits into byte 1
    assert(out[0] == 0xfd && out[1] == 0x0f && writer.cursor().bytes_used() == 2);
    writer.align();
    assert(out[2] == 0 && writer.cursor().byte == 2 && writer.cursor().bit == 0);
    writer.write(1, 9);
    assert(writer.cursor().overflow && writer.cursor().byte == 3 && writer.pointer() == 2);
    out[2] = 0x77;
    writer.write(1, 0);                                             // 0 bits: zeroes where the pointer was left
    assert(out[2] == 0);

    // game.exe's 32-bit end test wraps on a count near INT32_MAX with bits
    // used, and it then writes past the buffer; d2d takes it as an overflow.
    std::vector<std::uint8_t> one = { 0 };
    BitWriter small(one);
    small.write(1, 1);
    small.write(0, INT32_MAX);
    assert(small.cursor().overflow && small.cursor().byte == 1 && small.cursor().bit == 0);
    assert(small.pointer() == 0 && one[0] == 1);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        std::string text;
        while (std::getline(file, text))
            if (!text.empty()) std::printf("%s\n", line_of(text).c_str());
        return 0;
    }
    hand_checks();
    int matched = 0;
    for (const char* row : kFixture) {
        const std::string want = row;
        const std::string got = spaced(line_of(want));
        if (got != want) {
            std::fprintf(stderr, "game.exe: %s\nd2d:      %s\n", want.c_str(), got.c_str());
            return 1;
        }
        ++matched;
    }
    std::printf("bit buffer: %d of game.exe's cases match\n", matched);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_bit_buffer: %s\n", error.what());
    return 1;
}
