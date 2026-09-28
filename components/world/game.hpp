// The World's base: the data components, logging, the standard library
// and the game's iso geometry. Nothing here draws or plays sound; the
// client's common.hpp adds that on top.
#pragma once

#include <mpq.hpp>
#include <cof.hpp>
#include <compcode.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <maze.hpp>
#include <outdoor_data.hpp>
#include <dt1.hpp>
#include <tbl.hpp>
#include <txt.hpp>
#include <rules.hpp>
#include <monsters.hpp>
#include <combat.hpp>
#include <drops.hpp>
#include <shrines.hpp>
#include <quests.hpp>
#include <light.hpp>
#include <weather.hpp>
#include <skills.hpp>
#include <sequences.hpp>
#include <userdir.hpp>
#include <obj_preset.hpp>

#include "log.hpp"
#include "npc_menu.hpp"
#include "npc_talk.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace d2d::app {

// D2 iso-diamond tile dimensions. Each cell footprint = 160x80; each
// step in x moves (+80, +40) on screen, each step in y moves (-80, +40).
// See OpenDiablo2's mapengine for the same convention.
constexpr int kIsoW = 160;
constexpr int kIsoH = 80;

}  // namespace d2d::app
