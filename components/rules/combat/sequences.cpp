// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of sequences.hpp's functions; their notes are in the header.
#include "sequences.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace d2d::rules {

std::span<const SeqFrame> sequence(int seq, std::string_view wclass) {
    for (std::size_t weapon_class = 0; weapon_class < kSeqWClass.size(); ++weapon_class)
        if (kSeqWClass[weapon_class] == wclass)
            for (const auto& entry : kSeqEntries)
                if (entry.seq == seq && entry.wclass == weapon_class) return { kSeqFrames + entry.first, entry.count };
    return {};
}

}
