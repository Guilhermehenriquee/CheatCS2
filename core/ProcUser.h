// muei 無影 - plain Win32 handle backend.
//
// ReadProcessMemory / WriteProcessMemory against a process opened with the
// minimum rights each direction needs. Module bases come from EnumProcessModules
// and are cached per module name because the overlay asks for client.dll every
// frame.
//
// This is the fallback backend: when it returns "Acesso negado" on VM_READ the
// game is at a higher integrity level than this process, which is the exact case
// the elevated manifest exists for, and the case ProcKernel exists for.
#pragma once

#include "IMemory.h"

#include <string>
#include <vector>

class ProcUser final : public IMemory {
public:
    ProcUser() = default;
    ~ProcUser() override { detach(); }

    ProcUser(const ProcUser&) = delete;
    ProcUser& operator=(const ProcUser&) = delete;

    bool attach(const wchar_t* process) override;
    bool attachPid(DWORD targetPid) override;
    void detach() override;

    bool read(uintptr_t addr, void* buf, size_t size) override;
    bool write(uintptr_t addr, const void* buf, size_t size) override;

    uintptr_t getModuleBase(const wchar_t* module) override;
    ModInfo   getModule(const wchar_t* module) override;

    bool canWrite() override;
    void dropWrite();

    bool alive() override;
    DWORD pid() const override { return pid_; }
    const wchar_t* backendName() const override { return L"user"; }
    const std::wstring& lastError() const override { return lastError_; }

private:
    struct Entry {
        std::wstring name;
        ModInfo      info;
    };

    void setError(const wchar_t* what, DWORD code);

    DWORD              pid_     = 0;
    HANDLE             read_    = nullptr;
    HANDLE             write_   = nullptr;
    uintptr_t          exeBase_ = 0;
    std::wstring       lastError_;
    std::vector<Entry> cache_;

    static DWORD   findPid(const wchar_t* exeName);
    static ModInfo findModuleByPid(DWORD target, const wchar_t* moduleName);
};
