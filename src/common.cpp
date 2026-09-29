#include "common.h"

#include <stdio.h>
#include <stdlib.h>

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
    "; 0 = no log file (default). For troubleshooting: 1 = normal, 2 = verbose (dumps nearby triggers)\n"
    "log=0\n";

static float ReadFloat(const char* ini, const char* section, const char* key, float def) {
    char buf[32];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), ini);
    return buf[0] ? (float)atof(buf) : def;
}

void LoadConfig(const std::string& iniPath) {
    const char* ini = iniPath.c_str();
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        fopen_s(&f, ini, "w");
        if (f) {
            fputs(kDefaultIni, f);
            fclose(f);
        }
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
    g_cfg.log = GetPrivateProfileIntA(s, "log", 0, ini);
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
