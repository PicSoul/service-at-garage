#include "world.h"

#include <math.h>
#include <stdio.h>

#include "bay.h"
#include "common.h"
#include "icon.h"
#include "signatures.h"

namespace sag {

uint64_t g_frame;
bool g_haveTruckPos;
double g_truckX, g_truckY, g_truckZ;
double g_truckHeading;
static uint64_t g_placementUpdates;  // SDK truck placement callbacks received
GarageEntry g_garages[32];
TriggerInfo g_triggers[512];
int g_numTriggers;
void(__fastcall* OrigGarageItemUpdate)(void* item);
void(__fastcall* OrigTriggerUpdate)(void* core);
bool(__fastcall* OrigGaragePumpModes)(void* pump, uint32_t modes);

// Trigger object layout (checked at runtime by the scan; a wrong value only disables the icon and the
// exact leave detection, never the service prompt).
const size_t kTriggerListNode = 8;    // list node inside the trigger
const size_t kTriggerState = 0x18;    // 2 = outside, 3 = inside
const size_t kTriggerVPos = 0x20;     // vtable: const placement* position()
const size_t kTriggerVActions = 0x28; // vtable: void actions(view*) -> {first, stride, count}
const size_t kActionName = 0x18, kActionToken = 0x30, kActionParams = 0x40, kActionParamCount = 0x48;

SCSAPI_VOID OnTruckPlacement(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
    if (!value) {
        g_haveTruckPos = false;
        return;
    }
    g_truckX = value->value_dplacement.position.x;
    g_truckY = value->value_dplacement.position.y;
    g_truckZ = value->value_dplacement.position.z;
    g_truckHeading = value->value_dplacement.orientation.heading;
    g_placementUpdates++;
    g_haveTruckPos = true;
}

// ---------------------------------------------------------------- garages

// Map node positions are packed: low 17 bits = local coord in 1/256 m, high bits = 512 m sector.
static bool NodeWorldPos(void* item, double& x, double& y, double& z) {
    void* node = nullptr;
    int32_t n[3];
    if (!Read(item, g_game.itemNode, node) || !node || !SafeRead(node, n, sizeof(n))) return false;
    x = (double)(n[0] >> 17) * 512.0 + (double)(n[0] & 0x1ffff) / 256.0;
    y = (double)n[1] / 256.0;
    z = (double)(n[2] >> 17) * 512.0 + (double)(n[2] & 0x1ffff) / 256.0;
    return true;
}

static bool GarageStatus(void* item, uint32_t& status) {
    void* garage = nullptr;
    int32_t alive = 0;
    if (!Read(item, g_game.itemGarage, garage) || !garage || !Read(garage, 0x8, alive) || alive >= 0) return false;
    return Read(garage, g_game.garageStatus, status);
}

int GarageLevel(uint32_t status) {
    if (status & kGarageTiny) return 1;
    if (status & kGarageLarge) return 3;
    return 2;
}

bool IsFresh(const GarageEntry& g) { return g.item && g_frame - g.lastFrame <= 120; }

bool IsEligible(const GarageEntry& g) {
    if (!g_cfg.requireOwned) return true;
    return g.statusValid && (g.status & kGarageOwned) && GarageLevel(g.status) >= g_cfg.minLevel;
}

GarageEntry* FindGarage(void* item) {
    for (GarageEntry& g : g_garages)
        if (g.item == item) return &g;
    return nullptr;
}

static void RememberGarage(void* item) {
    GarageEntry e = {};
    e.item = item;
    if (!NodeWorldPos(item, e.x, e.y, e.z)) return;
    e.statusValid = GarageStatus(item, e.status);
    e.lastFrame = g_frame;
    int free = -1, oldest = 0;
    for (int i = 0; i < 32; i++) {
        if (g_garages[i].item == item) {
            g_garages[i] = e;
            return;
        }
        if (!g_garages[i].item && free < 0) free = i;
        if (g_garages[i].lastFrame < g_garages[oldest].lastFrame) oldest = i;
    }
    g_garages[free >= 0 ? free : oldest] = e;
}

GarageEntry* FindEligibleGarage(double* outDist, bool log) {
    if (!g_haveTruckPos) return nullptr;
    GarageEntry* best = nullptr;
    double bestD = g_cfg.radius;
    for (GarageEntry& g : g_garages) {
        if (!IsFresh(g)) continue;
        double d = sqrt((g.x - g_truckX) * (g.x - g_truckX) + (g.y - g_truckY) * (g.y - g_truckY) + (g.z - g_truckZ) * (g.z - g_truckZ));
        if (log)
            Log("  garage %p at (%.1f %.1f %.1f) dist=%.1f status=%s0x%x level=%d eligible=%d", g.item, g.x, g.y, g.z, d,
                g.statusValid ? "" : "?", g.status, g.statusValid ? GarageLevel(g.status) : 0, IsEligible(g));
        if (IsEligible(g) && d <= bestD) {
            bestD = d;
            best = &g;
        }
    }
    if (outDist) *outDist = bestD;
    return best;
}

const uint32_t kModeTruck = 1u << 0, kModeCar = 1u << 2;  // vehicle mode bits: truck 0, car 2 (ATS Road Trip)

bool __fastcall HookGaragePumpModes(void* pump, uint32_t modes) {
    bool serves = OrigGaragePumpModes(pump, modes);
    if (serves || g_passthrough || !g_cfg.enabled || !g_cfg.carFuel || !(modes & kModeCar)) return serves;
    // Same answer as for a truck: the game still checks the pump's garage (valid, owned) itself.
    return OrigGaragePumpModes(pump, (modes & ~kModeCar) | kModeTruck);
}

void __fastcall HookGarageItemUpdate(void* item) {
    if (g_passthrough) return OrigGarageItemUpdate(item);
    OrigGarageItemUpdate(item);
    RememberGarage(item);
}

// ---------------------------------------------------------------- triggers

double PlacementWorld(const uint8_t* pos16, int axis) {
    float f[3];
    int16_t sec[2];
    memcpy(f, pos16, 12);
    memcpy(sec, pos16 + 12, 4);
    if (axis == 0) return (double)sec[0] * 512.0 + f[0];
    if (axis == 1) return f[1];
    return (double)sec[1] * 512.0 + f[2];
}

bool TriggerListed(void* trig) {
    for (int i = 0; i < g_numTriggers; i++)
        if (g_triggers[i].trig == trig) return true;
    return false;
}

uint8_t TriggerState(void* trig) {
    uint8_t st = 0;
    Read(trig, kTriggerState, st);
    return st;
}

typedef const void*(__fastcall* TriggerPos_t)(void* trigger);
typedef void(__fastcall* TriggerActions_t)(void* trigger, uint64_t* view);

// Walks the game's list of nearby triggers (doubly linked between head/tail sentinels in the trigger
// manager). Returns the count, or -(count + 1) if a read faulted. POD only (__try).
static int ScanTriggers(void* core, TriggerInfo* out, int max) {
    int n = 0;
    __try {
        char* mgr = *(char**)((char*)core + g_game.triggerManager);
        if (!mgr) return 0;
        char* tail = mgr + g_game.triggerListTail;
        char* node = *(char**)(mgr + g_game.triggerListHead);
        for (int guard = 0; node && node != tail && guard < 8192 && n < max; guard++, node = *(char**)node) {
            TriggerInfo& t = out[n];
            memset(&t, 0, sizeof(t));
            t.trig = node - kTriggerListNode;
            t.vtable = *(uintptr_t*)t.trig;
            const void* pos = ((TriggerPos_t)(*(void**)(t.vtable + kTriggerVPos)))(t.trig);
            if (!pos) continue;
            memcpy(t.pos, pos, 16);
            uint64_t view[8] = {};
            ((TriggerActions_t)(*(void**)(t.vtable + kTriggerVActions)))(t.trig, view);
            char* first = (char*)view[0];
            uint64_t stride = view[1], count = view[2];
            if (first && stride >= 8 && stride <= 64 && count <= 16) {
                for (uint64_t i = 0; i < count && t.nactions < 4; i++) {
                    char* a = *(char**)(first + i * stride);
                    if (!a) continue;
                    int k = t.nactions++;
                    t.token[k] = *(uint64_t*)(a + kActionToken);
                    t.name[k] = *(const char**)(a + kActionName);
                    int64_t np = *(int64_t*)(a + kActionParamCount);
                    t.nparams[k] = (int)np;
                    if (np > 0) t.param0[k] = **(float**)(a + kActionParams);
                    if (t.token[k] == kTokenHudActivate && np > 0 && t.param0[k] == (float)kActSleep) t.sleep = true;
                }
            }
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = -n - 1;
    }
    return n;
}

static void TokenString(uint64_t v, char* out, size_t cap) {
    static const char chars[] = "\0" "0123456789abcdefghijklmnopqrstuvwxyz_";
    size_t i = 0;
    while (v && i + 1 < cap) {
        out[i++] = chars[v % 38];
        v /= 38;
    }
    out[i] = 0;
}

// Verbose log (log=2): each trigger near the truck, once.
static void LogNearbyTriggers() {
    static void* seen[256];
    static int nseen;
    for (int i = 0; i < g_numTriggers; i++) {
        TriggerInfo& t = g_triggers[i];
        double dx = PlacementWorld(t.pos, 0) - g_truckX, dz = PlacementWorld(t.pos, 2) - g_truckZ;
        if (dx * dx + dz * dz > 80.0 * 80.0) continue;
        bool known = false;
        for (int k = 0; k < nseen; k++) known |= seen[k] == t.trig;
        if (known) continue;
        if (nseen < 256) seen[nseen++] = t.trig;
        Log("trigger %p vt=exe+0x%llx state=%u pos=(%.2f %.2f %.2f) dist=%.1f sleep=%d", t.trig,
            (unsigned long long)(t.vtable - g_base), TriggerState(t.trig), PlacementWorld(t.pos, 0), PlacementWorld(t.pos, 1),
            PlacementWorld(t.pos, 2), sqrt(dx * dx + dz * dz), t.sleep);
        for (int k = 0; k < t.nactions; k++) {
            char tok[16], name[48] = "";
            TokenString(t.token[k], tok, sizeof(tok));
            if (t.name[k]) SafeRead(t.name[k], name, sizeof(name) - 1);
            Log("    action %d cmd=%s name='%s' nparams=%d p0=%.2f", k, tok, name, t.nparams[k], t.param0[k]);
        }
    }
}

// ---------------------------------------------------------------- gameplay mode / vehicle mode


// The local gameplay mode (owner of the activation slot) at core+0x2b30, as the game's own
// is_activation_enabled reads it. Null unless its vtable holds the start_activation we hook (+0x190).
void* GameplayMode(void* core) {
    void* gm = nullptr;
    uintptr_t* vt = nullptr;
    uintptr_t start = 0;
    if (!Read(core, 0x2b30, gm) || !gm || !Read(gm, 0, vt) || !vt || !Read(vt, 0x190, start)) return nullptr;
    return start == g_game.startActivation ? gm : nullptr;
}

// Vehicle mode (gameplay mode virtual +0xe0): 0 = truck, 2 = car (ATS Road Trip). -1 = unknown. POD only (__try).
static int VehicleMode(void* gm) {
    if (!gm) return -1;
    __try {
        typedef int(__fastcall * Fn)(void*);
        return ((Fn)(*(void***)gm)[0xe0 / 8])(gm);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Log (log=1): vehicle mode changes, and entering/leaving sleep zones with the SDK position at that moment.
static void LogVehicleDiagnostics(void* gm) {
    static int lastMode = -2;
    static void* lastInside = nullptr;
    static uint64_t lastUpdates;
    int mode = VehicleMode(gm);
    if (mode != lastMode) {
        Log("vehicle mode %d (0 = truck, 2 = car)", mode);
        lastMode = mode;
    }
    void* inside = nullptr;
    for (int i = 0; i < g_numTriggers && !inside; i++)
        if (g_triggers[i].sleep && TriggerState(g_triggers[i].trig) == 3) inside = g_triggers[i].trig;
    if (inside != lastInside) {
        GarageEntry* nearest = nullptr;
        double best = 1e18;
        for (GarageEntry& g : g_garages) {
            if (!IsFresh(g)) continue;
            double d = (g.x - g_truckX) * (g.x - g_truckX) + (g.z - g_truckZ) * (g.z - g_truckZ);
            if (d < best) best = d, nearest = &g;
        }
        Log("%s sleep zone %p (mode %d); SDK position (%.1f %.1f %.1f), %llu SDK updates since last check, "
            "nearest garage %p %.1f m",
            inside ? "entered" : "left", inside ? inside : lastInside, mode, g_truckX, g_truckY, g_truckZ,
            (unsigned long long)(g_placementUpdates - lastUpdates), nearest ? nearest->item : nullptr, nearest ? sqrt(best) : -1.0);
        lastInside = inside;
        lastUpdates = g_placementUpdates;
    }
}

// ---------------------------------------------------------------- sleep zones: truck only (sleep_ignore_trailer)
// A trigger's flags say which vehicle parts its "inside" check looks at. Sleep zones use truck + connected
// trailers, all fully inside the area, which makes parking a rig in a tight bay fiddly. Clearing the trailer
// bit makes the zone check the truck only. Only triggers whose single action is the sleep prompt are touched.
// The game rebuilds triggers from map data when sectors reload, so nothing persists.

const uint32_t kTrigCheckTruck = 0x4, kTrigCheckTrailers = 0x8;

struct ChangedTrigger {
    void* trig;
    uintptr_t vtable;
    uint8_t pos[16];
};
static ChangedTrigger g_changed[128];
static int g_numChanged;

static bool NearEligibleGarage(const TriggerInfo& t) {
    double x = PlacementWorld(t.pos, 0), z = PlacementWorld(t.pos, 2);
    for (GarageEntry& g : g_garages) {
        if (!IsFresh(g) || !IsEligible(g)) continue;
        if ((x - g.x) * (x - g.x) + (z - g.z) * (z - g.z) <= kTriggerToGarage * kTriggerToGarage) return true;
    }
    return false;
}

static bool WantsTruckOnly(const TriggerInfo& t) {
    if (!g_cfg.enabled || !t.sleep || t.nactions != 1) return false;
    if (g_cfg.sleepIgnoreTrailer == 2) return true;
    return g_cfg.sleepIgnoreTrailer == 1 && NearEligibleGarage(t);
}

static TriggerInfo* FindListed(const ChangedTrigger& c) {
    for (int i = 0; i < g_numTriggers; i++) {
        TriggerInfo& t = g_triggers[i];
        if (t.trig == c.trig && t.vtable == c.vtable && memcmp(t.pos, c.pos, sizeof(c.pos)) == 0) return &t;
    }
    return nullptr;
}

static bool IsChanged(void* trig) {
    for (int i = 0; i < g_numChanged; i++)
        if (g_changed[i].trig == trig) return true;
    return false;
}

static void SetTrailerBit(void* trig, bool on, const char* why) {
    uint32_t f = 0;
    char* p = (char*)trig + g_game.triggerFlags;
    if (!SafeRead(p, &f, sizeof(f))) return;
    uint32_t nf = on ? f | kTrigCheckTrailers : f & ~kTrigCheckTrailers;
    if (nf != f && SafeWrite(p, &nf, sizeof(nf)) && g_cfg.log >= 2)
        Log("sleep zone %p: %s (flags 0x%X -> 0x%X)", trig, why, f, nf);
}

static void UpdateSleepZones() {
    // Undo or forget earlier changes. A trigger is only written while it is in the game's list with the same
    // vtable and position as when it was changed (so never a freed or reused object).
    for (int i = 0; i < g_numChanged;) {
        TriggerInfo* t = FindListed(g_changed[i]);
        if (t && !WantsTruckOnly(*t)) SetTrailerBit(t->trig, true, "trailer check restored");
        if (!t || !WantsTruckOnly(*t)) {
            g_changed[i] = g_changed[--g_numChanged];
            continue;
        }
        i++;
    }
    for (int i = 0; i < g_numTriggers && g_numChanged < (int)(sizeof(g_changed) / sizeof(g_changed[0])); i++) {
        TriggerInfo& t = g_triggers[i];
        if (!WantsTruckOnly(t) || IsChanged(t.trig)) continue;
        uint32_t f = 0;
        if (!Read(t.trig, g_game.triggerFlags, f)) continue;
        if (!(f & kTrigCheckTruck) || !(f & kTrigCheckTrailers)) continue;  // never leave a zone checking nothing
        ChangedTrigger& c = g_changed[g_numChanged++];
        c.trig = t.trig;
        c.vtable = t.vtable;
        memcpy(c.pos, t.pos, sizeof(c.pos));
        SetTrailerBit(t.trig, false, "trailer no longer has to fit");
    }
}

void __fastcall HookTriggerUpdate(void* core) {
    if (g_passthrough) return OrigTriggerUpdate(core);
    OrigTriggerUpdate(core);
    if (!g_cfg.enabled) return;
    if (g_frame % 15 == 0) {
        int n = ScanTriggers(core, g_triggers, 512);
        if (n < 0) {
            n = -n - 1;
            Log("trigger scan faulted after %d triggers", n);
        }
        g_numTriggers = n;
        if (g_cfg.log >= 2 && g_haveTruckPos) LogNearbyTriggers();
        void* gm = GameplayMode(core);
        if (g_cfg.log >= 1) LogVehicleDiagnostics(gm);
        BayOnTriggerScan(gm);
        if (g_game.sleepTrailer) UpdateSleepZones();
        IconSelectTarget();
    }
    IconUpdate();
}

}  // namespace sag
