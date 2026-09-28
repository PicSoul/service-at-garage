#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>

namespace sag {

// Settings from service_at_garage.ini ([service_at_garage] section).
struct Config {
    int enabled = 1;
    int requireOwned = 1;         // only garages the player owns
    int minLevel = 1;             // 1 tiny, 2 small, 3 large (fully upgraded)
    float radius = 50.0f;         // max truck distance to the garage item when the bay prompt appears, metres
    int restoreSleepSec = 10;     // give the sleep prompt back if service is not used
    int icon = 1;                 // show the floating service icon in the bay
    float iconOffsetAway = 4.0f;  // metres from the sleep area center, away from the manage-garage icon
    float iconOffsetSide = 0.0f;  // metres sideways
    int log = 1;                  // 0 off, 1 normal, 2 verbose (dumps nearby triggers)
};
extern Config g_cfg;

// Loaded game executable (amtrucks.exe / eurotrucks2.exe).
extern uintptr_t g_base, g_size;

void LogOpen(const std::string& path);
void LogClose();
void Log(const char* fmt, ...);

// Reads the ini, writing one with the defaults first if it does not exist.
void LoadConfig(const std::string& iniPath);

// Exception-guarded reads of game memory.
bool SafeRead(const void* src, void* dst, size_t n);
template <typename T>
bool Read(const void* base, size_t off, T& out) {
    return base && SafeRead((const char*)base + off, &out, sizeof(T));
}

// Identifies one build of the game executable: PE link timestamp + image size, e.g. "6AB3A5D4-3973000".
// Signature overrides in the ini are stamped with it. tools/update_check.py computes the same string.
std::string GameBuildId();
bool IsEts2();
const char* GameName();

}  // namespace sag
