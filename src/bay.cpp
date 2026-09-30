#include "bay.h"

#include "common.h"
#include "icon.h"
#include "signatures.h"
#include "world.h"

namespace sag {

void(__fastcall* OrigStartActivation)(void* self, int type, void* data);
void(__fastcall* OrigStopActivation)(void* self);
void(__fastcall* OrigPerformActivation)(void* self);

// Idle:         normal game behaviour; a bay sleep prompt may be replaced by service.
// ServiceShown: our service prompt is in the slot.
// BayWatch:     service was used or timed out; keep the sleep prompt in the slot while in the bay.
enum class BayState { Idle, ServiceShown, BayWatch };
static BayState g_state = BayState::Idle;
static void* g_gameplay;     // local_gameplay_mode_u (owner of the activation slot)
static void* g_serviceItem;  // garage item passed as the service prompt's item
static void* g_bayTrigger;   // sleep trigger of the bay (null = unknown, use distance)
static ULONGLONG g_shownAt;
static bool g_internal;      // the plugin itself is calling start/stop
static bool g_ourSleep;      // the sleep prompt in the slot was put there by the plugin
static bool g_noSleep;       // the game gave no sleep prompt in this bay (e.g. ATS car mode): never add one
static void* g_zone;         // eligible sleep zone the vehicle is in (seen by the trigger scan)
static ULONGLONG g_zoneSince;
static bool g_zoneHandled;   // this zone entry already led to a prompt (ours or the game's)

bool BayIdle() { return g_state == BayState::Idle; }

static void SlotRead(int& type, void*& item) {
    type = 0;
    item = nullptr;
    Read(g_gameplay, g_game.slotType, type);
    Read(g_gameplay, g_game.slotItem, item);
}

static void SetState(BayState s) {
    if (s == BayState::Idle) {
        g_ourSleep = false;
        g_noSleep = false;
        g_bayTrigger = nullptr;
    }
    g_state = s;
}

static void CallStart(int type, void* data) {
    g_internal = true;
    OrigStartActivation(g_gameplay, type, data);  // clears any current prompt itself via stop_activation
    g_internal = false;
}

static void CallStop() {
    g_internal = true;
    OrigStopActivation(g_gameplay);
    g_internal = false;
}

// The sleep trigger the truck is inside right now (nearest one if several).
static void* FindBayTrigger() {
    void* best = nullptr;
    double bestD = 1e18;
    for (int i = 0; i < g_numTriggers; i++) {
        if (!g_triggers[i].sleep || TriggerState(g_triggers[i].trig) != 3) continue;
        double dx = PlacementWorld(g_triggers[i].pos, 0) - g_truckX, dz = PlacementWorld(g_triggers[i].pos, 2) - g_truckZ;
        if (dx * dx + dz * dz < bestD) {
            bestD = dx * dx + dz * dz;
            best = g_triggers[i].trig;
        }
    }
    return best;
}

void __fastcall HookStartActivation(void* self, int type, void* data) {
    if (g_passthrough) return OrigStartActivation(self, type, data);
    g_gameplay = self;
    if (g_internal) {
        OrigStartActivation(self, type, data);
        return;
    }
    if (type == kActSleep || type == kActManageGarage) IconLogGeometry(type == kActManageGarage ? data : nullptr, type == kActSleep ? "sleep prompt" : "manage-garage prompt");
    // Only replace the sleep prompt on a fresh bay entry, not while still parked after service.
    if (type == kActSleep) g_zoneHandled = true;
    if (g_cfg.enabled && g_state == BayState::Idle && type == kActSleep && data == nullptr) {
        Log("sleep prompt requested (truck %.1f %.1f %.1f)", g_truckX, g_truckY, g_truckZ);
        double dist = 0;
        GarageEntry* g = FindEligibleGarage(&dist);
        if (g) {
            g_serviceItem = g->item;
            g_shownAt = GetTickCount64();
            SetState(BayState::ServiceShown);
            g_bayTrigger = g_game.triggers ? FindBayTrigger() : nullptr;
            Log("-> showing service prompt for garage %p (%.1f m), bay trigger %p", g->item, dist, g_bayTrigger);
            CallStart(kActService, g->item);
            return;
        }
        Log("-> no eligible garage nearby, keeping sleep prompt");
    }
    if (g_state != BayState::Idle) Log("game started prompt type %d while in bay", type);
    g_ourSleep = false;
    OrigStartActivation(self, type, data);
}

void __fastcall HookStopActivation(void* self) {
    if (g_passthrough) return OrigStopActivation(self);
    if (!g_internal && g_state != BayState::Idle) {
        int type = 0;
        Read(self, g_game.slotType, type);
        Log("game stopped prompt type %d while in bay", type);
        // Leaving is detected in BayOnFrame; a stop here is e.g. a truck rebuild.
        if (g_state == BayState::ServiceShown) SetState(BayState::BayWatch);
    }
    OrigStopActivation(self);
}

void __fastcall HookPerformActivation(void* self) {
    if (g_passthrough) return OrigPerformActivation(self);
    int type = 0;
    void* data = nullptr;
    Read(self, g_game.slotType, type);
    Read(self, g_game.slotItem, data);
    bool ours = g_state == BayState::ServiceShown && type == kActService && data == g_serviceItem;
    OrigPerformActivation(self);
    if (ours) {
        Log("service screen opened from garage bay");
        SetState(BayState::BayWatch);
    }
}

// Puts the sleep prompt in the slot if it holds our service prompt or nothing at all. Not in a bay where
// the game itself gave no sleep prompt: there our service prompt just stays while the vehicle is in the bay.
static void EnsureSleepPrompt(const char* why) {
    if (g_noSleep) return;
    int type;
    void* data;
    SlotRead(type, data);
    bool ourService = type == kActService && data == g_serviceItem;
    if (type != 0 && !ourService) return;  // sleep already there, or the game shows something else
    Log("restoring sleep prompt (%s, slot type was %d)", why, type);
    CallStart(kActSleep, nullptr);
    g_ourSleep = true;
}

// True once the truck has left the bay: its sleep trigger reports "outside" (or is gone). Without a known
// trigger, falls back to the distance from the garage.
static bool LeftBay(const char** why) {
    if (g_bayTrigger) {
        if (!TriggerListed(g_bayTrigger)) {
            *why = "bay trigger unloaded";
            return true;
        }
        *why = "bay trigger reports outside";
        return TriggerState(g_bayTrigger) == 2;
    }
    GarageEntry* g = FindGarage(g_serviceItem);
    if (!g) {
        *why = "garage no longer nearby";
        return true;
    }
    double dx = g->x - g_truckX, dz = g->z - g_truckZ;
    *why = "moved away from garage";
    return dx * dx + dz * dz > (double)g_cfg.radius * g_cfg.radius;
}

// Clears whatever prompt the plugin put in the slot and returns to normal behaviour.
static void LeaveBay(const char* why) {
    int type;
    void* data;
    SlotRead(type, data);
    bool ours = (type == kActService && data == g_serviceItem) || (g_ourSleep && type == kActSleep);
    Log("truck left the bay (%s)%s", why, ours ? ", clearing our prompt" : "");
    if (ours) CallStop();
    SetState(BayState::Idle);
}

void BayOnFrame() {
    g_frame++;
    if (!g_gameplay || g_state == BayState::Idle || !g_haveTruckPos) return;
    const char* why = "";
    if (LeftBay(&why)) {
        LeaveBay(why);
        return;
    }
    if (g_state == BayState::ServiceShown) {
        if (!g_noSleep && g_cfg.restoreSleepSec > 0 && GetTickCount64() - g_shownAt > (ULONGLONG)g_cfg.restoreSleepSec * 1000) {
            SetState(BayState::BayWatch);
            EnsureSleepPrompt("timeout");
        }
        return;
    }
    if (g_noSleep) {
        // No sleep prompt to go back to: offer service again once the slot is empty.
        int type;
        void* data;
        SlotRead(type, data);
        if (type != 0) return;
        Log("showing service prompt again (service used / slot cleared)");
        g_shownAt = GetTickCount64();
        SetState(BayState::ServiceShown);
        CallStart(kActService, g_serviceItem);
        return;
    }
    EnsureSleepPrompt("service used / slot cleared");
}

// Bays where the game gives no sleep prompt (ATS car mode, Road Trip): after each trigger scan, if the
// vehicle entered an eligible garage's sleep zone and the game put no prompt in the slot within a second,
// the plugin shows the service prompt itself. With a truck the game's sleep prompt comes first and is
// replaced in HookStartActivation as before, so this never runs.
void BayOnTriggerScan(void* gameplay) {
    if (gameplay) g_gameplay = gameplay;
    void* zone = nullptr;
    GarageEntry* garage = nullptr;
    double dist = 0;
    if (g_cfg.enabled && g_game.triggers && (garage = FindEligibleGarage(&dist, false)) != nullptr) {
        for (int i = 0; i < g_numTriggers && !zone; i++) {
            TriggerInfo& t = g_triggers[i];
            if (!t.sleep || TriggerState(t.trig) != 3) continue;
            double dx = PlacementWorld(t.pos, 0) - garage->x, dz = PlacementWorld(t.pos, 2) - garage->z;
            if (dx * dx + dz * dz <= kTriggerToGarage * kTriggerToGarage) zone = t.trig;
        }
    }
    if (zone != g_zone) {
        g_zone = zone;
        g_zoneSince = GetTickCount64();
        g_zoneHandled = false;
    }
    if (!zone || g_zoneHandled || !g_gameplay || g_state != BayState::Idle) return;
    if (GetTickCount64() - g_zoneSince < 1000) return;  // give the game's own sleep prompt time to appear
    g_zoneHandled = true;
    int type;
    void* data;
    SlotRead(type, data);
    if (type != 0) return;  // the game shows some other prompt here; leave it alone
    g_serviceItem = garage->item;
    g_shownAt = GetTickCount64();
    SetState(BayState::ServiceShown);
    g_noSleep = true;
    g_bayTrigger = zone;
    Log("entered garage bay without a sleep prompt -> showing service prompt for garage %p (%.1f m), bay trigger %p",
        garage->item, dist, zone);
    CallStart(kActService, garage->item);
}

}  // namespace sag
