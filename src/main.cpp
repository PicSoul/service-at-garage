// Service At Garage - telemetry-SDK plugin for American Truck Simulator and Euro Truck Simulator 2.
// Lets the player use the service (repair) screen in the bay of a garage they own. See README.md.

#include <windows.h>

#include <string>

#include "MinHook.h"
#include "bay.h"
#include "common.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "icon.h"
#include "scssdk_telemetry.h"
#include "signatures.h"
#include "version.h"
#include "world.h"

using namespace sag;

static std::string g_dir;
static HMODULE g_module;
static bool g_hooked;

struct HookEntry {
    uintptr_t target;
    void* detour;
    const char* name;
};
static HookEntry g_hooks[8];
static int g_numHooks;

static bool Hook(uintptr_t target, void* detour, void* original, const char* name) {
    if (target && MH_CreateHook((void*)target, detour, (void**)original) == MH_OK) {
        g_hooks[g_numHooks++] = {target, detour, name};
        return true;
    }
    Log("could not hook %s", name);
    return false;
}

// True if the game function still jumps straight to our detour. MinHook (x64) patches the target with
// "E9 rel32" to a relay "FF 25 00000000 <detour>" (or, for tiny functions, "EB F9" to that jump placed
// 5 bytes above). If another plugin hooked the function after us, its own jump is there instead, and
// disabling ours would restore the original bytes over it.
static bool StillOurs(const HookEntry& h) {
    uint8_t head[2];
    if (!SafeRead((void*)h.target, head, 2)) return false;
    uintptr_t site = (head[0] == 0xEB && head[1] == 0xF9) ? h.target - 5 : h.target;
    uint8_t jmp[5];
    if (!SafeRead((void*)site, jmp, 5) || jmp[0] != 0xE9) return false;
    uintptr_t relay = site + 5 + *(int32_t*)(jmp + 1);
    uint8_t abs[14];
    static const uint8_t kAbs[6] = {0xFF, 0x25, 0, 0, 0, 0};
    return SafeRead((void*)relay, abs, 14) && !memcmp(abs, kAbs, 6) && *(void**)(abs + 6) == h.detour;
}

static SCSAPI_VOID OnFrameStart(const scs_event_t, const void* const, const scs_context_t) { BayOnFrame(); }

static SCSAPI_VOID OnPauseChanged(const scs_event_t event, const void* const, const scs_context_t) {
    IconOnPause(event == SCS_TELEMETRY_EVENT_paused);
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    auto p = (const scs_telemetry_init_params_v101_t*)params;
    std::string ini = g_dir + "service_at_garage.ini";
    LoadConfig(ini);
    if (g_cfg.log) LogOpen(g_dir + "service_at_garage.log");
    std::string build = GameBuildId();
    Log("Service At Garage v%s, %s, game build %s", SAG_VERSION, GameName(), build.c_str());

    std::string summary;
    ResolveSignatures(ini, build, summary);
    Log("%s", summary.c_str());
    if (!g_game.core) {
        std::string msg = "[Service At Garage] v" SAG_VERSION " INACTIVE (" + summary +
                          "). After a game update, run tools\\update_check.bat.";
        p->common.log(SCS_LOG_TYPE_warning, msg.c_str());
        return SCS_RESULT_ok;
    }

    bool ok = MH_Initialize() == MH_OK;
    ok = ok && Hook(g_game.startActivation, (void*)HookStartActivation, &OrigStartActivation, "start_activation") &&
         Hook(g_game.stopActivation, (void*)HookStopActivation, &OrigStopActivation, "stop_activation") &&
         Hook(g_game.performActivation, (void*)HookPerformActivation, &OrigPerformActivation, "perform_activation") &&
         Hook(g_game.garageItemUpdate, (void*)HookGarageItemUpdate, &OrigGarageItemUpdate, "garage item update");
    if (ok && g_game.triggers && !Hook(g_game.triggerUpdate, (void*)HookTriggerUpdate, &OrigTriggerUpdate, "trigger update"))
        g_game.triggers = g_game.icon = false;
    if (ok && g_game.icon && !Hook(g_game.garageItemDraw, (void*)HookGarageItemDraw, &OrigGarageItemDraw, "garage item draw"))
        g_game.icon = false;
    ok = ok && MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
    if (!ok) {
        MH_Uninitialize();
        p->common.log(SCS_LOG_TYPE_error, "[Service At Garage] INACTIVE: installing hooks failed (see service_at_garage.log)");
        return SCS_RESULT_ok;
    }
    g_hooked = true;

    p->register_for_event(SCS_TELEMETRY_EVENT_frame_start, OnFrameStart, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_paused, OnPauseChanged, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_started, OnPauseChanged, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement,
                            SCS_TELEMETRY_CHANNEL_FLAG_none, OnTruckPlacement, nullptr);

    std::string msg = std::string("[Service At Garage] v" SAG_VERSION " active") +
                      (g_game.icon ? "" : g_game.triggers ? " (service icon unavailable)" : " (service icon unavailable, basic bay detection)") +
                      (g_cfg.enabled ? "" : " - disabled in service_at_garage.ini");
    p->common.log(SCS_LOG_TYPE_message, msg.c_str());
    Log("%s", msg.c_str());
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    if (g_hooked) {
        g_passthrough = true;
        int kept = 0;
        for (int i = 0; i < g_numHooks; i++) {
            const HookEntry& h = g_hooks[i];
            if (StillOurs(h) && MH_DisableHook((void*)h.target) == MH_OK) continue;
            kept++;
            Log("%s is hooked by another plugin on top of ours; leaving ours in place (pass-through)", h.name);
        }
        if (kept) {
            // Our detours and MinHook's trampolines must outlive the game's FreeLibrary: keep the DLL loaded
            // and skip MH_Uninitialize (it frees the trampolines).
            HMODULE pinned;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCWSTR)g_module, &pinned);
        } else {
            MH_Uninitialize();
        }
        g_hooked = false;
    }
    Log("shutdown");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        g_module = inst;
        char path[MAX_PATH];
        GetModuleFileNameA(inst, path, MAX_PATH);
        g_dir = path;
        g_dir = g_dir.substr(0, g_dir.find_last_of("\\/") + 1);
        g_base = (uintptr_t)GetModuleHandleW(nullptr);
        g_size = ((IMAGE_NT_HEADERS64*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    } else if (reason == DLL_PROCESS_DETACH) {
        LogClose();
    }
    return TRUE;
}
