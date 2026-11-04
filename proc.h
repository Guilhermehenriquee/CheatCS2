#pragma once
#include <windows.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>

#pragma comment(lib, "psapi.lib")

// -----------------------------------------------------------------------------
// Proc - handle owner for the target process and its modules.
//
//   h   read handle  (PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION)
//   hw  write handle (PROCESS_VM_WRITE | PROCESS_VM_OPERATION), opened on demand
//
// Every read is checked. rd<T>() returns T{} on failure - callers never see
// uninitialised memory. rdOk<T>() is for call sites that must tell "read zero"
// from "read failed".
//
// Module bases are per-module. base_of(L"client.dll") and base_of(L"engine2.dll")
// are different values; never use the process base for module-relative offsets.
// -----------------------------------------------------------------------------

struct ModInfo {
    uintptr_t base = 0;
    size_t    size = 0;
    bool valid() const { return base != 0 && size != 0; }
};

class Proc {
public:
    DWORD     pid  = 0;
    HANDLE    h    = nullptr;   // read
    HANDLE    hw   = nullptr;   // write
    uintptr_t exeBase = 0;      // cs2.exe image base

    Proc() = default;
    Proc(const Proc&) = delete;
    Proc& operator=(const Proc&) = delete;
    ~Proc() { detach(); }

    // ---------------------------------------------------------------- attach --
    // Reports why it failed via lastError_ instead of silently returning false.
    // The overlay surfaces that string in its log; without it every failure looks
    // identical on screen.
    std::wstring lastError_;

    bool attach(const wchar_t* exeName) {
        detach();
        lastError_.clear();

        DWORD foundPid = FindPid(exeName);
        if (!foundPid) {
            lastError_ = L"process not found (Toolhelp snapshot)";
            return false;
        }
        pid = foundPid;

        h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            lastError_ = FormatWin32(L"OpenProcess(VM_READ) denied", GetLastError());
            pid = 0;
            return false;
        }

        ModInfo exe = FindModule(exeName);
        exeBase = exe.base;
        if (!exeBase) {
            // A read handle exists but the module list was refused - almost always
            // an integrity-level mismatch (game elevated, overlay not).
            lastError_ = FormatWin32(L"EnumProcessModules denied", GetLastError());
            return false;
        }

