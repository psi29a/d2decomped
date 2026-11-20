// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of object_sounds.hpp's functions; their notes are in the header.
#include "object_sounds.hpp"

#include "object_ids.hpp"
#include "sound_ids.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace d2d::rules {

const ObjectSounds* object_sounds(int id) {
    const auto found = std::ranges::lower_bound(kObjectSounds, id, {}, &ObjectSounds::id);
    return found != std::end(kObjectSounds) && found->id == id ? &*found : nullptr;
}

int object_sound(int id, int mode, int from) {
    const auto* sounds = object_sounds(id);
    if (!sounds || mode < 0 || mode > 7 || (sounds->shared && from > mode)) return 0;
    return sounds->modes[std::size_t(mode)];
}

int object_loop(int id, int mode) {
    if (mode < 0 || mode > 7) return 0;
    if (id == 0x3d) return kObject61Loops[std::size_t(mode)];
    const auto* sounds = object_sounds(id);
    if (!sounds || (sounds->loops[0] == 0 && sounds->loops[2] == 0)) return 0;
    if (sounds->loops[1] == mode || sounds->loops[1] == 8) return sounds->loops[0];
    if (sounds->loops[3] == mode || sounds->loops[3] == 8) return sounds->loops[2];
    return 0;
}

int object_call(int id, int mode, int range, bool played) {
    return id == object_ids::kGibbet && mode == 0 && range < kCallRange && !played ? sound_ids::kCainHelp : 0;
}

}
