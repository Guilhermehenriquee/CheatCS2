// muei 無影 - logging
//
// Single file sink next to the executable. Every line is timestamped, flushed
// immediately and prefixed with the thread id, because the overlay writes from
// the render thread while attach and feature ticks can run elsewhere.
//
// Nothing here throws: logging is diagnostic, it must never be the reason the
// process dies.
#pragma once

#include <windows.h>
#include <string>

namespace logx {

// Resolve <exe-stem>_log.txt next to the running image and truncate it, then
// emit a banner. Safe to call more than once; only the first call re-opens.
void init(const char* exeStem = "muei");

// printf-style. Never called before init() from another thread: init happens
// in wWinMain before any thread is spawned.
void line(const char* fmt, ...);

// Log a Win32 error with the system message appended.
void win32(const char* what, DWORD code);

// Log a wide string (usually IMemory::lastError) as UTF-8.
void wide(const char* what, const std::wstring& text);

// Build "what (error N: message)" the same way ProcUser used to, but free of
// any process state so any backend can use it.
std::wstring formatWin32(const wchar_t* what, DWORD code);

// Convert UTF-16 to UTF-8, replacing what cannot be converted.
std::string narrow(const std::wstring& wide);

// Absolute path of the active log file (empty before init, or when no sink
// could be opened anywhere).
const wchar_t* path();

// Win32 error from the first sink that failed, 0 when init succeeded on the
// first try. Useful when the expected location is not writable.
DWORD initError();

} // namespace logx
