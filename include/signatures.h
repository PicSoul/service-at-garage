#pragma once

#include <stdint.h>
#include <string>

namespace sag {

// Everything the plugin needs from the game executable, found at startup from code signatures.
// Nothing here is hard-coded per game build: functions are the signature match itself, struct offsets
// and data addresses are read from operands inside the matched code.
struct GameLayout {
    // Functions
    uintptr_t startActivation;    // local_gameplay_mode_u::start_activation(type, item)
    uintptr_t stopActivation;     // local_gameplay_mode_u::stop_activation()
    uintptr_t performActivation;  // runs the active prompt (Enter was pressed)
    uintptr_t garageItemUpdate;   // garage map item, per frame while near the player
    uintptr_t garageItemDraw;     // garage map item draw method (item, render context)
    uintptr_t triggerUpdate;      // per-frame enter/leave check over all nearby triggers (arg: game core)
    uintptr_t markerCreate;       // activation marker (floating icon): create(marker, &model path, &anim path)
    uintptr_t markerPlace;        //   place(marker, placement)
    uintptr_t markerDraw;         //   draw(marker, render context)
    uintptr_t markerRelease;      //   destructor (drops the model references)
    uintptr_t garagePumpModes;    // garage fuel pump: serves(pump, vehicle mode bits) -> bool

    // Data: the service entries (index 1) of the activation model / animation path tables
    uintptr_t serviceIconModel;
    uintptr_t serviceIconAnim;

    // Struct offsets
    uint32_t slotType, slotItem;   // activation slot in local_gameplay_mode_u
    uint32_t itemNode;             // map item -> node (packed position + rotation)
    uint32_t itemGarage;           // garage item -> garage_u
    uint32_t garageStatus;         // garage_u status bits
    uint32_t triggerManager;       // game core -> trigger manager
    uint32_t triggerListHead;      // trigger manager -> list head / tail sentinels
    uint32_t triggerListTail;
    uint32_t triggerFlags;         // trigger -> flags: which vehicle parts must be inside (see world.cpp)

    // Which features have everything they need
    bool core;      // service prompt in the bay
    bool triggers;  // trigger list (exact bay leave detection, icon placement)
    bool icon;      // floating service icon
    bool carFuel;   // refuelling at garage pumps in car mode (ATS Road Trip)
    bool sleepTrailer;  // sleep zones that only need the truck inside (sleep_ignore_trailer)
};
extern GameLayout g_game;

// Resolves every signature. [Signatures] entries in the ini replace the built-in ones only when that
// section's GameBuild equals `buildId`. `summary` receives a one-line result for the console.
void ResolveSignatures(const std::string& iniPath, const std::string& buildId, std::string& summary);

}  // namespace sag
