// Offline test of the plugin's signature resolution: maps a game executable as an image (without running
// it) and runs the plugin's own ResolveSignatures() against it, with and without ini overrides.
//   sig_test.exe <amtrucks.exe|eurotrucks2.exe> [...]     or the paths one per line on stdin

#include <stdio.h>
#include <windows.h>

#include <string>

#include "common.h"
#include "signatures.h"

using namespace sag;

static int g_failures;

static void Expect(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failures++;
}

static std::string TempIni(const char* content) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "sag", 0, path);
    FILE* f = fopen(path, "w");
    fputs(content, f);
    fclose(f);
    return path;
}

static void TestExe(const char* exe) {
    printf("%s\n", exe);
    HMODULE m = LoadLibraryExA(exe, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) {
        printf("  could not map (error %lu)\n", GetLastError());
        g_failures++;
        return;
    }
    g_base = (uintptr_t)m;
    g_size = ((IMAGE_NT_HEADERS64*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->OptionalHeader.SizeOfImage;
    std::string build = GameBuildId(), summary;
    printf("  build %s\n", build.c_str());

    // 1. Built-in signatures only
    std::string empty = TempIni("[service_at_garage]\n");
    ResolveSignatures(empty, build, summary);
    printf("  %s\n", summary.c_str());
    Expect(g_game.core && g_game.triggers && g_game.icon, "built-in signatures: all features available");
    Expect(g_game.slotType == 0x14 && g_game.slotItem == 0x18, "activation slot offsets read from code");
    Expect(g_game.itemNode == 0x48 && g_game.itemGarage == 0x90 && g_game.garageStatus == 0x88, "garage offsets read from code");
    Expect(g_game.triggerManager == 0x958 && g_game.triggerListTail == g_game.triggerListHead + 0x10, "trigger list offsets");
    Expect(g_game.serviceIconModel && g_game.serviceIconAnim, "service icon table entries verified by path");
    uintptr_t start = g_game.startActivation;

    // 2. Override for this build: a longer (still unique) StartActivation pattern must be used
    std::string ini = "[Signatures]\nGameBuild=" + build +
                      "\nStartActivation=40 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 00 EB FF FF B8 00 16 00 00 E8 ? ? ? ? "
                      "48 2B E0 83 79 ? 00 4D 8B F0 8B FA 48 8B F1 74 ? 48 8B 01 FF 90 ? ? ? ? 45 33 FF 4C 89 76 ? 48 8D 0D\n";
    std::string over = TempIni(ini.c_str());
    ResolveSignatures(over, build, summary);
    printf("  %s\n", summary.c_str());
    Expect(summary.find("using ini repairs") != std::string::npos && g_game.startActivation == start,
           "ini override for this build is used");

    // 3. Override stamped with another build must be ignored, even if it is broken
    std::string stale = TempIni("[Signatures]\nGameBuild=00000000-0\nStartActivation=DE AD BE EF\n");
    ResolveSignatures(stale, build, summary);
    Expect(g_game.core && g_game.startActivation == start, "override for another game build is ignored");

    // 4. A broken override for this build disables the plugin instead of guessing
    std::string broken = TempIni(("[Signatures]\nGameBuild=" + build + "\nStartActivation=DE AD BE EF 00 11\n").c_str());
    ResolveSignatures(broken, build, summary);
    printf("  %s\n", summary.c_str());
    Expect(!g_game.core, "a signature that no longer matches makes the plugin inactive");

    DeleteFileA(empty.c_str());
    DeleteFileA(over.c_str());
    DeleteFileA(stale.c_str());
    DeleteFileA(broken.c_str());
    FreeLibrary(m);
}

int main(int argc, char** argv) {
    char log[MAX_PATH];
    GetTempPathA(MAX_PATH, log);
    strcat_s(log, "sag_sig_test.log");
    LogOpen(log);
    if (argc > 1) {
        for (int i = 1; i < argc; i++) TestExe(argv[i]);
    } else {  // one executable path per line on stdin (update_check.py --print-exe)
        char line[MAX_PATH];
        while (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0]) TestExe(line);
        }
    }
    printf("%s (%d failure%s). Detailed log: %s\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s", log);
    LogClose();
    return g_failures ? 1 : 0;
}
