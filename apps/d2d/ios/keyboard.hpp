// SPDX-License-Identifier: GPL-3.0-or-later
// Keep the SDL view on the full window while the keyboard is up. UIKit stays in keyboard.mm.
#pragma once

namespace d2d::ios {

// Watch keyboard show and hide. Put the SDL view back on the window bounds,
// give SDL's hidden text field a 1-point frame, and make it first responder.
void present_text_keyboard();

// Resign the text field so the software keyboard closes. Does not move the view.
void dismiss_text_keyboard();

}  // namespace d2d::ios
