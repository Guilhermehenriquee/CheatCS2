// muei 無影 - memory backend interface
//
// Every feature reads the target through this interface, never through a
// concrete class. That is what lets the same build run on a plain handle
// (ProcUser), a driver that hides the access (ProcKernel) or an FPGA/DMA card
// (ProcDMA) without a single #ifdef in features/.
//
// Design rules:
//   * read() returns false on any failure and never partially fills a caller
//     buffer without saying so - callers check the bool.
//   * The templated rd<T>() returns a zero-initialised T on failure so a bad
//     read degrades a frame instead of faulting.
//   * rdOk<T>() exists for the call sites that must tell "value is zero" from
//     "the read failed" - that distinction is the whole difference between a
//     dormant entity and a corrupt pointer chain.
#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <string>

struct ModInfo {
    uintptr_t base = 0;
    size_t    size = 0;
    bool valid() const { return base != 0 && size != 0; }
};

class IMemory {
public:
    virtual ~IMemory() = default;

    // ------------------------------------------------------------- lifecycle --
    // Resolve by image name (L"cs2.exe"). Clears any previous attachment.
    virtual bool attach(const wchar_t* process) = 0;

    // Resolve by pid, for when the name search is hidden but the pid is known
    // (window ownership is the usual route).
    virtual bool attachPid(DWORD targetPid) = 0;

    // Release handles and caches. Must be safe to call twice.
    virtual void detach() = 0;

    // ------------------------------------------------------------ primitives --
    virtual bool read(uintptr_t addr, void* buf, size_t size) = 0;
    virtual bool write(uintptr_t addr, const void* buf, size_t size) = 0;

    virtual uintptr_t getModuleBase(const wchar_t* module) = 0;

    // Module base + image size. Default implementation reports size 0, which
    // callers treat as "base known, bounds unknown".
    virtual ModInfo getModule(const wchar_t* module) {
        ModInfo mi{};
        mi.base = getModuleBase(module);
        return mi;
    }

    // Read access requested at attach; write capability is separate because
    // most backends grant read long before write (and some never).
    virtual bool canWrite() { return false; }

    // ---------------------------------------------------------------- status --
    virtual bool alive() = 0;
    virtual DWORD pid() const = 0;
    virtual const wchar_t* backendName() const = 0;
    virtual const std::wstring& lastError() const = 0;

    // ------------------------------------------------------------- templates --
    template <typename T>
    bool rdOk(uintptr_t addr, T& out) const {
        T tmp{};
        if (!const_cast<IMemory*>(this)->read(addr, &tmp, sizeof(T))) return false;
        out = tmp;
        return true;
    }

    template <typename T>
    T rd(uintptr_t addr) const {
        T tmp{};
        const_cast<IMemory*>(this)->read(addr, &tmp, sizeof(T));
        return tmp;
    }

    template <typename T>
    bool wr(uintptr_t addr, const T& value) {
        return write(addr, &value, sizeof(T));
    }

    bool rdBytes(uintptr_t addr, void* out, size_t size) {
        return read(addr, out, size);
    }
    bool wrBytes(uintptr_t addr, const void* data, size_t size) {
        return write(addr, data, size);
    }

    // Pointer chase: deref(root, {a, b}) == *(*(root + a) + b)
    uintptr_t deref(uintptr_t root, std::initializer_list<uintptr_t> chain) {
        uintptr_t p = root;
        for (uintptr_t off : chain) {
            if (!plausible(p)) return 0;
            p = rd<uintptr_t>(p + off);
        }
        return p;
    }

    // A user-mode pointer below 0x10000 is never a real object, and the
    // canonical user range on x64 ends before 0x00007FFFFFFF0000.
    static bool plausible(uintptr_t p) {
        return p > 0x10000ull && p < 0x00007FFFFFFE0000ull;
    }
};
