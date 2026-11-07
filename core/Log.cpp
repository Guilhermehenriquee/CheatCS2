#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// Logging deliberately does not use the CRT file API.
//
// The MSVC "_s" functions have templated overloads whose arity differs from the
// plain C declarations MinGW exposes, and getting that wrong silently produced
// "no log file" with no error anywhere. CreateFileW/WriteFile behave identically
// on every toolchain, and the payload is written as UTF-8 bytes, so a non-ASCII
// player name in a log line cannot corrupt the file either.
//
// The sink is chosen at init with a fallback chain, because the directory the
// exe lives in is not always writable: a read-only install, a protected folder,
// a copy run straight out of a zip, or a policy-blocked location all fail the
// first CreateFileW. Falling back to %TEMP% keeps diagnostics available instead
// of losing them exactly when they are needed.
namespace logx {
namespace {

CRITICAL_SECTION g_lock;
bool             g_ready = false;
std::wstring     g_path;
DWORD            g_lastInitError = 0;
wchar_t          g_firstTried[MAX_PATH]{};

void ensureLock() {
    static bool once = false;
    if (!once) {
        InitializeCriticalSection(&g_lock);
        once = true;
    }
}

// Append raw bytes. Opens once per call, so a crashed process still has
// everything it managed to say on disk.
void appendBytes(const char* data, size_t len) {
    if (!g_ready || !data || !len) return;

    HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(h, data, (DWORD)len, &written, nullptr);
    CloseHandle(h);
}

// Try one directory. Returns true when the file could be created there.
bool trySink(const std::wstring& dir, const std::wstring& stem) {
    std::wstring path = dir + stem + L"_log.txt";

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // Remember the first failure: that is the location the operator expects.
        if (g_lastInitError == 0) {
            g_lastInitError = GetLastError();
            // std::wstring::copy, not wcsncpy_s: the _s family has different
            // arities between MSVC and MinGW and silently produces garbage.
            const size_t n = path.copy(g_firstTried, MAX_PATH - 1);
            g_firstTried[n] = L'\0';
        }
        return false;
    }
    CloseHandle(h);
    g_path = path;
    return true;
}

std::wstring exeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    const size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.erase(slash + 1);
    else p.clear();
    return p;
}

std::wstring exeStem() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    const size_t slash = p.find_last_of(L"\\/");
    const size_t dot   = p.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        p.erase(dot);
    }
    if (slash != std::wstring::npos) p.erase(0, slash + 1);
    return p;
}

std::wstring tempDir() {
    wchar_t tmp[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (!n) return {};
    std::wstring p(tmp);
    if (!p.empty() && p.back() != L'\\') p.push_back(L'\\');
    return p;
}

} // namespace

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring formatWin32(const wchar_t* what, DWORD code) {
    wchar_t msg[512]{};
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, msg, 512, nullptr);
    if (n) {
        while (n > 0 && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' ||
                         msg[n - 1] == L' ')) {
            msg[--n] = L'\0';
        }
    } else {
        msg[0] = L'?';
        msg[1] = L'\0';
    }

    std::wstring out;
    if (what && *what) {
        out += what;
        out += L" ";
    }
    out += L"(error ";
    out += std::to_wstring((unsigned long)code);
    out += L": ";
    out += msg;
    out += L")";
    return out;
}

void init(const char* exeStemName) {
    ensureLock();
    EnterCriticalSection(&g_lock);

    if (!g_ready) {
        const std::wstring stem = exeStem();

        if (!trySink(exeDir(), stem) &&
            !trySink(tempDir(), stem) &&
            !trySink(L".\\", stem)) {
            // Nowhere writable. Still surface the reason through the debugger
            // channel so a failure in the field is not completely silent.
            std::wstring msg = L"muei: logger could not open any sink, first tried ";
            msg += g_firstTried;
            msg += L" (";
            msg += std::to_wstring((unsigned long)g_lastInitError);
            msg += L")";
            OutputDebugStringW(msg.c_str());
        } else {
            g_ready = true;
        }
    }

    LeaveCriticalSection(&g_lock);

    if (!g_ready) return;

    line("=== %s start (pid %lu) ===",
         exeStemName ? exeStemName : "muei", (unsigned long)GetCurrentProcessId());
}

const wchar_t* path() { return g_path.c_str(); }

DWORD initError() { return g_lastInitError; }

void line(const char* fmt, ...) {
    if (!g_ready || !fmt) return;

    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(body)) n = (int)sizeof(body) - 1;

    SYSTEMTIME st{};
    GetLocalTime(&st);

    char prefix[64];
    int p = snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d.%03d][t%04lu] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                     (unsigned long)(GetCurrentThreadId() & 0xFFFF));
    if (p < 0) p = 0;

    EnterCriticalSection(&g_lock);

    appendBytes(prefix, (size_t)p);
    appendBytes(body, (size_t)n);
    appendBytes("\r\n", 2);

    LeaveCriticalSection(&g_lock);
}

void win32(const char* what, DWORD code) {
    std::wstring w = formatWin32(L"", code);
    line("%s: %s", what ? what : "win32", narrow(w).c_str());
}

void wide(const char* what, const std::wstring& text) {
    line("%s: %s", what ? what : "error", narrow(text).c_str());
}

} // namespace logx
