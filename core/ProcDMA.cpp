#include "ProcDMA.h"
#include "Log.h"

namespace {

// MemProcFS flags (vmmdll.h). NOCACHE forces a fresh physical read instead of
// serving the previous tick out of the page cache.
constexpr unsigned long VMMDLL_FLAG_NOCACHE     = 0x0001;
constexpr unsigned long VMMDLL_FLAG_NOCACHEPUT  = 0x0002;

constexpr size_t kMaxPerCall = 0x1000;

// Resolve an export into a typed function pointer.
//
// A direct reinterpret_cast from FARPROC to the real signature is the usual
// idiom but is a cast between incompatible function types, which -Wcast-function
// warns about. Copying the bits avoids the type pun entirely: the representation
// of a function pointer is the same either way on every ABI we target, and the
// size assert keeps that honest.
template <typename T>
void resolveExport(HMODULE module, const char* name, T& out) {
    static_assert(sizeof(T) == sizeof(FARPROC), "function pointer size mismatch");
    FARPROC raw = GetProcAddress(module, name);
    out = nullptr;
    if (raw) memcpy(&out, &raw, sizeof(T));
}

} // namespace

std::string ProcDMA::toAscii(const wchar_t* w) const {
    if (!w) return {};
    char buf[256]{};
    WideCharToMultiByte(CP_ACP, 0, w, -1, buf, (int)sizeof(buf) - 1, nullptr, nullptr);
    return buf;
}

bool ProcDMA::resolve() {
    if (dll_) return true;

    dll_ = LoadLibraryW(L"vmm.dll");
    if (!dll_) {
        lastError_ = L"vmm.dll not found (is MemProcFS on PATH or beside the exe?)";
        logx::wide("ProcDMA", lastError_);
        return false;
    }

    pInitialize       = nullptr;
    pClose            = nullptr;
    pPidGetFromName   = nullptr;
    pMemReadEx        = nullptr;
    pMemWrite         = nullptr;
    pGetModuleBaseU64 = nullptr;
    pGetModuleBaseW   = nullptr;

    resolveExport(dll_, "VMMDLL_Initialize",              pInitialize);
    resolveExport(dll_, "VMMDLL_Close",                   pClose);
    resolveExport(dll_, "VMMDLL_PidGetFromName",          pPidGetFromName);
    resolveExport(dll_, "VMMDLL_MemReadEx",               pMemReadEx);
    resolveExport(dll_, "VMMDLL_MemWrite",                pMemWrite);
    resolveExport(dll_, "VMMDLL_ProcessGetModuleBaseU64", pGetModuleBaseU64);
    resolveExport(dll_, "VMMDLL_ProcessGetModuleBaseW",   pGetModuleBaseW);

    const bool coreOk = pInitialize && pClose && pPidGetFromName &&
                        pMemReadEx && pMemWrite;
    const bool modOk  = pGetModuleBaseU64 || pGetModuleBaseW;

    if (!coreOk || !modOk) {
        lastError_ = L"vmm.dll loaded but required exports are missing";
        logx::wide("ProcDMA", lastError_);
        return false;
    }
    return true;
}

bool ProcDMA::initDevice(const char* device) {
    if (!resolve()) return false;
    if (ctx_) return true;

    const char* argv[4]{};
    unsigned long argc = 0;
    argv[argc++] = "";
    if (device && *device) {
        argv[argc++] = "-device";
        argv[argc++] = device;
    }

    ctx_ = pInitialize(argc, argv);
    if (!ctx_) {
        lastError_ = L"VMMDLL_Initialize failed (no FPGA/DMA device found)";
        logx::wide("ProcDMA", lastError_);
        return false;
    }
    logx::line("ProcDMA device initialised (argc=%lu)", (unsigned long)argc);
    return true;
}

bool ProcDMA::attach(const wchar_t* process) {
    detach();
    lastError_.clear();

    if (!initDevice(nullptr)) return false;

    std::string name = toAscii(process);
    unsigned long pid = 0;
    if (!pPidGetFromName(ctx_, name.c_str(), &pid) || !pid) {
        lastError_ = logx::formatWin32(L"VMMDLL_PidGetFromName failed", GetLastError());
        logx::wide("ProcDMA", lastError_);
        return false;
    }
    return attachPid((DWORD)pid);
}

