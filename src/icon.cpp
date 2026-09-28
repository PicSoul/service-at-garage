#include "icon.h"

#include <math.h>

#include "bay.h"
#include "common.h"
#include "signatures.h"
#include "world.h"

namespace sag {

void(__fastcall* OrigGarageItemDraw)(void* item, void* ctx);

struct Marker {
    void* inst;
    void* params;
};
// Same layout the game builds from a map node: local position, 512 m sector, rotation.
struct Placement {
    float x, y, z;
    int16_t sx, sz;
    float rot[4];
};
typedef void(__fastcall* MarkerCreate_t)(Marker* m, const void* modelPathRef, const void* animPathRef);
typedef void(__fastcall* MarkerPlace_t)(Marker* m, const Placement* p);
typedef void(__fastcall* MarkerDraw_t)(Marker* m, void* renderCtx);
typedef void(__fastcall* MarkerRelease_t)(Marker* m);

const double kTriggerToGarage = 40.0;  // a bay's sleep trigger lies within this distance of its garage item
const size_t kNodeRotation = 0x10;     // node: packed position, then rotation quaternion

static Marker g_marker;
static bool g_markerCreated;
static bool g_paused;
static bool g_iconValid;
static Placement g_iconPlacement;
static void* g_iconGarage;

static bool Enabled() { return g_game.icon && g_cfg.enabled && g_cfg.icon; }

// Writes a world x/z position as 512 m sector + local offset.
static void SetPlacementWorld(Placement& p, double x, double z) {
    double sx = floor(x / 512.0), sz = floor(z / 512.0);
    p.sx = (int16_t)sx;
    p.sz = (int16_t)sz;
    p.x = (float)(x - sx * 512.0);
    p.z = (float)(z - sz * 512.0);
}

void IconSelectTarget() {
    if (!Enabled()) return;
    bool found = false;
    double bestTruck = 1e18;
    Placement best = {};
    void* bestGarage = nullptr;
    for (GarageEntry& g : g_garages) {
        if (!IsFresh(g) || !IsEligible(g)) continue;
        for (int i = 0; i < g_numTriggers; i++) {
            TriggerInfo& t = g_triggers[i];
            if (!t.sleep) continue;
            double x = PlacementWorld(t.pos, 0), z = PlacementWorld(t.pos, 2);
            if ((x - g.x) * (x - g.x) + (z - g.z) * (z - g.z) > kTriggerToGarage * kTriggerToGarage) continue;
            double dt = (x - g_truckX) * (x - g_truckX) + (z - g_truckZ) * (z - g_truckZ);
            if (dt >= bestTruck) continue;
            bestTruck = dt;
            memset(&best, 0, sizeof(best));
            // The garage item is the manage-garage icon; move ours away from it (and sideways).
            double ax = x - g.x, az = z - g.z, len = sqrt(ax * ax + az * az);
            if (len > 0.01) {
                ax /= len;
                az /= len;
            } else {
                ax = 1.0;
                az = 0.0;
            }
            SetPlacementWorld(best, x + ax * g_cfg.iconOffsetAway - az * g_cfg.iconOffsetSide,
                              z + az * g_cfg.iconOffsetAway + ax * g_cfg.iconOffsetSide);
            best.y = (float)g.y;  // trigger centers sit ~1 m above the bay floor; the garage node is on it
            void* node = nullptr;
            if (!Read(g.item, g_game.itemNode, node) || !node || !SafeRead((char*)node + kNodeRotation, best.rot, sizeof(best.rot)))
                best.rot[0] = 1.0f;
            bestGarage = g.item;
            found = true;
        }
    }
    if (found && (!g_iconValid || g_iconGarage != bestGarage || memcmp(&best, &g_iconPlacement, 16) != 0))
        Log("service icon -> garage %p at (%.2f %.2f %.2f)", bestGarage, (double)best.sx * 512.0 + best.x, (double)best.y,
            (double)best.sz * 512.0 + best.z);
    if (!found && g_iconValid) Log("service icon hidden (no eligible garage bay nearby)");
    if (found) {
        g_iconPlacement = best;
        g_iconGarage = bestGarage;
    }
    g_iconValid = found;
}

// Drops the marker's model references. Must happen before the game tears down its resources (holding
// them froze the game on exit), so it runs on pause and whenever no icon is needed.
static void ReleaseMarker(const char* why) {
    if (!g_markerCreated) return;
    if (g_marker.inst || g_marker.params) ((MarkerRelease_t)g_game.markerRelease)(&g_marker);
    g_marker.inst = g_marker.params = nullptr;
    g_markerCreated = false;
    Log("service icon released (%s)", why);
}

void IconUpdate() {
    if (!Enabled()) return;
    if (!g_iconValid) {
        ReleaseMarker("no icon needed");
        return;
    }
    if (g_paused) return;
    if (!g_markerCreated) {
        g_markerCreated = true;
        ((MarkerCreate_t)g_game.markerCreate)(&g_marker, (void*)g_game.serviceIconModel, (void*)g_game.serviceIconAnim);
        Log("service icon created (inst %p)", g_marker.inst);
    }
    if (g_marker.inst) ((MarkerPlace_t)g_game.markerPlace)(&g_marker, &g_iconPlacement);
}

void IconOnPause(bool paused) {
    g_paused = paused;
    if (paused) ReleaseMarker("game paused");
}

void __fastcall HookGarageItemDraw(void* item, void* ctx) {
    OrigGarageItemDraw(item, ctx);
    // Like a real service station, hide the icon while its prompt is active.
    if (item == g_iconGarage && g_iconValid && g_marker.inst && BayIdle()) ((MarkerDraw_t)g_game.markerDraw)(&g_marker, ctx);
}

}  // namespace sag
