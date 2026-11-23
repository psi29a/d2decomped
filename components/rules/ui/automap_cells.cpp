// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of automap_cells.hpp's functions; their notes are in the header.
#include "automap_cells.hpp"

namespace d2d::rules {

int automap_cel_group(int cel) {
    for (const auto& pair : kAutomapCelGroups)
        if (pair[0] == cel) return pair[1];
    return -1;
}

}
