#include "ESP.h"

#include <cmath>

namespace esp {
namespace {

ImU32 toU32(const float c[4], float alphaScale = 1.f) {
    float a = clampf(c[3] * alphaScale, 0.f, 1.f);
    return IM_COL32((int)(clampf(c[0], 0.f, 1.f) * 255.f),
                    (int)(clampf(c[1], 0.f, 1.f) * 255.f),
                    (int)(clampf(c[2], 0.f, 1.f) * 255.f),
                    (int)(a * 255.f));
}

void strokeRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float th) {
    // Dark backing then the colour, so the box reads on bright maps.
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 170), 0.f, 0, th + 2.f);
    dl->AddRect(a, b, col, 0.f, 0, th);
}

void healthBar(ImDrawList* dl, const Box& box, int hp, int maxHp) {
    if (maxHp <= 0) maxHp = 100;
    float pct = clampf((float)hp / (float)maxHp, 0.f, 1.f);

    float x  = box.min.x - 6.f;
    ImVec2 bgMin(x - 1.f, box.min.y - 1.f);
    ImVec2 bgMax(x + 4.f, box.max.y + 1.f);
    dl->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 190));

    float h = box.max.y - box.min.y;
    ImU32 fill = IM_COL32((int)(255.f * (1.f - pct)), (int)(255.f * pct), 0, 235);
    dl->AddRectFilled(ImVec2(x, box.max.y - h * pct), ImVec2(x + 3.f, box.max.y), fill);
}

void shadowText(ImDrawList* dl, const ImVec2& pos, ImU32 col, const char* txt) {
    if (!txt || !*txt) return;
    dl->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 215), txt);
    dl->AddText(pos, col, txt);
}

// Fill a BoneSet for one player. Cleared first: a stale ok[] from the previous
// frame would draw a bone that no longer resolves.
void loadSkeleton(const Nav& nav, const Player& pl, BoneSet& bs) {
    bs = BoneSet{};
    for (int i = 0; i < kSkeletonPairCount; ++i) {
        const BonePair& pair = kSkeletonPairs[i];
        for (int k = 0; k < 2; ++k) {
            int idx = (k == 0) ? pair.a : pair.b;
            if (idx < 0 || idx >= Bone::MAX_BONES) continue;
            if (bs.ok[idx]) continue;
            Vec3 wp;
            if (nav.bone(pl.pawn, idx, pl.origin, wp)) {
                bs.pts[idx] = wp;
                bs.ok[idx]  = true;
            }
        }
    }
}

} // namespace

Box computeBox(const Nav& nav, const Player& pl, const Frame& frame) {
    Box box;

    Vec3 head{}, foot{};
    bool haveHead = nav.bone(pl.pawn, Bone::HEAD, pl.origin, head);
    bool haveFoot = nav.bone(pl.pawn, Bone::R_FOOT, pl.origin, foot) ||
                    nav.bone(pl.pawn, Bone::L_FOOT, pl.origin, foot);

    // Fall back to estimated points so the box still appears when the bone
    // array is momentarily unreadable (weapon switch, respawn).
    Vec3 headPt = haveHead ? head : Vec3{ pl.origin.x, pl.origin.y, pl.origin.z + 72.f };
    Vec3 footPt = haveFoot ? foot : Vec3{ pl.origin.x, pl.origin.y, pl.origin.z - 2.f };

    Vec2 hs, fs;
    if (!frame.screen.project(headPt, hs)) return box;
    if (!frame.screen.project(footPt, fs)) return box;
    if (!hs.finite() || !fs.finite()) return box;

    float top = hs.y;
    float bot = fs.y;
    if (bot <= top) return box;

    float h = bot - top;
    if (h < 4.f || h > 4000.f) return box;

    float w  = h * 0.42f;
    float cx = (hs.x + fs.x) * 0.5f;

    box.min = ImVec2(cx - w * 0.5f, top);
    box.max = ImVec2(cx + w * 0.5f, bot);
    box.valid = true;
    return box;
}

void draw(ImDrawList* dl, const Nav& nav, const Config& cfg, Frame& frame) {
    if (!dl || !frame.haveLocal || !frame.screen.valid) return;

    frame.skeletons.resize(frame.players.size());

    const int myTeam = frame.local.team;

    if (cfg.esp) {
        for (size_t i = 0; i < frame.players.size(); ++i) {
            const Player& pl = frame.players[i];
            if (pl.pawn == frame.local.pawn) continue;
            if (pl.team == myTeam && cfg.espTeamFilter == 1) continue;
            if (!pl.alive()) continue;

            float dist = pl.distTo(frame.local);
            if (dist > cfg.espMaxDist) continue;

            const bool enemy = (pl.team != myTeam);
            const ImU32 col = enemy ? toU32(cfg.espEnemyCol) : toU32(cfg.espTeamCol);

            Box box = computeBox(nav, pl, frame);

            if (box.valid) {
                if (cfg.espFilled) {
                    dl->AddRectFilled(box.min, box.max,
                        IM_COL32((int)(cfg.espEnemyCol[0] * 255.f),
                                 (int)(cfg.espEnemyCol[1] * 255.f),
                                 (int)(cfg.espEnemyCol[2] * 255.f), 28));
                }
                if (cfg.espBox)     strokeRect(dl, box.min, box.max, col, cfg.espThickness);
                if (cfg.espHealth)  healthBar(dl, box, pl.health, pl.maxHealth);

                float textY = box.min.y - 15.f;
                if (cfg.espName && pl.name[0]) {
                    shadowText(dl, ImVec2(box.min.x, textY), col, pl.name);
                    textY -= 15.f;
                }
                if (cfg.espDistance) {
                    char buf[32];
                    sprintf_s(buf, "%.0f m", dist * 0.0254f);
                    shadowText(dl, ImVec2(box.min.x, box.max.y + 2.f), col, buf);
                }
                if (cfg.espSnaplines) {
                    dl->AddLine(ImVec2(frame.screenCenter.x, (float)frame.screen.h),
                                ImVec2(box.max.x, box.max.y), col, 1.f);
                }
            }

            if (cfg.espSkeleton) {
                BoneSet& bs = frame.skeletons[i];
                loadSkeleton(nav, pl, bs);

                const ImU32 skelCol = toU32(cfg.espSkelCol);
                for (int p = 0; p < kSkeletonPairCount; ++p) {
                    const BonePair& pair = kSkeletonPairs[p];
                    if (pair.a >= Bone::MAX_BONES || pair.b >= Bone::MAX_BONES) continue;
                    if (!bs.ok[pair.a] || !bs.ok[pair.b]) continue;

                    Vec2 sa, sb;
                    if (!frame.screen.project(bs.pts[pair.a], sa)) continue;
                    if (!frame.screen.project(bs.pts[pair.b], sb)) continue;

                    dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), skelCol, 1.4f);
                }
            }
        }
    }

    // FOV ring: the exact angular radius the aimbot will consider, projected
    // through the same half-height the target search uses.
    if (cfg.aimbot) {
        float radius = std::tanf(cfg.aimFov * DEG2RAD) * ((float)frame.screen.h * 0.5f);
        radius = clampf(radius, 4.f, 4000.f);
        dl->AddCircle(ImVec2(frame.screenCenter.x, frame.screenCenter.y),
                      radius, IM_COL32(255, 255, 255, 70), 0, 1.0f);
    }
}

} // namespace esp
