# components/

Shared libraries consumed by anything under `apps/` or `tests/`. One
library per subdirectory, each with its own `CMakeLists.txt`.

- `iso9660/` — header-only ISO 9660 + Joliet reader. INTERFACE target
  `iso9660`. No deps.

Convention: keep components dependency-light. If a component wants a heavy
third-party lib, guard it behind an option so consumers can opt out.
