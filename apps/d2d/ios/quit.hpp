// SPDX-License-Identifier: GPL-3.0-or-later
// End the iOS host process after the game loop returns (SDL_Quit is not enough).
#pragma once

namespace d2d::ios {

void request_app_termination();

}  // namespace d2d::ios