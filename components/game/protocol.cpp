// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for protocol.hpp: a command's wire form and back. Out of the
// header: their visit over every command was ~1.5 s of parsing for each of
// the ~290 files that include world.hpp.
#include "protocol.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace d2d::game {

// A command's bytes: its opcode, then its fields in order (UseSkill ends with
// its stand byte and its target's unit type, 0 a player, 1 a monster).
std::vector<std::uint8_t> encode(const Command& command) {
    wire::Out out;
    std::visit([&](const auto& message) {
        using T = std::decay_t<decltype(message)>;
        if constexpr (std::is_same_v<T, cmd::Move>) out.u8(opcode::kMove).f32(message.x).f32(message.y).u8(message.fresh);
        else if constexpr (std::is_same_v<T, cmd::UseSkill>) out.u8(message.left ? opcode::kLeftSkill : opcode::kRightSkill).i32(message.skill).f32(message.x).f32(message.y).i32(message.unit).i32(message.item).u8(message.stand ? 1 : 0).u8(message.unit_type);
        else if constexpr (std::is_same_v<T, cmd::Interact>) out.u8(opcode::kInteract).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Pickup>) out.u8(opcode::kPickup).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Resurrect>) out.u8(opcode::kResurrect);
        else if constexpr (std::is_same_v<T, cmd::SwapWeapons>) out.u8(opcode::kSwapWeapons);
        else if constexpr (std::is_same_v<T, cmd::MercItem>) out.u8(opcode::kMercItem).i32(message.slot);
        else if constexpr (std::is_same_v<T, cmd::Transmute>) out.u8(opcode::kButton).u16(0x18);
        else if constexpr (std::is_same_v<T, cmd::DropGold>) out.u8(opcode::kDropGold).i32(message.amount);
        else if constexpr (std::is_same_v<T, cmd::StashGold>) out.u8(opcode::kButton).u16(message.deposit ? 0x14 : 0x13).i32(message.amount);
        else if constexpr (std::is_same_v<T, cmd::StatPoint>) out.u8(opcode::kStatPoint).i32(message.stat).i32(message.count);
        else if constexpr (std::is_same_v<T, cmd::SkillPoint>) out.u8(opcode::kSkillPoint).i32(message.skill);
        else if constexpr (std::is_same_v<T, cmd::SelectSkill>) out.u8(opcode::kSelectSkill).i32(message.skill).u8(message.left);
        else if constexpr (std::is_same_v<T, cmd::UseBelt>) out.u8(opcode::kUseBelt).i32(message.slot).u8(message.merc);
        else if constexpr (std::is_same_v<T, cmd::UseItem>) out.u8(opcode::kUseItem).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ReadScroll>) out.u8(opcode::kReadScroll).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ObjectItem>) out.u8(opcode::kObjectItem).i32(message.npc).i32(message.item).u8(message.place ? 3 : 2);
        else if constexpr (std::is_same_v<T, cmd::ToCursor>) out.u8(opcode::kToCursor).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Drop>) out.u8(opcode::kDrop).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ToGrid>) out.u8(opcode::kToGrid).i32(message.panel).i32(message.col).i32(message.row);
        else if constexpr (std::is_same_v<T, cmd::ToBody>) out.u8(opcode::kToBody).i32(message.slot);
        else if constexpr (std::is_same_v<T, cmd::ToBelt>) out.u8(opcode::kToBelt).i32(message.box);
        else if constexpr (std::is_same_v<T, cmd::AutoBelt>) out.u8(opcode::kAutoBelt).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::EquipFromBag>) out.u8(opcode::kEquipFromBag).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::DismissEffect>) out.u8(opcode::kDismissEffect).u8(message.kind).u32(std::uint32_t(message.instance)).u32(std::uint32_t(message.instance >> 32));
        else if constexpr (std::is_same_v<T, cmd::Socket>) out.u8(opcode::kSocket).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ScrollToBook>) out.u8(opcode::kScrollToBook).i32(message.book);
        else if constexpr (std::is_same_v<T, cmd::OpenTrade>) out.u8(opcode::kNpcDeal).u8(message.gamble ? npc_deal::kGamble : npc_deal::kTrade).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::OpenHire>) out.u8(opcode::kNpcDeal).u8(npc_deal::kHire).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Respec>) out.u8(opcode::kNpcDeal).u8(npc_deal::kRespec).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::ResurrectMerc>) out.u8(opcode::kNpcDeal).u8(npc_deal::kResurrectMerc).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Buy>) out.u8(opcode::kBuy).i32(message.item).u8(message.fill);
        else if constexpr (std::is_same_v<T, cmd::Sell>) out.u8(opcode::kSell).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Repair>) out.u8(opcode::kRepair).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Identify>) out.u8(opcode::kIdentify);
        else if constexpr (std::is_same_v<T, cmd::IdentifyWith>) out.u8(opcode::kIdentifyWith).i32(message.scroll).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Hire>) out.u8(opcode::kHire).i32(message.name);
        else if constexpr (std::is_same_v<T, cmd::CloseTrade>) out.u8(opcode::kCloseTrade);
        else if constexpr (std::is_same_v<T, cmd::Run>) out.u8(message.running ? opcode::kRunOn : opcode::kRunOff);
        else if constexpr (std::is_same_v<T, cmd::Chat>) out.u8(opcode::kChat).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Say>) out.u8(opcode::kSay).str(message.text);
        else if constexpr (std::is_same_v<T, cmd::QuestMessage>) out.u8(opcode::kQuestMessage).i32(message.npc).i32(message.string);
        else if constexpr (std::is_same_v<T, cmd::Caravan>) out.u8(opcode::kNpcDeal).u8(npc_deal::kCaravan).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Imbue>) out.u8(opcode::kNpcDeal).u8(npc_deal::kImbue).i32(message.npc).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Waypoint>) out.u8(opcode::kWaypoint).i32(message.npc).i32(message.level);
        else static_assert(!sizeof(T), "a command without a wire form");
    }, command);
    return out.bytes;
}

