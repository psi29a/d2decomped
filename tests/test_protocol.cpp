// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The command codec (apps/d2d/protocol.hpp): every command survives its
// wire form, and malformed messages are refused.
#include <protocol.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <variant>
#include <vector>

using namespace d2d::game;

int main() try {
    const std::vector<Command> all{
        cmd::Move{ 12.5f, 3.25f, true }, cmd::UseSkill{ 36, 1.f, 2.f, 77, true }, cmd::UseSkill{ 0, 4.f, 5.f, -1, false },
        cmd::Interact{ 9 }, cmd::Pickup{ 41 }, cmd::Resurrect{}, cmd::Transmute{}, cmd::StatPoint{ 3, 5 }, cmd::SkillPoint{ 12 },
        cmd::SelectSkill{ 98, false }, cmd::UseBelt{ 2 }, cmd::UseItem{ 23 }, cmd::ToCursor{ 17 }, cmd::Drop{ 17 }, cmd::ToGrid{ 5, -1, 3 }, cmd::ToBody{ 4 },
        cmd::ToBelt{ 7 }, cmd::OpenTrade{ 3, true }, cmd::OpenTrade{ 3, false }, cmd::OpenHire{ 8 }, cmd::Buy{ 2 }, cmd::Buy{ 5, 3 },
        cmd::Sell{ 55 }, cmd::Repair{ -1 }, cmd::Identify{}, cmd::IdentifyWith{ 7, 12 }, cmd::Hire{ 1 }, cmd::CloseTrade{}, cmd::Run{ true }, cmd::Run{ false },
        cmd::Chat{ 4 }, cmd::Chat{ -1 }, cmd::QuestMessage{ 3, 76 }, cmd::Respec{ 2 }, cmd::Waypoint{ 5, 3 }, cmd::Caravan{ 6 }, cmd::Imbue{ 7 }, cmd::SwapWeapons{} };
    for (const auto& command : all) {
        const auto bytes = encode(command);
        const auto back = decode(bytes);
        assert(back && back->index() == command.index());
        assert(encode(*back) == bytes);                      // same fields: the same bytes
    }
    // A charged entry's cast keeps its item.
    {
        const auto back = decode(encode(cmd::UseSkill{ 54, 3.f, 4.f, -1, false, 812 }));
        const auto* use = back ? std::get_if<cmd::UseSkill>(&*back) : nullptr;
        assert(use && use->skill == 54 && use->item == 812 && !use->left);
    }
    // Through the transport, in order.
    LocalTransport transport;
    for (const auto& command : all) transport.send(command);
    const auto got = transport.receive();
    assert(got.size() == all.size() && transport.receive().empty());
    for (std::size_t i = 0; i < all.size(); ++i) assert(got[i].index() == all[i].index());
    // Malformed: empty, unknown id, short, trailing bytes.
    auto message = encode(cmd::Pickup{ 1 });
    assert(!decode({}) && !decode(std::vector<std::uint8_t>{ 0xee }));
    assert(!decode(std::span(message).first(message.size() - 1)));
    message.push_back(0);
    assert(!decode(message));
    std::puts("ok");
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
