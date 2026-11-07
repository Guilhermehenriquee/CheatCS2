// muei 無影 - DMA backend (MemProcFS).
//
// Loads vmm.dll at runtime and resolves the handful of exports the overlay
// needs. No SDK headers, no link-time dependency: if MemProcFS is not installed
// attach() fails cleanly and the caller stays on ProcUser.
//
// Export names differ between MemProcFS v4 and v5 (ProcessGetModuleBase*), so
// both spellings are probed. Reads use MemReadEx with the no-cache flag set:
// a cached DMA read returns the previous tick's bone array, which looks exactly
// like an ESP that lags a frame behind the player.
#pragma once

#include "IMemory.h"

#include <string>
#include <vector>

class ProcDMA final : public IMemory {
public:
    ProcDMA() = default;
    ~ProcDMA() override { detach(); }

    ProcDMA(const ProcDMA&) = delete;
    ProcDMA& operator=(const ProcDMA&) = delete;

    bool attach(const wchar_t* process) override;
    bool attachPid(DWORD targetPid) override;
    void detach() override;

    bool read(uintptr_t addr, void* buf, size_t size) override;
    bool write(uintptr_t addr, const void* buf, size_t size) override;

    uintptr_t getModuleBase(const wchar_t* module) override;
    ModInfo   getModule(const wchar_t* module) override;

    bool canWrite() override { return ctx_ != nullptr; }

    bool alive() override;
    DWORD pid() const override { return pid_; }
    const wchar_t* backendName() const override { return L"dma"; }
    const std::wstring& lastError() const override { return lastError_; }

    // Load vmm.dll and initialise a context. device is the MemProcFS device
    // string ("fpga", "mmap", "existing", ...). Defaults to an FPGA scan.
    bool initDevice(const char* device = nullptr);

private:
    bool resolve();
    std::string toAscii(const wchar_t* w) const;

    using fn_Initialize            = void* (*)(unsigned long, const char**);
    using fn_Close                 = int   (*)(void*);
    using fn_PidGetFromName        = int   (*)(void*, const char*, unsigned long*);
    using fn_MemReadEx             = int   (*)(void*, unsigned long, unsigned long long,
                                                unsigned char*, unsigned long,
                                                unsigned long*, unsigned long);
    using fn_MemWrite              = int   (*)(void*, unsigned long, unsigned long long,
                                                unsigned char*, unsigned long);
    using fn_GetModuleBaseU64      = int   (*)(void*, unsigned long, const char*,
                                                unsigned long long*);
    using fn_GetModuleBaseW        = int   (*)(void*, unsigned long, const wchar_t*,
                                                unsigned long long*);

    HMODULE              dll_       = nullptr;
    void*                ctx_       = nullptr;
    DWORD                pid_       = 0;

    fn_Initialize        pInitialize        = nullptr;
    fn_Close             pClose             = nullptr;
    fn_PidGetFromName    pPidGetFromName    = nullptr;
    fn_MemReadEx         pMemReadEx         = nullptr;
    fn_MemWrite          pMemWrite          = nullptr;
    fn_GetModuleBaseU64  pGetModuleBaseU64  = nullptr;
    fn_GetModuleBaseW    pGetModuleBaseW    = nullptr;

    std::wstring lastError_;
    std::vector<std::pair<std::wstring, ModInfo>> cache_;
};
