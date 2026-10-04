// SPDX-License-Identifier: GPL-3.0-or-later
// The client's view of the game (components/game): its headers, and
// each game name the client uses, brought into d2d::client one by one.
// A new name the client needs goes on this list.
#pragma once

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <fight.hpp>
#include <game.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <inventory.hpp>
#include <item_text.hpp>
#include <loot.hpp>
#include <npc_menu.hpp>
#include <npc_talk.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <filesystem>   // IWYU pragma: keep (namespace fs)

namespace d2d::client {

namespace fs = std::filesystem;
namespace cmd = game::cmd;
namespace ev = game::ev;
namespace wire = game::wire;

using game::Character;
using game::CharacterStore;
using game::Command;
using game::Cues;
using game::Event;
using game::Fight;
using game::GameData;
using game::Level;
using game::LocalTransport;
using game::Loot;
using game::Monster;
using game::Npc;
using game::NpcMenu;
using game::NpcTalk;
using game::PanelStats;
using game::Store;
using game::TextLine;
using game::UnitState;
using game::View;
using game::ViewEncoder;
using game::World;
using game::attack_mode;
using game::cof_direction;
using game::direction16;
using game::ds1_path_to_mpq;
using game::id_rows;
using game::kCharCode;
using game::kIsoH;
using game::kIsoW;
using game::kLayerCode;
using game::kModeCode;
using game::kModeDD;
using game::kModeDT;
using game::kModeNU;
using game::kModeRN;
using game::kModeTN;
using game::kModeTW;
using game::kModeWL;
using game::kNpcMenus;
using game::kNpcTalk;
using game::kTickMs;
using game::make_monster;
using game::kTxtBlue;
using game::kTxtGrey;
using game::kTxtWhite;
using game::kVariant;
using game::level_light;
using game::player_cof;
using game::self_cast;
using game::skill_built;
using game::split_variants;
using game::tile_key;
using game::u16_to_latin1;

}  // namespace d2d::client
