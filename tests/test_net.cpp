// SPDX-License-Identifier: GPL-3.0-or-later
// The D2GS codec and the join session against a scripted host, over a
// synthetic Huffman table (no game.exe bytes): docs/design/net-join-plan.md
// M1-M3.
#include <d2gs/c2s.hpp>
#include <d2gs/exe_tables.hpp>
#include <d2gs/frame.hpp>
#include <d2gs/huffman.hpp>
#include <d2gs/s2c_names.hpp>
#include <d2gs/split.hpp>
#include <join.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace d2d::net;
using d2gs::Bytes;

namespace {

// A complete code that isn't game.exe's: two symbols of 7 bits, four of
// 9, the rest 8 (Kraft sum exactly 1).
auto synthetic_lengths() -> d2gs::CodeLengths {
    d2gs::CodeLengths lengths{};
    lengths.fill(8);
    lengths[0x00] = 7;   // the two 7-bit codes take 2 * 2^-8 more...
    lengths[0x01] = 9;   // ...which four 9-bit ones give back
    lengths[0x02] = 9;
    lengths[0x03] = 9;
    lengths[0x04] = 9;
    lengths[0x05] = 7;
    return lengths;
}

auto synthetic_sizes() -> d2gs::SizeTable {
    d2gs::SizeTable sizes{};
    sizes[0x00] = 1; sizes[0x01] = 8; sizes[0x02] = 1; sizes[0x03] = 12; sizes[0x04] = 1;
    sizes[0x05] = 1; sizes[0x06] = 1; sizes[0x59] = 26; sizes[0x8f] = 33; sizes[0xb0] = 1; sizes[0xb4] = 5;
    sizes[0xaf] = -1; sizes[0xb3] = -1;
    return sizes;
}

auto save_of(std::size_t size) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> save(size, 0);
    save[0] = 0x55; save[1] = 0xaa; save[2] = 0x55; save[3] = 0xaa;
    const char name[] = "Tester";
    for (std::size_t i = 0; i < sizeof name - 1; ++i) save[0x14 + i] = static_cast<std::uint8_t>(name[i]);
    save[0x28] = 3;   // paladin
    return save;
}

} // namespace

