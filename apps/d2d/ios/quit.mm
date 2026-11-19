// SPDX-License-Identifier: GPL-3.0-or-later
#include "quit.hpp"

#include <cstdlib>

namespace d2d::ios {

void request_app_termination() {
    // UIKit keeps the SDL host alive when main returns; the user chose to leave.
    std::exit(0);
}

}  // namespace d2d::ios