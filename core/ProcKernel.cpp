#include "ProcKernel.h"
#include "Log.h"

#include <tlhelp32.h>

namespace {

constexpr const wchar_t* kChannelKey = L"SOFTWARE\\Microsoft\\Clipboard";
constexpr const char*    kChannelVal = "DisableAntiSpyware";

// The driver rejects oversized transfers; anything bigger is split so a
// view-matrix read (64 bytes) and a bone-array sweep (kilobytes) both work.
constexpr size_t kMaxPerCall = 0x1000;

std::string toAscii(const wchar_t* w) {
    if (!w) return {};
    char buf[256]{};
    WideCharToMultiByte(CP_ACP, 0, w, -1, buf, (int)sizeof(buf) - 1, nullptr, nullptr);
    return buf;
}

} // namespace

void ProcKernel::setError(const wchar_t* what, DWORD code) {
    lastError_ = logx::formatWin32(what, code);
    logx::wide("ProcKernel", lastError_);
}

bool ProcKernel::isSuccess(NTSTATUS s) const {
    // STATUS_OK is the documented success code. Some driver variants return the
    // registry call's own success (0) instead; accept both and log anything
    // else so a mismatched variant is visible rather than silent.
    return s == aimstar::STATUS_OK || s == 0;
}

bool ProcKernel::openChannel() {
    if (hKey_) return true;

    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kChannelKey, 0,
                               KEY_ALL_ACCESS, &hKey_);
    if (st != ERROR_SUCCESS) {
        hKey_ = nullptr;
        lastError_ = logx::formatWin32(L"driver channel missing (RegOpenKeyEx)", (DWORD)st);
        return false;
    }
    return true;
}

NTSTATUS ProcKernel::call(DWORD command, void* data, DWORD size) {
    if (!hKey_ && !openChannel()) return (NTSTATUS)aimstar::STATUS_FAIL;

    LSTATUS st = RegSetValueExA(hKey_, kChannelVal, 0, command,
                                reinterpret_cast<const BYTE*>(data), size);
    return (NTSTATUS)st;
}

NTSTATUS ProcKernel::probe() {
    if (!hKey_ && !openChannel()) return (NTSTATUS)aimstar::STATUS_FAIL;
    return call(aimstar::CMD_PING, nullptr, 0);
}

bool ProcKernel::sendMouse(const aimstar::MouseInputData& data) {
    if (!hKey_) return false;
    aimstar::MouseInputData copy = data;
    NTSTATUS s = call(aimstar::CMD_MOUSE, &copy, (DWORD)sizeof(copy));
    return isSuccess(s);
}

bool ProcKernel::attach(const wchar_t* process) {
    detach();
    lastError_.clear();

    if (!openChannel()) {
        logx::line("ProcKernel: no driver channel, falling back");
        return false;
    }

    NTSTATUS s = probe();
    if (!isSuccess(s)) {
        lastError_.resize(0);
        wchar_t buf[128]{};
        swprintf_s(buf, L"driver channel present but ping returned 0x%08lX",
                   (unsigned long)s);
        lastError_ = buf;
        logx::wide("ProcKernel", lastError_);
        return false;
    }

    // Direct name search: the driver only needs a pid, but a snapshot is the
    // cheapest way to get one without opening the process at all.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        setError(L"Toolhelp snapshot failed", GetLastError());
        return false;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, process)) {
                found = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (!found) {
        lastError_ = L"process not found (Toolhelp snapshot)";
        return false;
    }
    return attachPid(found);
}

bool ProcKernel::attachPid(DWORD targetPid) {
    if (!hKey_ && !openChannel()) return false;
    if (!targetPid) {
        lastError_ = L"no pid supplied";
        return false;
    }

    pid_ = targetPid;

    aimstar::BaseReq req{};
    ULONG64 base = 0;
    req.hProcessId = pid_;
    req.OutBuffer  = (ULONG64)(uintptr_t)&base;

    NTSTATUS s = call(aimstar::CMD_BASE_ADDR, &req, (DWORD)sizeof(req));
    if (!isSuccess(s) || !base) {
        wchar_t buf[160]{};
        swprintf_s(buf, L"driver base-address failed status=0x%08lX base=0x%llX",
                   (unsigned long)s, (unsigned long long)base);
        lastError_ = buf;
        logx::wide("ProcKernel", lastError_);
        pid_ = 0;
        return false;
    }

    exeBase_ = (uintptr_t)base;
    logx::line("ProcKernel attached pid=%lu image=%p (driver)",
               (unsigned long)pid_, (void*)exeBase_);
    return true;
}