int main() {
    // Huffman: refuses a bad code, round trips any bytes.
    const auto lengths = synthetic_lengths();
    assert(d2gs::check_lengths(lengths));
    auto broken = lengths;
    broken[0x10] = 0;
    assert(!d2gs::check_lengths(broken) && !d2gs::Huffman::build(broken));
    const auto huffman = d2gs::Huffman::build(lengths);
    assert(huffman);
    std::mt19937 random(7);
    for (int round = 0; round < 300; ++round) {
        Bytes input(random() % 600);
        for (auto& byte : input) byte = static_cast<std::uint8_t>(random());
        Bytes packed, unpacked;
        huffman->compress(input, packed);
        huffman->decompress(packed, unpacked);
        assert(unpacked == input);
    }

    // Frames: 1-byte headers under 0xf0, 2-byte past; split across reads.
    assert(d2gs::read_frame_header(Bytes{ 0x05, 0x00 })->size == 5 && d2gs::read_frame_header(Bytes{ 0x05, 0x00 })->header_size == 1);
    assert(d2gs::read_frame_header(Bytes{ 0xf1, 0x23 })->size == 0x123 && d2gs::read_frame_header(Bytes{ 0xf1, 0x23 })->header_size == 2);
    assert(!d2gs::read_frame_header(Bytes{ 0x05 }));
    {
        const Bytes payload{ 0x01, 0x00, 0x04, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x02 };
        const auto frame = d2gs::encode_frame(*huffman, payload);
        assert(frame);
        d2gs::FrameReader reader;
        reader.append(std::span(*frame).first(1));
        assert(reader.next(*huffman) && !*reader.next(*huffman));
        reader.append(std::span(*frame).subspan(1));
        const auto whole = reader.next(*huffman);
        assert(whole && *whole && **whole == payload);
    }

    // The splitter: fixed sizes, a packet across appends, an unknown id is a Desync.
    {
        d2gs::Splitter splitter(synthetic_sizes());
        splitter.append(Bytes{ 0x02, 0x04, 0x01, 0, 0 });
        assert(**splitter.next() == Bytes{ 0x02 } && **splitter.next() == Bytes{ 0x04 } && !*splitter.next());
        splitter.append(Bytes{ 0, 0, 0, 0, 0 });
        assert((*splitter.next())->size() == 8);
        splitter.append(Bytes{ 0x77 });
        assert(!splitter.next());
    }

    // C->S: 0x68 is 37 bytes; a save goes out in 0xff chunks that add up to it.
    {
        const auto join = d2gs::c2s::join_request(3, "Tester");
        assert(join.size() == d2gs::c2s::kJoinSize && join[0] == 0x68 && join[7] == 3 && join[8] == 0x0e && join[0x15] == 'T');
        const auto save = save_of(2516);
        const auto chunks = d2gs::c2s::save_chunks(save);
        std::size_t total = 0;
        for (const auto& chunk : chunks) {
            assert(chunk[0] == 0x6c && chunk.size() == chunk[1] + 7u && d2gs::read_u32(chunk, 2) == 2516);
            total += chunk[1];
        }
        assert(chunks.size() == 10 && total == 2516);
        assert(d2gs::c2s::ping(1234).size() == 13);
    }
    assert(d2gs::s2c_name(0x59) == "assign player" && d2gs::s2c_name(0xb5).empty());

    // The join, against a scripted host.
    const d2gs::ExeTables tables{ lengths, synthetic_sizes() };
    auto frame = [&](Bytes payload) { return *d2gs::encode_frame(*huffman, payload); };
    {
        assert(!JoinSession::create(tables, save_of(0x2000)));   // past what the host takes
        assert(valid_join_name("Tester") && valid_join_name("Mule_abcd") && valid_join_name("O'Neil"));
        assert(!valid_join_name("Bob Bitchen") && !valid_join_name("_Mule") && !valid_join_name("a-b-c") && !valid_join_name("X"));
        auto session = JoinSession::create(tables, save_of(600));
        assert(session && session->name() == "Tester");
        auto step = session->receive(Bytes{ 0xaf, 0x01 }, 0);                       // raw hello
        assert(session->state() == JoinState::Uploading && step.to_send.size() == 1 + 3);
        assert(step.to_send[0][0] == 0x68 && step.to_send[1][0] == 0x6c);
        step = session->receive(frame({ 0x01, 0, 4, 0, 0x10, 0, 1, 0, 0x00, 0x02 }), 10);
        assert(session->state() == JoinState::Loading && step.to_send == std::vector<Bytes>{ { 0x6b } } && step.packets.size() == 3);
        step = session->receive(frame({ 0x04 }), 20);
        assert(session->state() == JoinState::InGame && step.to_send.empty());
        assert(session->tick(4000).empty() && session->tick(5020).size() == 1 && session->tick(6000).empty());
        assert(session->leave() == std::vector<Bytes>{ { 0x69 } } && session->state() == JoinState::Leaving);
        session->receive(frame({ 0xb3, 3, 1, 5, 0, 0, 0, 0xaa, 0xbb, 0xcc }), 30);
        session->receive(frame({ 0xb3, 2, 0, 5, 0, 0, 0, 0xdd, 0xee, 0xb0, 0x05, 0x06 }), 40);
        assert(session->save_back() == (Bytes{ 0xaa, 0xbb, 0xcc, 0xdd, 0xee }) && session->save_back_total() == 5);
        assert(session->state() == JoinState::Closed);
    }
    {
        // Refused: B4, then the 01 00 02 the host still flushes gets no 6b.
        auto session = JoinSession::create(tables, save_of(600));
        session->receive(Bytes{ 0xaf, 0x01 }, 0);
        const auto step = session->receive(frame({ 0xb4, 0x10, 0, 0, 0, 0x01, 0, 4, 0, 0x10, 0, 1, 0, 0x00, 0x02 }), 5);
        assert(session->state() == JoinState::Refused && session->refused_reason() == 0x10 && step.to_send.empty());
    }
    {
        // A stream that can't be split ends the session.
        auto session = JoinSession::create(tables, save_of(600));
        session->receive(Bytes{ 0xaf, 0x01 }, 0);
        session->receive(frame({ 0x77, 0x01 }), 5);
        assert(session->state() == JoinState::Desync && !session->desync_reason().empty());
    }
    std::puts("test_net: ok");
    return 0;
}
