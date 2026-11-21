// muei 無影 - live offset self test.
//
// Compiles into muei.exe and runs behind `--verify`. Attaches to a running
// cs2.exe, proves every offset resolves, writes a PASS/FAIL table to reportPath.
// Kept in the main binary rather than shipped as a second exe: one artefact to
// trust, one fewer unsigned binary for Windows' application control to block.
//
// Returns 0 when every checked field passed.
#pragma once

#include <string>

int runSelfTest(const std::wstring& reportPath);
