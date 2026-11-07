// muei 無影 - kernel backend.
//
// Talks to an AimStar-protocol driver. That protocol is not DeviceIoControl:
// the driver registers a registry callback and the user-mode side issues
// RegSetValueExA on
//
//     HKLM\SOFTWARE\Microsoft\Clipboard  value "DisableAntiSpyware"
//
// where the registry *value type* field carries the command code and the value
// *data* carries the request struct. The driver's pre-set callback reads the
// request, performs the operation in our process context (so user-mode
// pointers in the struct are dereferenceable) and returns an NTSTATUS.
//
// Command codes are the multi-character literals '0000'..'0021' from the
// reference client, i.e. ASCII digits packed big-endian into a DWORD. The whole
// set is replicated here so the backend is a drop-in for that driver.
//
// If no driver is loaded, attach() fails with a clear message and the caller
// falls back to ProcUser. Nothing here assumes the driver exists.
#pragma once

#include "IMemory.h"

#include <string>
#include <vector>

namespace aimstar {

// ASCII "0000".."0021" packed the way an MSVC multi-character literal packs.
constexpr DWORD cmd(int n) {
    return ((DWORD)('0' + (n / 1000) % 10) << 24) |
           ((DWORD)('0' + (n / 100) % 10) << 16) |
           ((DWORD)('0' + (n / 10) % 10) << 8) |
           ((DWORD)('0' + n % 10));
}

constexpr DWORD CMD_PING         = cmd(0);    // 0000
constexpr DWORD CMD_VERIFY       = cmd(1);    // 0001
constexpr DWORD CMD_INJECT       = cmd(2);    // 0002
constexpr DWORD CMD_GRANT_HANDLE = cmd(3);    // 0003
constexpr DWORD CMD_BASE_ADDR    = cmd(4);    // 0004
constexpr DWORD CMD_MODULE_ADDR  = cmd(5);    // 0005
constexpr DWORD CMD_READ_WRITE   = cmd(6);    // 0006
constexpr DWORD CMD_DELETE_FILE  = cmd(7);    // 0007
constexpr DWORD CMD_PROTECT_PROC = cmd(8);    // 0008
constexpr DWORD CMD_HIDE_PROC    = cmd(9);    // 0009
constexpr DWORD CMD_KILL_PROC    = cmd(10);   // 0010
constexpr DWORD CMD_ALLOC        = cmd(11);   // 0011
constexpr DWORD CMD_FREE         = cmd(12);   // 0012
constexpr DWORD CMD_PROTECT_MEM  = cmd(13);   // 0013
constexpr DWORD CMD_HIDE_MEM     = cmd(14);   // 0014
constexpr DWORD CMD_QUERY_MEM    = cmd(15);   // 0015
constexpr DWORD CMD_CREATE_THREAD= cmd(16);   // 0016
constexpr DWORD CMD_MOUSE        = cmd(17);   // 0017
constexpr DWORD CMD_KEYBOARD     = cmd(18);   // 0018
constexpr DWORD CMD_SPOOF_HWID   = cmd(19);   // 0019
constexpr DWORD CMD_FIND_PATTERN = cmd(20);   // 0020
constexpr DWORD CMD_HIDE_WINDOW  = cmd(21);   // 0021

constexpr NTSTATUS STATUS_OK = (NTSTATUS)0xE0000000;
constexpr NTSTATUS STATUS_FAIL = (NTSTATUS)0xE0000001;

constexpr DWORD RW_READ     = 0;
constexpr DWORD RW_WRITE    = 1;
constexpr DWORD RW_WRITE_BP = 2;   // write, breakpoint-protected

#pragma pack(push, 8)
struct ReadWriteReq {
    ULONG64 hProcessId;      // +0x00
    ULONG64 TargetAddress;   // +0x08  target-side address
    ULONG64 SourceAddress;   // +0x10  our-side buffer
    ULONG64 NumberOfBytes;   // +0x18
    ULONG32 ReadWriteType;   // +0x20  RW_READ / RW_WRITE
    ULONG32 Reserved;        // +0x24  keep the struct 40 bytes like the client
};

struct BaseReq {
    ULONG64 hProcessId;
    ULONG64 OutBuffer;       // ULONG64* in our process
};

struct ModuleReq {
    ULONG64 hProcessId;
    ULONG64 ModuleName;      // char* in our process
    ULONG64 OutBuffer;       // ULONG64* in our process
};

struct AllocReq {
    ULONG64 hProcessId;
    ULONG64 MemSize;
    ULONG32 MemProtect;
    ULONG32 HighAddress;
    ULONG64 OutBuffer;
};

struct FreeReq {
    ULONG64 hProcessId;
    ULONG64 MemoryAddress;
};
#pragma pack(pop)

static_assert(sizeof(ReadWriteReq) == 40, "driver expects a 40-byte read/write request");

// The kernel mouse input packet, same shape the reference uses for the driver's
// simulated-input command. Feature code that wants driver-level input can post
// these instead of SendInput.
struct MouseInputData {
    USHORT UnitId;
    USHORT Flags;
    union {
        ULONG Buttons;
        struct {
            USHORT ButtonFlags;
            USHORT ButtonData;
        };
    };
    ULONG RawButtons;
    LONG  LastX;
    LONG  LastY;
    ULONG ExtraInformation;
};

struct KeyboardInputData {
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG  ExtraInformation;
};

} // namespace aimstar

class ProcKernel final : public IMemory {
public:
    ProcKernel() = default;
    ~ProcKernel() override { detach(); }

    ProcKernel(const ProcKernel&) = delete;
    ProcKernel& operator=(const ProcKernel&) = delete;

    bool attach(const wchar_t* process) override;
    bool attachPid(DWORD targetPid) override;
    void detach() override;

    bool read(uintptr_t addr, void* buf, size_t size) override;
    bool write(uintptr_t addr, const void* buf, size_t size) override;

    uintptr_t getModuleBase(const wchar_t* module) override;
    ModInfo   getModule(const wchar_t* module) override;

    bool canWrite() override { return hKey_ != nullptr; }

    bool alive() override;
    DWORD pid() const override { return pid_; }
    const wchar_t* backendName() const override { return L"kernel"; }
    const std::wstring& lastError() const override { return lastError_; }

    // Driver presence probe. Returns the raw status so a variant driver that
    // answers with a different success code is diagnosable from the log.
    NTSTATUS probe();

    // Raw command dispatch, exposed so future features (input injection, memory
    // allocation) do not need a second copy of the channel setup.
    NTSTATUS call(DWORD command, void* data, DWORD size);

    // Driver-level input. Returns false when the driver is absent; callers fall
    // back to SendInput.
    bool sendMouse(const aimstar::MouseInputData& data);

private:
    void setError(const wchar_t* what, DWORD code);
    bool openChannel();
    bool isSuccess(NTSTATUS s) const;

    DWORD        pid_       = 0;
    HKEY         hKey_      = nullptr;
    uintptr_t    exeBase_   = 0;
    std::wstring lastError_;
    std::vector<std::pair<std::wstring, ModInfo>> cache_;
};