void ProcKernel::detach() {
    if (hKey_) {
        RegCloseKey(hKey_);
        hKey_ = nullptr;
    }
    pid_     = 0;
    exeBase_ = 0;
    cache_.clear();
}

bool ProcKernel::alive() {
    if (!pid_) return false;

    HANDLE probeH = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid_);
    if (!probeH) {
        // The driver route deliberately leaves the process invisible to us; if
        // the handle is refused we cannot distinguish "dead" from "hidden", so
        // report alive and let a failing read decide.
        return true;
    }
    DWORD code = 0;
    bool ok = GetExitCodeProcess(probeH, &code) && code == STILL_ACTIVE;
    CloseHandle(probeH);
    return ok;
}

bool ProcKernel::read(uintptr_t addr, void* buf, size_t size) {
    if (!hKey_ || !pid_ || !addr || !buf || !size) return false;

    BYTE* out = static_cast<BYTE*>(buf);
    size_t done = 0;
    while (done < size) {
        size_t chunk = size - done;
        if (chunk > kMaxPerCall) chunk = kMaxPerCall;

        aimstar::ReadWriteReq req{};
        req.hProcessId    = pid_;
        req.TargetAddress = (ULONG64)(addr + done);
        req.SourceAddress = (ULONG64)(uintptr_t)(out + done);
        req.NumberOfBytes = (ULONG64)chunk;
        req.ReadWriteType = aimstar::RW_READ;

        NTSTATUS s = call(aimstar::CMD_READ_WRITE, &req, (DWORD)sizeof(req));
        if (!isSuccess(s)) {
            logx::line("ProcKernel read fail addr=0x%llX size=%zu status=0x%08lX",
                       (unsigned long long)(addr + done), chunk, (unsigned long)s);
            return false;
        }
        done += chunk;
    }
    return true;
}

bool ProcKernel::write(uintptr_t addr, const void* buf, size_t size) {
    if (!hKey_ || !pid_ || !addr || !buf || !size) return false;

    const BYTE* in = static_cast<const BYTE*>(buf);
    size_t done = 0;
    while (done < size) {
        size_t chunk = size - done;
        if (chunk > kMaxPerCall) chunk = kMaxPerCall;

        aimstar::ReadWriteReq req{};
        req.hProcessId    = pid_;
        req.TargetAddress = (ULONG64)(addr + done);
        req.SourceAddress = (ULONG64)(uintptr_t)(in + done);
        req.NumberOfBytes = (ULONG64)chunk;
        req.ReadWriteType = aimstar::RW_WRITE;

        NTSTATUS s = call(aimstar::CMD_READ_WRITE, &req, (DWORD)sizeof(req));
        if (!isSuccess(s)) {
            logx::line("ProcKernel write fail addr=0x%llX size=%zu status=0x%08lX",
                       (unsigned long long)(addr + done), chunk, (unsigned long)s);
            return false;
        }
        done += chunk;
    }
    return true;
}

ModInfo ProcKernel::getModule(const wchar_t* module) {
    if (!module || !pid_) return {};

    for (auto& e : cache_) {
        if (!_wcsicmp(e.first.c_str(), module)) return e.second;
    }

    std::string name = toAscii(module);
    ULONG64 base = 0;

    aimstar::ModuleReq req{};
    req.hProcessId = pid_;
    req.ModuleName = (ULONG64)(uintptr_t)name.c_str();
    req.OutBuffer  = (ULONG64)(uintptr_t)&base;

    NTSTATUS s = call(aimstar::CMD_MODULE_ADDR, &req, (DWORD)sizeof(req));
    ModInfo mi{};
    if (isSuccess(s) && base) {
        // The driver returns the base only; image size is resolved from the
        // header so bounds checks in Nav still work.
        mi.base = (uintptr_t)base;
        size_t imageSize = 0;
        IMAGE_DOS_HEADER dos{};
        if (read(mi.base, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE) {
            IMAGE_NT_HEADERS64 nt{};
            if (read(mi.base + (uintptr_t)dos.e_lfanew, &nt, sizeof(nt)) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                imageSize = nt.OptionalHeader.SizeOfImage;
            }
        }
        mi.size = imageSize;
    } else {
        logx::line("ProcKernel module lookup failed for %s status=0x%08lX",
                   name.c_str(), (unsigned long)s);
    }

    cache_.push_back({ module, mi });
    return mi;
}

uintptr_t ProcKernel::getModuleBase(const wchar_t* module) {
    return getModule(module).base;
}
