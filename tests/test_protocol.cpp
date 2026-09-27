// The command codec (apps/d2d/protocol.hpp): every command survives its
// wire form, and malformed messages are refused.
#include "protocol.hpp"

#include <cassert>
#include <cstdio>

int main() {
    const std::vector<Command> all{
        cmd::Move{ 12.5f, 3.25f, true }, cmd::UseSkill{ 36, 1.f, 2.f, 77, true }, cmd::UseSkill{ 0, 4.f, 5.f, -1, false },
        cmd::Interact{ 9 }, cmd::Pickup{ 41 }, cmd::Resurrect{}, cmd::StatPoint{ 3, 5 }, cmd::SkillPoint{ 12 },
        cmd::SelectSkill{ 98, false }, cmd::UseBelt{ 2 }, cmd::ToCursor{ 17 }, cmd::ToGrid{ 5, -1, 3 }, cmd::ToBody{ 4 },
        cmd::ToBelt{ 7 }, cmd::OpenTrade{ 3, true }, cmd::OpenTrade{ 3, false }, cmd::OpenHire{ 8 }, cmd::Buy{ 2 },
        cmd::Sell{ 55 }, cmd::Repair{ -1 }, cmd::Identify{}, cmd::Hire{ 1 }, cmd::CloseTrade{}, cmd::Run{ true }, cmd::Run{ false },
        cmd::Chat{ 4 }, cmd::Chat{ -1 }, cmd::QuestMessage{ 3, 76 } };
    for (const auto& c : all) {
        const auto b = encode(c);
        const auto back = decode(b);
        assert(back && back->index() == c.index());
        assert(encode(*back) == b);                      // same fields: the same bytes
    }
    // Through the transport, in order.
    LocalTransport t;
    for (const auto& c : all) t.send(c);
    const auto got = t.receive();
    assert(got.size() == all.size() && t.receive().empty());
    for (std::size_t i = 0; i < all.size(); ++i) assert(got[i].index() == all[i].index());
    // Malformed: empty, unknown id, short, trailing bytes.
    auto m = encode(cmd::Pickup{ 1 });
    assert(!decode({}) && !decode(std::vector<std::uint8_t>{ 0xee }));
    assert(!decode(std::span(m).first(m.size() - 1)));
    m.push_back(0);
    assert(!decode(m));
    std::puts("ok");
}
