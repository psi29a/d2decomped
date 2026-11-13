# vcpkg overlay ports

CI's Windows legs install their vcpkg ports with
`--overlay-ports=tools/vcpkg-ports` (`.github/workflows/ci.yml`, "Download
prebuilt dependencies"), so a port here replaces vcpkg's own of that name.
Only the ARM64 leg installs `openal-soft` (x64 takes upstream's prebuilt zip).

## openal-soft

vcpkg's port of 1.25.2 as it was on microsoft/vcpkg master on 2026-11-13
(`ports/openal-soft`, last changed by 398e9a71; MIT), plus one patch:

- `005-msvc-14.52-dereference.patch`: upstream commit
  7f9c6de1162b1d4604b9a2f8bcd6c53021566221 (kcat/openal-soft, LGPL-2.0-or-later),
  unchanged. MSVC 14.52's STL no longer lets `&std::unique_ptr<T>::operator*`
  be used as a ranges projection (kcat/openal-soft#1303), so 1.25.2 stops at
  `alc/alc.cpp(1783)`: error C3889. The `windows-11-arm` image moved from MSVC
  14.51 to 14.52 with 20261112.217, and the leg failed in every run that
  got that image.

It applies cleanly on 1.25.2 after the port's other four patches, and leaves
no `::operator*)` projection in the tree. Drop this directory once vcpkg
ships an openal-soft with the fix.
