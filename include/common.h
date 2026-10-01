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
    float iconDistance = 10.5f;   // metres from the manage-garage icon toward the bay door
    float iconOffsetSide = 0.0f;  // metres sideways
    int carFuel = 1;              // cars (ATS Road Trip) can refuel at garage pumps
    int sleepIgnoreTrailer = 2;   // sleep zones need only the truck inside: 0 off, 1 your garages, 2 everywhere
    int log = 0;                  // 0 off (no log file), 1 normal, 2 verbose (dumps nearby triggers)
};
extern Config g_cfg;

// Loaded game executable (amtrucks.exe / eurotrucks2.exe).
extern uintptr_t g_base, g_size;

// Set at shutdown: every hook just calls the original game function. A hook that another plugin has
// chained on top of cannot be removed safely, so it stays installed in this state.
extern volatile bool g_passthrough;

void LogOpen(const std::string& path);
void LogClose();
void Log(const char* fmt, ...);

// Reads the ini, writing one with the defaults first if it does not exist, or bringing an older one up to
// date (new settings added, obsolete ones removed, the player's values kept). Returns what was changed in
// the file ("" if nothing), for the log.
std::string LoadConfig(const std::string& iniPath);

// Exception-guarded reads of game memory.
bool SafeRead(const void* src, void* dst, size_t n);
bool SafeWrite(void* dst, const void* src, size_t n);
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
