#include "common.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

#include "ini_upgrade.h"

namespace sag {

Config g_cfg;
uintptr_t g_base, g_size;
volatile bool g_passthrough;
static FILE* g_log;

void LogOpen(const std::string& path) { fopen_s(&g_log, path.c_str(), "w"); }

void LogClose() {
    if (g_log) fclose(g_log);
    g_log = nullptr;
}

void Log(const char* fmt, ...) {
    if (!g_log) return;
    fprintf(g_log, "[%10llu] ", GetTickCount64());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static const char kDefaultIni[] =
    "[service_at_garage]\n"
    "; 1 = on, 0 = off\n"
    "enabled=1\n"
    "; 1 = only garages you own\n"
    "require_owned=1\n"
    "; minimum garage level: 1 = tiny, 2 = small, 3 = large (fully upgraded)\n"
    "min_level=1\n"
    "; max distance in metres between truck and garage when the bay prompt appears\n"
    "radius=50\n"
    "; seconds before the sleep prompt comes back if service is not used\n"
    "restore_sleep_sec=10\n"
    "; 1 = show the floating service icon in the garage bay\n"
    "icon=1\n"
    "; metres from the manage-garage icon toward the bay door\n"
    "icon_distance=10.5\n"
    "; metres to move the icon sideways (negative = other side)\n"
    "icon_offset_side=0\n"
    "; 1 = cars (ATS Road Trip) can refuel at your garages' fuel pumps, like trucks\n"
    "car_fuel=1\n"
    "; sleep spots only need your truck inside, not the trailer: 0 = off (game default), 1 = your garages,\n"
    ";   2 = every sleep spot in the game\n"
    "sleep_ignore_trailer=2\n"
    "; 0 = no log file (default). For troubleshooting: 1 = normal, 2 = verbose (dumps nearby triggers)\n"
    "log=0\n";

static float ReadFloat(const char* ini, const char* section, const char* key, float def) {
    char buf[32];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), ini);
    return buf[0] ? (float)atof(buf) : def;
}

// Writes `text` to `path` through a temporary file, so a crash never leaves a half-written ini.
static bool WriteFileAtomic(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        for (char c : text) {  // Windows line endings, as the file always had
            if (c == '\n') f << '\r';
            f << c;
        }
        if (!f.flush()) return false;
    }
    if (MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileA(tmp.c_str());
    return false;
}

std::string LoadConfig(const std::string& iniPath) {
    const char* ini = iniPath.c_str();
    std::string note;
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        WriteFileAtomic(iniPath, kDefaultIni);
    } else {
        // An ini from an older version: add new settings, drop obsolete ones, keep the player's values.
        std::ifstream f(iniPath, std::ios::binary);
        std::stringstream text;
        text << f.rdbuf();
        f.close();
        std::string upgraded;
        std::string changes = UpgradeIni(text.str(), kDefaultIni, upgraded);
        if (!changes.empty())
            note = WriteFileAtomic(iniPath, upgraded) ? "updated service_at_garage.ini: " + changes
                                                      : "could not update service_at_garage.ini (" + changes + ")";
    }
    const char* s = "service_at_garage";
    g_cfg.enabled = GetPrivateProfileIntA(s, "enabled", 1, ini);
    g_cfg.requireOwned = GetPrivateProfileIntA(s, "require_owned", 1, ini);
    g_cfg.minLevel = GetPrivateProfileIntA(s, "min_level", 1, ini);
    g_cfg.radius = ReadFloat(ini, s, "radius", 50.0f);
    g_cfg.restoreSleepSec = GetPrivateProfileIntA(s, "restore_sleep_sec", 10, ini);
    g_cfg.icon = GetPrivateProfileIntA(s, "icon", 1, ini);
    g_cfg.iconDistance = ReadFloat(ini, s, "icon_distance", 10.5f);  // replaces icon_offset_away (v1.0.x), now ignored
    g_cfg.iconOffsetSide = ReadFloat(ini, s, "icon_offset_side", 0.0f);
    g_cfg.carFuel = GetPrivateProfileIntA(s, "car_fuel", 1, ini);
    g_cfg.sleepIgnoreTrailer = GetPrivateProfileIntA(s, "sleep_ignore_trailer", 2, ini);
    g_cfg.log = GetPrivateProfileIntA(s, "log", 0, ini);
    return note;
}

bool SafeWrite(void* dst, const void* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeRead(const void* src, void* dst, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string GameBuildId() {
    auto nt = (IMAGE_NT_HEADERS64*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    char buf[32];
    snprintf(buf, sizeof(buf), "%08X-%X", (unsigned)nt->FileHeader.TimeDateStamp, (unsigned)nt->OptionalHeader.SizeOfImage);
    return buf;
}

bool IsEts2() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"eurotrucks2.exe") == 0;
}

const char* GameName() { return IsEts2() ? "Euro Truck Simulator 2" : "American Truck Simulator"; }

}  // namespace sag
