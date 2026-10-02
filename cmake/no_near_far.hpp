// Force-included off Windows (CMakeLists.txt). windows.h's minwindef.h
// #defines near and far to nothing, so `int near = 0;` builds on macOS and
// Linux and breaks only on Windows. Here either name is a compile error on
// every platform, with this message.
#pragma once
#define near _Pragma("GCC error \"'near' is a windows.h macro (minwindef.h): pick another name\"")
#define far _Pragma("GCC error \"'far' is a windows.h macro (minwindef.h): pick another name\"")
