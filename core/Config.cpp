#include "Config.h"
#include "Log.h"

#include "../vendor/nlohmann/json.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <type_traits>

using nlohmann::json;

namespace {

json col(const float c[4]) { return json::array({ c[0], c[1], c[2], c[3] }); }

void readCol(const json& j, const char* key, float out[4]) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 4) return;
    for (int i = 0; i < 4; ++i) {
        if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>();
    }
}

template <typename T>
void get(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        if constexpr (std::is_same_v<T, int>) {
            if (it->is_number_integer())      out = it->get<int>();
            else if (it->is_number_unsigned())out = (int)it->get<unsigned>();
            else if (it->is_number_float())   out = (int)it->get<double>();
        } else if constexpr (std::is_same_v<T, float>) {
            if (it->is_number()) out = it->get<float>();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (it->is_boolean()) out = it->get<bool>();
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (it->is_string()) out = it->get<std::string>();
        }
    } catch (const json::exception&) {
        // One bad field keeps its default; the rest of the file still loads.
    }
}

void readName(const json& j, const char* key, char* dst, size_t cap) {
    std::string tmp;
    get(j, key, tmp);
    if (tmp.empty()) return;
    strncpy_s(dst, cap, tmp.c_str(), _TRUNCATE);
}

} // namespace

std::wstring Config::defaultPath() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *(slash + 1) = L'\0';
    return std::wstring(exe) + L"muei.cfg";
}

void Config::reset() { *this = Config{}; }

bool Config::save() const {
    json j;
    j["version"] = version;

    j["esp"]           = esp;
    j["espBox"]        = espBox;
    j["espHealth"]     = espHealth;
    j["espName"]       = espName;
    j["espSkeleton"]   = espSkeleton;
    j["espDistance"]   = espDistance;
    j["espFilled"]     = espFilled;
    j["espSnaplines"]  = espSnaplines;
    j["espMaxDist"]    = espMaxDist;
    j["espTeamFilter"] = espTeamFilter;
    j["espThickness"]  = espThickness;
    j["espEnemyCol"]   = col(espEnemyCol);
    j["espTeamCol"]    = col(espTeamCol);
    j["espSkelCol"]    = col(espSkelCol);

    j["bhop"]           = bhop;
    j["bhopAutoStrafe"] = bhopAutoStrafe;

    j["triggerbot"]       = triggerbot;
    j["triggerDelayMs"]   = triggerDelayMs;
    j["triggerHoldMs"]    = triggerHoldMs;
    j["triggerTeamFlag"]  = triggerTeamFlag;
    j["triggerHeadOnly"]  = triggerHeadOnly;
    j["triggerUseDriver"] = triggerUseDriver;

    j["aimbot"]         = aimbot;
    j["aimVisibleOnly"] = aimVisibleOnly;
    j["aimMode"]        = (int)aimMode;
    j["aimKey"]         = (int)aimKey;
    j["aimFov"]         = aimFov;
    j["aimSmooth"]      = aimSmooth;
    j["aimBone"]        = aimBone;
    j["aimAutoFire"]    = aimAutoFire;
    j["aimMaxDist"]     = aimMaxDist;
    j["aimSens"]        = aimSens;
    j["aimUseDriver"]   = aimUseDriver;

    j["resolverOn"]          = resolverOn;
    j["resolverMode"]        = (int)resolverMode;
    j["resolverBulletSpeed"] = resolverBulletSpeed;
    j["resolverLatency"]     = resolverLatency;

    j["rcs"]        = rcs;
    j["rcsScaleX"]  = rcsScaleX;
    j["rcsScaleY"]  = rcsScaleY;
    j["rcsBullets"] = rcsBullets;

    j["glow"]      = glow;
    j["glowType"]  = glowType;
    j["glowEnemy"] = col(glowEnemy);
    j["glowTeam"]  = col(glowTeam);

    j["skinChanger"]  = skinChanger;
    j["skinPaintKit"] = skinPaintKit;
    j["skinSeed"]     = skinSeed;
    j["skinWear"]     = skinWear;
    j["skinStatTrak"] = skinStatTrak;
    j["skinQuality"]  = skinQuality;
    j["skinName"]     = skinName;
    j["skinSetMask"]  = skinSetMask;

    j["noFlash"]   = noFlash;
    j["menuKey"]   = menuKey;
    j["panicKey"]  = panicKey;
    j["backend"]   = backend;
    j["accentCol"] = col(accentCol);

    std::wstring path = defaultPath();
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        logx::line("config save failed (cannot open %ls)", path.c_str());
        return false;
    }
    out << j.dump(2);
    return out.good();
}