        lastError_.clear();
        return true;
    }

    // Attach to a specific PID. Used when the PID came from window ownership
    // rather than a name search - some launchers hide the process from a plain
    // snapshot but leave the window enumerable.
    bool attachPid(DWORD foundPid) {
        detach();
        lastError_.clear();
        if (!foundPid) {
            lastError_ = L"no pid supplied";
            return false;
        }
        pid = foundPid;

        h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            lastError_ = FormatWin32(L"OpenProcess(VM_READ) denied", GetLastError());
            pid = 0;
            return false;
        }

        // The image name is not known here; resolve the main module directly.
        ModInfo main = FindModuleByPid(pid, nullptr);
        exeBase = main.base;
        if (!exeBase) {
            lastError_ = FormatWin32(L"main module lookup denied", GetLastError());
            return false;
        }
        return true;
    }

    const std::wstring& lastError() const { return lastError_; }

    static std::wstring FormatWin32(const wchar_t* what, DWORD code) {
        wchar_t msg[256]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, msg, 256, nullptr);
        wchar_t buf[512]{};
        swprintf_s(buf, L"%s (error %lu: %s)", what, code, msg);
        // Trim the trailing newline FormatMessage appends.
        size_t n = wcslen(buf);
        while (n > 0 && (buf[n - 1] == L'\n' || buf[n - 1] == L'\r')) buf[--n] = L'\0';
        return buf;
    }

    void detach() {
        dropWrite();
        if (h) { CloseHandle(h); h = nullptr; }
        pid = 0;
        exeBase = 0;
        cache_.clear();
    }

    bool alive() const {
        if (!pid) return false;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!p) return false;
        DWORD code = 0;
        bool ok = GetExitCodeProcess(p, &code) && code == STILL_ACTIVE;
        CloseHandle(p);
        return ok;
    }

    // ----------------------------------------------------------------- write --
    bool ensureWrite() {
        if (hw) return true;
        if (!pid) return false;
        hw = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_VM_READ
                         | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        return hw != nullptr;
    }

    void dropWrite() {
        if (hw) { CloseHandle(hw); hw = nullptr; }
    }

    template <typename T>
    bool wr(uintptr_t a, const T& val) const {
        if (!hw || !a) return false;
        SIZE_T written = 0;
        return WriteProcessMemory(hw, (LPVOID)a, &val, sizeof(T), &written)
               && written == sizeof(T);
    }

    bool wrBytes(uintptr_t a, const void* data, size_t len) const {
        if (!hw || !a || !data || !len) return false;
        SIZE_T written = 0;
        return WriteProcessMemory(hw, (LPVOID)a, data, len, &written) && written == len;
    }

    // ------------------------------------------------------------------ read --
    bool rdBytes(uintptr_t a, void* out, size_t len) const {
        if (!h || !a || !out || !len) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(h, (LPCVOID)a, out, len, &got) && got == len;
    }

    template <typename T>
    inline T rd(uintptr_t a) const {
        T v{};
        if (!h || !a) return v;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (LPCVOID)a, &v, sizeof(T), &got) || got != sizeof(T)) {
            return T{};
        }
        return v;
    }

    template <typename T>
    inline bool rdOk(uintptr_t a, T& out) const {
        if (!h || !a) return false;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (LPCVOID)a, &out, sizeof(T), &got) || got != sizeof(T)) {
            return false;
        }
        return true;
    }

    // Pointer-chasing helper: deref(root, {a, b}) == *(*(root + a) + b)
    inline uintptr_t deref(uintptr_t root, std::initializer_list<uintptr_t> chain) const {
        uintptr_t p = root;
        for (uintptr_t off : chain) {
            if (!p) return 0;
            p = rd<uintptr_t>(p + off);
        }
        return p;
    }

    static bool plausible(uintptr_t p) { return p > 0x10000ull; }

    // ---------------------------------------------------------------- modules --
    ModInfo base_of(const wchar_t* module) {
        for (auto& e : cache_) {
            if (!_wcsicmp(e.name.c_str(), module)) return e.info;
        }
        ModInfo mi = FindModule(module);
        cache_.push_back({ module, mi });
        return mi;
    }

    // Address of a module-relative offset, or 0 if the module is missing or the
    // offset lands outside it (stale offset signature).
    uintptr_t at(const wchar_t* module, uintptr_t rva) {
        ModInfo mi = base_of(module);
        if (!mi.valid()) return 0;
        if (rva >= mi.size) return 0;
        return mi.base + rva;
    }

    void invalidateCache() { cache_.clear(); }

private:
    struct Entry { std::wstring name; ModInfo info; };
    std::vector<Entry> cache_;

    static DWORD FindPid(const wchar_t* exeName) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        DWORD found = 0;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (!_wcsicmp(pe.szExeFile, exeName)) { found = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    // Main module of a PID. When moduleName is null the first enumerated module
    // is the image itself, which is what attachPid needs.
    static ModInfo FindModuleByPid(DWORD target, const wchar_t* moduleName) {
        if (!target) return {};
        ModInfo out{};
        HMODULE mods[1024];
        DWORD needed = 0;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, target);
        if (!p) return {};
        if (EnumProcessModulesEx(p, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
            DWORD count = needed / sizeof(HMODULE);
            if (count > 1024) count = 1024;
            for (DWORD i = 0; i < count; ++i) {
                if (moduleName) {
                    wchar_t name[MAX_PATH]{};
                    if (!GetModuleBaseNameW(p, mods[i], name, MAX_PATH)) continue;
                    if (_wcsicmp(name, moduleName) != 0) continue;
                }
                MODULEINFO mi{};
                if (GetModuleInformation(p, mods[i], &mi, sizeof(mi))) {
                    out.base = (uintptr_t)mi.lpBaseOfDll;
                    out.size = (size_t)mi.SizeOfImage;
                }
                break;
            }
        }
        SetLastError(GetLastError());
        CloseHandle(p);
        return out;
    }

    ModInfo FindModule(const wchar_t* module) const {
        return FindModuleByPid(pid, module);
    }
};
