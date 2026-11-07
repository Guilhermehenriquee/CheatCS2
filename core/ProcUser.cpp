#include "ProcUser.h"
#include "Log.h"

#include <psapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "psapi.lib")

void ProcUser::setError(const wchar_t* what, DWORD code) {
    lastError_ = logx::formatWin32(what, code);
    logx::wide("ProcUser", lastError_);
}

DWORD ProcUser::findPid(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, exeName)) {
                found = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

ModInfo ProcUser::findModuleByPid(DWORD target, const wchar_t* moduleName) {
    ModInfo out{};
    if (!target) return out;

    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                           FALSE, target);
    if (!p) return out;

    HMODULE mods[1024]{};
    DWORD needed = 0;
    if (EnumProcessModulesEx(p, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        DWORD count = needed / (DWORD)sizeof(HMODULE);
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
    CloseHandle(p);
    return out;
}

bool ProcUser::attach(const wchar_t* process) {
    detach();
    lastError_.clear();

    DWORD found = findPid(process);
    if (!found) {
        lastError_ = L"process not found (Toolhelp snapshot)";
        return false;
    }
    return attachPid(found);
}

bool ProcUser::attachPid(DWORD targetPid) {
    detach();
    lastError_.clear();

    if (!targetPid) {
        lastError_ = L"no pid supplied";
        return false;
    }

    read_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE, targetPid);
    if (!read_) {
        setError(L"OpenProcess(VM_READ) denied", GetLastError());
        return false;
    }

    pid_ = targetPid;

    // The image name is unknown on this route; the first enumerated module is
    // the executable itself.
    ModInfo main = findModuleByPid(pid_, nullptr);
    exeBase_ = main.base;
    if (!exeBase_) {
        setError(L"main module lookup denied", GetLastError());
        detach();
        return false;
    }

    logx::line("ProcUser attached pid=%lu image=%p",
               (unsigned long)pid_, (void*)exeBase_);
    return true;
}

void ProcUser::detach() {
    dropWrite();
    if (read_) {
        CloseHandle(read_);
        read_ = nullptr;
    }
    pid_     = 0;
    exeBase_ = 0;
    cache_.clear();
}

bool ProcUser::alive() {
    if (!pid_ || !read_) return false;

    DWORD code = 0;
    if (GetExitCodeProcess(read_, &code) && code == STILL_ACTIVE) return true;

    // A read handle opened with limited query rights can still fail the exit
    // code check on some protected processes; fall back to a live-object probe.
    HANDLE probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid_);
    if (!probe) return false;
    bool ok = GetExitCodeProcess(probe, &code) && code == STILL_ACTIVE;
    CloseHandle(probe);
    return ok;
}

bool ProcUser::canWrite() {
    if (write_) return true;
    if (!pid_) return false;
    write_ = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                         PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
                         FALSE, pid_);
    if (!write_) {
        logx::line("ProcUser write handle denied: %s",
                   logx::narrow(logx::formatWin32(L"OpenProcess(VM_WRITE)", GetLastError())).c_str());
        return false;
    }
    return true;
}

void ProcUser::dropWrite() {
    if (write_) {
        CloseHandle(write_);
        write_ = nullptr;
    }
}

bool ProcUser::read(uintptr_t addr, void* buf, size_t size) {
    if (!read_ || !addr || !buf || !size) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(read_, (LPCVOID)addr, buf, size, &got) && got == size;
}

bool ProcUser::write(uintptr_t addr, const void* buf, size_t size) {
    if (!write_ || !addr || !buf || !size) return false;
    SIZE_T put = 0;
    return WriteProcessMemory(write_, (LPVOID)addr, buf, size, &put) && put == size;
}

ModInfo ProcUser::getModule(const wchar_t* module) {
    if (!module) return {};
    for (auto& e : cache_) {
        if (!_wcsicmp(e.name.c_str(), module)) return e.info;
    }
    ModInfo mi = findModuleByPid(pid_, module);
    cache_.push_back({ module, mi });
    return mi;
}

uintptr_t ProcUser::getModuleBase(const wchar_t* module) {
    return getModule(module).base;
}