// encode's inverse: the command, or none for an unknown opcode or short bytes.
std::optional<Command> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    wire::In input{ bytes };
    auto i32 = [&] { return int(input.get<std::int32_t>()); };
    auto f32 = [&] { return input.get<float>(); };
    auto byte = [&] { return int(input.get<std::uint8_t>()); };
    std::optional<Command> command;
    switch (bytes[0]) {
        case opcode::kMove: { const float x = f32(), y = f32(); command = cmd::Move{ x, y, byte() != 0 }; break; }
        case opcode::kLeftSkill: case opcode::kRightSkill: { const int skill = i32(); const float x = f32(), y = f32(); const int unit = i32(); const int item = i32(); const bool stand = byte() != 0; command = cmd::UseSkill{ skill, x, y, unit, bytes[0] == opcode::kLeftSkill, item, stand, byte() }; break; }
        case opcode::kInteract: command = cmd::Interact{ i32() }; break;
        case opcode::kPickup: command = cmd::Pickup{ i32() }; break;
        case opcode::kResurrect: command = cmd::Resurrect{}; break;
        case opcode::kSwapWeapons: command = cmd::SwapWeapons{}; break;
        case opcode::kMercItem: command = cmd::MercItem{ i32() }; break;
        case opcode::kButton: {
            const auto button = input.get<std::uint16_t>();
            if (button == 0x18) command = cmd::Transmute{};
            else if (button == 0x13 || button == 0x14) command = cmd::StashGold{ i32(), button == 0x14 };
            else return std::nullopt;
            break;
        }
        case opcode::kStatPoint: { const int stat = i32(); command = cmd::StatPoint{ stat, i32() }; break; }
        case opcode::kSkillPoint: command = cmd::SkillPoint{ i32() }; break;
        case opcode::kSelectSkill: { const int skill = i32(); command = cmd::SelectSkill{ skill, byte() != 0 }; break; }
        case opcode::kUseBelt: { const int slot = i32(); command = cmd::UseBelt{ slot, byte() != 0 }; break; }
        case opcode::kUseItem: command = cmd::UseItem{ i32() }; break;
        case opcode::kReadScroll: command = cmd::ReadScroll{ i32() }; break;
        case opcode::kObjectItem: { const int npc = i32(), item = i32(); command = cmd::ObjectItem{ npc, item, byte() == 3 }; break; }
        case opcode::kToCursor: command = cmd::ToCursor{ i32() }; break;
        case opcode::kDrop: command = cmd::Drop{ i32() }; break;
        case opcode::kToGrid: { const int panel = i32(), col = i32(); command = cmd::ToGrid{ panel, col, i32() }; break; }
        case opcode::kToBody: command = cmd::ToBody{ i32() }; break;
        case opcode::kToBelt: command = cmd::ToBelt{ i32() }; break;
        case opcode::kAutoBelt: command = cmd::AutoBelt{ i32() }; break;
        case opcode::kEquipFromBag: command = cmd::EquipFromBag{ i32() }; break;
        case opcode::kDismissEffect: { const int kind = byte(); if (kind > 1) return std::nullopt; const auto low = input.get<std::uint32_t>(); const auto high = input.get<std::uint32_t>(); command = cmd::DismissEffect{ kind, std::uint64_t(low) | (std::uint64_t(high) << 32) }; break; }
        case opcode::kDropGold: command = cmd::DropGold{ i32() }; break;
        case opcode::kSocket: command = cmd::Socket{ i32() }; break;
        case opcode::kScrollToBook: command = cmd::ScrollToBook{ i32() }; break;
        case opcode::kNpcDeal: {
            const int kind = byte(), npc = i32();
            // Assigned per arm. A ternary of Command temporaries makes GCC 15
            // warn that the variant is used uninitialized (-Werror).
            if (kind == npc_deal::kImbue) command = cmd::Imbue{ npc, i32() };
            else if (kind == npc_deal::kCaravan) command = cmd::Caravan{ npc };
            else if (kind == npc_deal::kRespec) command = cmd::Respec{ npc };
            else if (kind == npc_deal::kResurrectMerc) command = cmd::ResurrectMerc{ npc };
            else if (kind == npc_deal::kHire) command = cmd::OpenHire{ npc };
            else command = cmd::OpenTrade{ npc, kind == npc_deal::kGamble };
            break;
        }
        case opcode::kBuy: { const int item = i32(); command = cmd::Buy{ item, byte() != 0 }; break; }
        case opcode::kSell: command = cmd::Sell{ i32() }; break;
        case opcode::kRepair: command = cmd::Repair{ i32() }; break;
        case opcode::kIdentify: command = cmd::Identify{}; break;
        case opcode::kIdentifyWith: { const int scroll = i32(); command = cmd::IdentifyWith{ scroll, i32() }; break; }
        case opcode::kHire: command = cmd::Hire{ i32() }; break;
        case opcode::kCloseTrade: command = cmd::CloseTrade{}; break;
        case opcode::kRunOn: case opcode::kRunOff: command = cmd::Run{ bytes[0] == 0x53 }; break;
        case opcode::kChat: command = cmd::Chat{ i32() }; break;
        case opcode::kSay: command = cmd::Say{ input.str() }; break;
        case opcode::kQuestMessage: { const int quest = i32(); command = cmd::QuestMessage{ quest, i32() }; break; }
        case opcode::kWaypoint: { const int npc = i32(); command = cmd::Waypoint{ npc, i32() }; break; }
        default: return std::nullopt;
    }
    if (!input.ok || input.offset != bytes.size()) return std::nullopt;
    return command;
}

std::vector<Command> LocalTransport::receive() {
    std::vector<Command> out;
    for (const auto& message : to_server) if (auto command = decode(message)) out.push_back(*command);
    to_server.clear();
    return out;
}

}  // namespace d2d::game
