// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of light.hpp's functions; their notes are in the header.
#include "light.hpp"

namespace d2d::rules {

int light_radius8(int quality, int type, bool own, int radius8) {
    if (quality != 0 || radius8 <= 0) return radius8;              // 0: not stamped at all (radius - 1 over 0xfe)
    if (type == 0 && !own && radius8 > 15) return 16;
    if (type == 1) return 8;
    return type == 3 ? 0 : radius8;
}

}
