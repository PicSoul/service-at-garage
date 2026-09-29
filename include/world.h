#pragma once

#include <stdint.h>

#include "scssdk_telemetry.h"

namespace sag {

// Fixed game constants (stable across versions; not worth a signature each).
const int kActService = 1;  // hud activation type: service
const int kActSleep = 3;    // hud activation type: parking / sleep (item is null)
const int kActManageGarage = 9;  // hud activation type: manage garage (item is the garage item)
const uint64_t kTokenHudActivate = 0x344dc5bd6c3df4ecull;  // trigger command "hud_activate"
const uint32_t kGarageOwned = 0x2, kGarageTiny = 0x4, kGarageLarge = 0x1;  // garage_u status bits

extern uint64_t g_frame;

// ---- truck position (telemetry channel)
extern bool g_haveTruckPos;
extern double g_truckX, g_truckY, g_truckZ;
extern double g_truckHeading;  // SDK heading: 0..1 of a full turn
SCSAPI_VOID OnTruckPlacement(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t);

// ---- garages near the player (fed by the garage item update hook)
struct GarageEntry {
    void* item;          // garage map item (also draws the manage-garage icon)
    double x, y, z;      // world position of its node
    uint32_t status;     // garage_u status bits
    bool statusValid;
    uint64_t lastFrame;  // last frame the game updated it
};
extern GarageEntry g_garages[32];
bool IsFresh(const GarageEntry& g);
bool IsEligible(const GarageEntry& g);  // passes the ownership / level settings
int GarageLevel(uint32_t status);       // 1 tiny, 2 small, 3 large
GarageEntry* FindGarage(void* item);
GarageEntry* FindEligibleGarage(double* outDist);  // nearest eligible garage within the radius

// ---- triggers near the player (scanned from the trigger update hook)
struct TriggerInfo {
    void* trig;
    uintptr_t vtable;
    uint8_t pos[16];  // float x, y, z local + int16 sector x, z (area center, ~1 m above ground)
    int nactions;
    uint64_t token[4];
    const char* name[4];
    int nparams[4];
    float param0[4];
    bool sleep;  // hud_activate with parameter 3: a parking/sleep zone (the garage bay)
};
extern TriggerInfo g_triggers[512];
extern int g_numTriggers;
double PlacementWorld(const uint8_t* pos16, int axis);  // 0 = x, 1 = y, 2 = z
bool TriggerListed(void* trig);
uint8_t TriggerState(void* trig);  // live: 3 = truck inside, 2 = outside, 0 = unknown

// Hooks installed by main.cpp
void __fastcall HookGarageItemUpdate(void* item);
void __fastcall HookTriggerUpdate(void* core);
extern void(__fastcall* OrigGarageItemUpdate)(void* item);
extern void(__fastcall* OrigTriggerUpdate)(void* core);

}  // namespace sag