bool ProcDMA::attachPid(DWORD targetPid) {
    if (!targetPid) {
        lastError_ = L"no pid supplied";
        return false;
    }
    if (!ctx_ && !initDevice(nullptr)) return false;

    pid_ = targetPid;
    cache_.clear();
    logx::line("ProcDMA attached pid=%lu", (unsigned long)pid_);
    return true;
}

void ProcDMA::detach() {
    if (ctx_ && pClose) {
        pClose(ctx_);
    }
    ctx_ = nullptr;
    pid_ = 0;
    cache_.clear();
    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
    pInitialize = nullptr;
    pClose = nullptr;
    pPidGetFromName = nullptr;
    pMemReadEx = nullptr;
    pMemWrite = nullptr;
    pGetModuleBaseU64 = nullptr;
    pGetModuleBaseW = nullptr;
}

bool ProcDMA::alive() {
    if (!ctx_ || !pid_) return false;

    // A four-byte read on the image base is the cheapest liveness probe that
    // goes through the same path the features use.
    unsigned char probe[4]{};
    unsigned long got = 0;
    return pMemReadEx(ctx_, pid_, 0x140000000ull, probe, 4, &got,
                      VMMDLL_FLAG_NOCACHE) && got == 4;
}

bool ProcDMA::read(uintptr_t addr, void* buf, size_t size) {
    if (!ctx_ || !pid_ || !addr || !buf || !size) return false;

    unsigned char* out = static_cast<unsigned char*>(buf);
    size_t done = 0;
    while (done < size) {
        unsigned long chunk = (unsigned long)((size - done > kMaxPerCall)
                                              ? kMaxPerCall : (size - done));
        unsigned long got = 0;
        BOOL ok = pMemReadEx(ctx_, pid_, (unsigned long long)(addr + done),
                             out + done, chunk, &got, VMMDLL_FLAG_NOCACHE);
        if (!ok || got != chunk) {
            logx::line("ProcDMA read fail addr=0x%llX want=%lu got=%lu",
                       (unsigned long long)(addr + done), chunk, got);
            return false;
        }
        done += chunk;
    }
    return true;
}

bool ProcDMA::write(uintptr_t addr, const void* buf, size_t size) {
    if (!ctx_ || !pid_ || !addr || !buf || !size) return false;

    const unsigned char* in = static_cast<const unsigned char*>(buf);
    size_t done = 0;
    while (done < size) {
        unsigned long chunk = (unsigned long)((size - done > kMaxPerCall)
                                              ? kMaxPerCall : (size - done));
        // MemWrite takes a non-const pointer in every SDK revision.
        unsigned char* p = const_cast<unsigned char*>(in + done);
        if (!pMemWrite(ctx_, pid_, (unsigned long long)(addr + done), p, chunk)) {
            logx::line("ProcDMA write fail addr=0x%llX size=%lu",
                       (unsigned long long)(addr + done), chunk);
            return false;
        }
        done += chunk;
    }
    return true;
}

ModInfo ProcDMA::getModule(const wchar_t* module) {
    if (!module || !ctx_ || !pid_) return {};

    for (auto& e : cache_) {
        if (!_wcsicmp(e.first.c_str(), module)) return e.second;
    }

    ModInfo mi{};
    unsigned long long base = 0;

    bool ok = false;
    if (pGetModuleBaseW) {
        ok = pGetModuleBaseW(ctx_, pid_, module, &base) != 0;
    }
    if (!ok && pGetModuleBaseU64) {
        std::string a = toAscii(module);
        ok = pGetModuleBaseU64(ctx_, pid_, a.c_str(), &base) != 0;
    }

    if (ok && base) {
        mi.base = (uintptr_t)base;

        IMAGE_DOS_HEADER dos{};
        if (read(mi.base, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE) {
            IMAGE_NT_HEADERS64 nt{};
            if (read(mi.base + (uintptr_t)dos.e_lfanew, &nt, sizeof(nt)) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                mi.size = nt.OptionalHeader.SizeOfImage;
            }
        }
    } else {
        logx::line("ProcDMA module lookup failed for %ls", module);
    }

    cache_.push_back({ module, mi });
    return mi;
}

uintptr_t ProcDMA::getModuleBase(const wchar_t* module) {
    return getModule(module).base;
}
