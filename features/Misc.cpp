#include "Misc.h"

#include "../core/Log.h"

void Misc::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    if (!cfg.noFlash) return;
    if (!frame.haveLocal || !frame.local.alive()) return;

    IMemory& mem = nav.mem();
    if (!mem.canWrite()) {
        if (!warnedWrite_) {
            warnedWrite_ = true;
            logx::line("Misc: no-flash needs a writeable backend, staying off");
        }
        return;
    }

    float started = mem.rd<float>(frame.local.pawn + g_off.m_flFlashBangTime);
    float duration = mem.rd<float>(frame.local.pawn + g_off.m_flFlashDuration);

    // Only touch it while a flash is actually in flight; writing every frame
    // would fight the engine's own bookkeeping.
    if (started > 0.f || duration > 0.f) {
        mem.wr<float>(frame.local.pawn + g_off.m_flFlashBangTime, 0.f);
        mem.wr<float>(frame.local.pawn + g_off.m_flFlashDuration, 0.f);
    }
}
