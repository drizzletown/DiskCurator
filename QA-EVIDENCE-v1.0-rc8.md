# rc8 Quality evidence — 2026-11-18

Environment: container Linux, C++17 core and CLI. Windows MSVC/GUI/NTFS NOT RUN.

| Check | Outcome |
|---|---|
| CMake Release CTest all cases | PASS 8/8 |
| Clang strict warnings Debug selected regression | PASS 6/6 |
| Clang ASan/UBSan Debug selected regression | PASS 6/6 |
| Manual selected duplicates & hardlink safety | PASS Linux test_foundation.cpp |
| Arbitrary directory pair / empty dir / no-denominator metrics | PASS Linux test_foundation.cpp |
| Nested overlapping dirs / truncated discovery | PASS Linux test_foundation.cpp |
| Windows MSVC rc8 build and GUI smoke | NOT RUN |
| Windows Win32 manual multi-selection & workflow | NOT RUN |
| NTFS/link/junction, USB disconnect, realistic photos | NOT RUN |

Uncompleted items do not meet v1.0 final release gate.