bool Config::load() {
    std::wstring path = defaultPath();

    if (!std::ifstream(std::filesystem::path(path)).good()) {
        logx::line("config: no file at %ls, using defaults", path.c_str());
        return false;
    }

    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (text.empty()) {
        logx::line("config: empty file, using defaults");
        return false;
    }

    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& e) {
        logx::line("config: parse failed (%s), moving aside", e.what());
        std::wstring bad = path + L".bad";
        MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    get(j, "version", version);

    get(j, "esp", esp);
    get(j, "espBox", espBox);
    get(j, "espHealth", espHealth);
    get(j, "espName", espName);
    get(j, "espSkeleton", espSkeleton);
    get(j, "espDistance", espDistance);
    get(j, "espFilled", espFilled);
    get(j, "espSnaplines", espSnaplines);
    get(j, "espMaxDist", espMaxDist);
    get(j, "espTeamFilter", espTeamFilter);
    get(j, "espThickness", espThickness);
    readCol(j, "espEnemyCol", espEnemyCol);
    readCol(j, "espTeamCol", espTeamCol);
    readCol(j, "espSkelCol", espSkelCol);

    get(j, "bhop", bhop);
    get(j, "bhopAutoStrafe", bhopAutoStrafe);

    get(j, "triggerbot", triggerbot);
    get(j, "triggerDelayMs", triggerDelayMs);
    get(j, "triggerHoldMs", triggerHoldMs);
    get(j, "triggerTeamFlag", triggerTeamFlag);
    get(j, "triggerHeadOnly", triggerHeadOnly);
    get(j, "triggerUseDriver", triggerUseDriver);

    get(j, "aimbot", aimbot);
    get(j, "aimVisibleOnly", aimVisibleOnly);
    { int v = (int)aimMode; get(j, "aimMode", v); aimMode = (AimMode)v; }
    { int v = (int)aimKey;  get(j, "aimKey", v);  aimKey  = (AimKey)v; }
    get(j, "aimFov", aimFov);
    get(j, "aimSmooth", aimSmooth);
    get(j, "aimBone", aimBone);
    get(j, "aimAutoFire", aimAutoFire);
    get(j, "aimMaxDist", aimMaxDist);
    get(j, "aimSens", aimSens);
    get(j, "aimUseDriver", aimUseDriver);

    get(j, "resolverOn", resolverOn);
    { int v = (int)resolverMode; get(j, "resolverMode", v); resolverMode = (ResolveMode)v; }
    get(j, "resolverBulletSpeed", resolverBulletSpeed);
    get(j, "resolverLatency", resolverLatency);

    get(j, "rcs", rcs);
    get(j, "rcsScaleX", rcsScaleX);
    get(j, "rcsScaleY", rcsScaleY);
    get(j, "rcsBullets", rcsBullets);

    get(j, "glow", glow);
    get(j, "glowType", glowType);
    readCol(j, "glowEnemy", glowEnemy);
    readCol(j, "glowTeam", glowTeam);

    get(j, "skinChanger", skinChanger);
    get(j, "skinPaintKit", skinPaintKit);
    get(j, "skinSeed", skinSeed);
    get(j, "skinWear", skinWear);
    get(j, "skinStatTrak", skinStatTrak);
    get(j, "skinQuality", skinQuality);
    readName(j, "skinName", skinName, sizeof(skinName));
    get(j, "skinSetMask", skinSetMask);

    get(j, "noFlash", noFlash);
    get(j, "menuKey", menuKey);
    get(j, "panicKey", panicKey);
    get(j, "backend", backend);
    readCol(j, "accentCol", accentCol);

    // Clamp anything a hand-edited file could push out of range.
    if (aimSmooth < 1.f) aimSmooth = 1.f;
    if (aimFov <= 0.f) aimFov = 1.f;
    if (aimFov > 90.f) aimFov = 90.f;
    if (aimSens <= 0.f) aimSens = 1.f;
    if (espThickness < 0.5f) espThickness = 0.5f;
    if (resolverBulletSpeed < 1.f) resolverBulletSpeed = 1.f;
    if (resolverLatency < 0.f) resolverLatency = 0.f;
    if (rcsScaleX <= 0.f) rcsScaleX = 0.01f;
    if (rcsScaleY <= 0.f) rcsScaleY = 0.01f;
    if (rcsBullets < 0) rcsBullets = 0;
    if (triggerDelayMs < 0) triggerDelayMs = 0;
    if (triggerHoldMs < 1) triggerHoldMs = 1;
    if (menuKey <= 0 || menuKey > 0xFF) menuKey = VK_INSERT;
    if (panicKey <= 0 || panicKey > 0xFF) panicKey = VK_END;
    if (backend < 0 || backend > 3) backend = (int)Backend::Auto;
    if (glowType < 0) glowType = 0;

    logx::line("config loaded (version %d, backend %d)", version, backend);
    return true;
}
