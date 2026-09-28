// SDL and OpenAL, for the headers that use them.
#pragma once

#include <SDL3/SDL.h>

#if __has_include(<AL/al.h>)
#  include <AL/al.h>
#  include <AL/alc.h>
#else
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#endif

// windef.h (through SDL / OpenAL on Windows) defines near and far as empty
// macros, and the SDK's later headers need them (FAR): name nothing so.
