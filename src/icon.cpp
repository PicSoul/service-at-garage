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

// Rotation of a map node as a yaw angle (radians). The node stores a quaternion (w, x, y, z).
static bool NodeYaw(void* item, float q[4], double& yaw) {
    void* node = nullptr;
    if (!Read(item, g_game.itemNode, node) || !node || !SafeRead((char*)node + kNodeRotation, q, 4 * sizeof(float)))
        return false;
    yaw = 2.0 * atan2((double)q[2], (double)q[0]);
    return true;
}

// World x/z offset from the garage node -> garage-local (right, forward) for a node rotated by yaw about +y.
static void ToLocal(double yaw, double dx, double dz, double& lx, double& lz) {
    double c = cos(yaw), s = sin(yaw);
    lx = dx * c - dz * s;
    lz = dx * s + dz * c;
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
            // The garage node is the manage-garage icon, at the back of the entrance bay; the node's forward axis
            // runs down that bay to the door. Place ours along it, so it stays in that bay however large the
            // garage's sleep area is (a multi-bay garage can have one sleep area for all its bays).
            double yaw = 0;
            if (!NodeYaw(g.item, best.rot, yaw)) {
                best.rot[0] = 1.0f;
                best.rot[1] = best.rot[2] = best.rot[3] = 0.0f;
            }
            double fx = sin(yaw), fz = cos(yaw);  // forward (toward the door); left = (-fz, fx)
            SetPlacementWorld(best, g.x + fx * g_cfg.iconDistance - fz * g_cfg.iconOffsetSide,
                              g.z + fz * g_cfg.iconDistance + fx * g_cfg.iconOffsetSide);
            best.y = (float)g.y;
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

void IconLogGeometry(void* garageItem, const char* why) {
    if (g_cfg.log < 2) return;
    GarageEntry* g = garageItem ? FindGarage(garageItem) : nullptr;
    if (!g) {  // nearest known garage
        double best = 1e18;
        for (GarageEntry& e : g_garages) {
            if (!IsFresh(e)) continue;
            double d = (e.x - g_truckX) * (e.x - g_truckX) + (e.z - g_truckZ) * (e.z - g_truckZ);
            if (d < best) best = d, g = &e;
        }
    }
    if (!g) {
        Log("geometry (%s): no garage nearby", why);
        return;
    }
    float q[4] = {};
    double yaw = 0;
    bool haveYaw = NodeYaw(g->item, q, yaw);
    Log("geometry (%s): garage %p node (%.2f %.2f %.2f) quat w=%.4f x=%.4f y=%.4f z=%.4f yaw=%.1f deg", why, g->item, g->x,
        g->y, g->z, q[0], q[1], q[2], q[3], haveYaw ? yaw * 57.29578 : 0.0);
    double lx, lz;
    ToLocal(yaw, g_truckX - g->x, g_truckZ - g->z, lx, lz);
    Log("  truck (%.2f %.2f %.2f) heading=%.1f deg, garage-local right=%.2f fwd=%.2f", g_truckX, g_truckY, g_truckZ,
        g_truckHeading * 360.0, lx, lz);
    for (int i = 0; i < g_numTriggers; i++) {
        TriggerInfo& t = g_triggers[i];
        if (!t.sleep) continue;
        double x = PlacementWorld(t.pos, 0), z = PlacementWorld(t.pos, 2);
        double dx = x - g->x, dz = z - g->z;
        if (dx * dx + dz * dz > 80.0 * 80.0) continue;
        ToLocal(yaw, dx, dz, lx, lz);
        Log("  sleep trigger %p center (%.2f %.2f) dist=%.1f state=%u garage-local right=%.2f fwd=%.2f", t.trig, x, z,
            sqrt(dx * dx + dz * dz), TriggerState(t.trig), lx, lz);
    }
    if (g_iconValid && g_iconGarage == g->item) {
        ToLocal(yaw, g_iconPlacement.sx * 512.0 + g_iconPlacement.x - g->x, g_iconPlacement.sz * 512.0 + g_iconPlacement.z - g->z,
                lx, lz);
        Log("  service icon garage-local right=%.2f fwd=%.2f", lx, lz);
    }
}

void IconOnPause(bool paused) {
    g_paused = paused;
    if (paused) ReleaseMarker("game paused");
}

void __fastcall HookGarageItemDraw(void* item, void* ctx) {
    if (g_passthrough) return OrigGarageItemDraw(item, ctx);
    OrigGarageItemDraw(item, ctx);
    // Like a real service station, hide the icon while its prompt is active.
    if (item == g_iconGarage && g_iconValid && g_marker.inst && BayIdle()) ((MarkerDraw_t)g_game.markerDraw)(&g_marker, ctx);
}

}  // namespace sag
