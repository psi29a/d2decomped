// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// rules::wav_ms: a WAV's `data` size over the `fmt ` chunk's average bytes a
// second, in ms, whatever chunks come between; not a WAV, or no rate: none.

#include <wav_length.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

void put32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::byte(value >> shift & 0xff));
}
void put_tag(std::vector<std::byte>& out, std::string_view tag) {
    for (const char letter : tag) out.push_back(std::byte(letter));
}
// A RIFF WAVE with a 16-byte fmt (its byte rate at +8), an optional chunk between, and `data` of `size` bytes.
std::vector<std::byte> wav(std::uint32_t byte_rate, std::uint32_t size, bool extra) {
    std::vector<std::byte> out;
    put_tag(out, "RIFF"); put32(out, 0); put_tag(out, "WAVE");
    put_tag(out, "fmt "); put32(out, 16);
    for (const std::uint32_t word : { 0x00010001u, 22050u, byte_rate, 0x00100002u }) put32(out, word);
    if (extra) { put_tag(out, "LIST"); put32(out, 3); for (int k = 0; k < 4; ++k) out.push_back(std::byte(0)); }   // odd: padded
    put_tag(out, "data"); put32(out, size);
    return out;
}

}  // namespace

int main() try {
    assert(d2d::rules::wav_ms(wav(44100, 44100, false)) == 1000u);
    assert(d2d::rules::wav_ms(wav(44100, 66150, true)) == 1500u);   // past an odd chunk's pad byte
    assert(d2d::rules::wav_ms(wav(11100, 5550, false)) == 500u);    // IMA ADPCM: the avg byte rate
    assert(!d2d::rules::wav_ms(wav(0, 100, false)));
    // A fmt chunk cut short before its byte rate, and a chunk whose size would wrap the walk: none, no overrun or loop.
    auto cut = wav(44100, 100, false);
    cut.resize(12 + 8 + 10);
    assert(!d2d::rules::wav_ms(cut));
    std::vector<std::byte> huge;
    put_tag(huge, "RIFF"); put32(huge, 0); put_tag(huge, "WAVE");
    put_tag(huge, "LIST"); put32(huge, 0xfffffff8u);
    assert(!d2d::rules::wav_ms(huge));
    std::vector<std::byte> junk(40, std::byte(0));
    assert(!d2d::rules::wav_ms(junk));
    std::puts("OK: wav_ms");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_wav_length: %s\n", error.what());
    return 1;
}
