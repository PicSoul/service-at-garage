#include "signatures.h"

#include <vector>

#include "common.h"

namespace sag {

GameLayout g_game;

// Built-in signatures for ATS & ETS2 1.61 (identical in both games).
// Each literal carries a "SAGSIG:<name>=" marker so tools/update_check.py can read the signatures a DLL was
// built with straight from service_at_garage.dll; the marker is stripped before use.
// Every signature must match exactly once, except the data-only ones marked sameValues, which may match
// several identical copies of the same code as long as they all give the same operand values.
struct Signature {
    const char* marked;
    bool sameValues;
};
static const Signature kSignatures[] = {
    {"SAGSIG:StartActivation=40 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 00 EB FF FF B8 00 16 00 00 E8 ? ? ? ? "
     "48 2B E0 83 79 ? 00 4D 8B F0 8B FA 48 8B F1 74 ? 48 8B 01 FF 90 ? ? ? ? 45 33 FF 4C 89 76 ?", false},
    {"SAGSIG:StopActivation=40 53 48 83 EC 20 48 8B D9 48 8B 89 A8 00 00 00 48 85 C9 74 ? E8 ? ? ? ?", false},
    {"SAGSIG:PerformActivation=40 55 41 54 48 8D AC 24 B8 FB FF FF 48 81 EC 48 05 00 00 83 79 ? 00", false},
    {"SAGSIG:GarageItemUpdate=40 55 41 54 48 81 EC A8 00 00 00 4C 8B 41 ?", false},
    {"SAGSIG:GarageItemLayout=48 8D 8D ? ? ? ? 0F 5B C0 F3 0F 59 C6 F3 0F 11 44 24 38 41 0F 10 40 10 0F 11 44 24 "
     "40 E8 ? ? ? ? 48 8B 85 ? ? ? ?", false},
    {"SAGSIG:GarageItemDraw=48 8B C4 48 89 70 20 41 56 48 83 EC 70", false},
    {"SAGSIG:GarageStatus=8B 81 ? ? ? ? 4C 8B D2 44 8B C0", false},
    {"SAGSIG:TriggerUpdate=48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 57 48 83 EC 20 48 8B D9 48 8B "
     "89 68 09 00 00 E8 ? ? ? ? 48 8B AB ? ? ? ? 45 33 E4 48 8B 5D ? 41 BF 18 00 00 00 48 8D 75 10 48 8D 7D ?", false},
    {"SAGSIG:IconTables=4C 8D 81 ? ? ? ? 4C 03 C0 83 FF 0A 72 ? 48 8D 15 ? ? ? ? EB ? 48 8D 91 ? ? ? ?", true},
    {"SAGSIG:MarkerCreate=48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 40 4D 8B E0", false},
    {"SAGSIG:MarkerPlace=48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 01 48 8B FA 48 8B 35 ? ? ? ? 48 8B D9 "
     "44 8B 80 ? ? ? ?", false},
    {"SAGSIG:MarkerDraw=48 89 5C 24 08 57 48 83 EC 30 48 8B 01 48 8B DA 48 8B F9", false},
    {"SAGSIG:MarkerRelease=48 89 5C 24 08 57 48 83 EC 20 48 8B D9 BF FF FF FF FF 48 8B 49 08", false},
};

// Operands read from a match: byte position and size inside the signature. Must match
// tools/update_check.py (repaired signatures keep these positions).
struct Operand {
    const char* sig;
    const char* name;
    int pos, size;
};
static const Operand kOperands[] = {
    {"StartActivation", "slot_type", 34, 1},     {"StartActivation", "stop_vslot", 51, 4},
    {"StartActivation", "slot_item", 61, 1},     {"PerformActivation", "slot_type_2", 21, 1},
    {"GarageItemUpdate", "item_node", 14, 1},    {"GarageItemLayout", "item_marker", 3, 4},
    {"GarageItemLayout", "item_garage", 38, 4},  {"GarageStatus", "garage_status", 2, 4},
    {"TriggerUpdate", "trigger_manager", 42, 4}, {"TriggerUpdate", "trigger_list_head", 52, 1},
    {"TriggerUpdate", "trigger_list_tail", 66, 1}, {"IconTables", "icon_anim_table", 3, 4},
    {"IconTables", "icon_model_table", 27, 4},   {"MarkerPlace", "marker_flags", 34, 4},
};

static const int kMaxSig = sizeof(kSignatures) / sizeof(kSignatures[0]);

struct Resolved {
    std::string name;
    std::string pattern;
    bool fromIni = false;
    uintptr_t address = 0;  // match (0 = not resolved)
};
static Resolved g_resolved[kMaxSig];

static uintptr_t g_text, g_textSize;

static void FindText() {
    auto nt = (IMAGE_NT_HEADERS64*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 6) == 0) {
            g_text = g_base + sec->VirtualAddress;
            g_textSize = sec->Misc.VirtualSize;
            return;
        }
    }
    g_text = g_base;
    g_textSize = g_size;
}

static bool Parse(const std::string& s, std::vector<int>& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') i++;
        if (i >= s.size()) break;
        size_t j = s.find(' ', i);
        std::string tok = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? s.size() : j;
        if (tok == "?" || tok == "??") {
            out.push_back(-1);
        } else {
            char* end = nullptr;
            unsigned long v = strtoul(tok.c_str(), &end, 16);
            if (*end || v > 0xFF) return false;
            out.push_back((int)v);
        }
    }
    return !out.empty();
}

// Up to `max` match addresses of a pattern in .text.
static int Scan(const std::vector<int>& pat, uintptr_t* hits, int max) {
    const uint8_t* p = (const uint8_t*)g_text;
    size_t n = pat.size(), end = g_textSize - n;
    int first = pat[0], count = 0;
    for (size_t i = 0; i <= end && count < max; i++) {
        if (first >= 0) {
            const void* f = memchr(p + i, first, end - i + 1);
            if (!f) break;
            i = (const uint8_t*)f - p;
        }
        size_t k = 1;
        while (k < n && (pat[k] < 0 || p[i + k] == pat[k])) k++;
        if (k == n) hits[count++] = (uintptr_t)(p + i);
    }
    return count;
}

static uint32_t OperandAt(uintptr_t match, int pos, int size) {
    uint32_t v = 0;
    SafeRead((const void*)(match + pos), &v, size);
    return v;
}

static bool SameOperands(const std::string& name, const uintptr_t* hits, int n) {
    for (const Operand& o : kOperands) {
        if (name != o.sig) continue;
        for (int i = 1; i < n; i++)
            if (OperandAt(hits[i], o.pos, o.size) != OperandAt(hits[0], o.pos, o.size)) return false;
    }
    return true;
}

static uintptr_t Match(const char* name) {
    for (const Resolved& r : g_resolved)
        if (r.name == name) return r.address;
    return 0;
}

static bool Op(const char* name, uint32_t& out) {
    for (const Operand& o : kOperands) {
        if (strcmp(o.name, name) != 0) continue;
        uintptr_t m = Match(o.sig);
        if (!m) return false;
        out = OperandAt(m, o.pos, o.size);
        return true;
    }
    return false;
}

// A struct offset read from code: non-zero, small and aligned.
static bool Plausible(const char* name, uint32_t v, uint32_t align, std::string& problems) {
    if (v && v < 0x10000 && v % align == 0) return true;
    char buf[96];
    snprintf(buf, sizeof(buf), " %s=0x%X implausible;", name, v);
    problems += buf;
    return false;
}

// The service entry of an activation path table: must point to the named model/animation path.
static uintptr_t TableEntry(uint32_t tableRva, const char* expect, std::string& problems) {
    uintptr_t entry = g_base + tableRva + 8;  // index 1 = service
    uintptr_t str = 0;
    char text[64] = {};
    if (tableRva < g_size && Read((void*)entry, 0, str) && str > g_base && str < g_base + g_size &&
        SafeRead((void*)str, text, sizeof(text) - 1) && strcmp(text, expect) == 0)
        return entry;
    problems += " icon table does not hold ";
    problems += expect;
    problems += ";";
    return 0;
}

void ResolveSignatures(const std::string& iniPath, const std::string& buildId, std::string& summary) {
    memset(&g_game, 0, sizeof(g_game));
    FindText();
    char iniBuild[64] = {};
    GetPrivateProfileStringA("Signatures", "GameBuild", "", iniBuild, sizeof(iniBuild), iniPath.c_str());
    bool useIni = iniBuild[0] && buildId == iniBuild;
    int overrides = 0, found = 0;
    std::string missing;

    for (int i = 0; i < kMaxSig; i++) {
        std::string marked = kSignatures[i].marked;
        size_t eq = marked.find('=');
        Resolved& r = g_resolved[i];
        r.name = marked.substr(7, eq - 7);  // after "SAGSIG:"
        r.pattern = marked.substr(eq + 1);
        r.address = 0;
        r.fromIni = false;
        if (useIni) {
            char buf[1024] = {};
            GetPrivateProfileStringA("Signatures", r.name.c_str(), "", buf, sizeof(buf), iniPath.c_str());
            if (buf[0]) {
                r.pattern = buf;
                r.fromIni = true;
                overrides++;
            }
        }
        std::vector<int> pat;
        uintptr_t hits[8];
        int n = Parse(r.pattern, pat) ? Scan(pat, hits, 8) : -1;
        bool ok = n == 1 || (n > 1 && kSignatures[i].sameValues && SameOperands(r.name, hits, n));
        if (ok) {
            r.address = hits[0];
            found++;
            Log("signature %-18s %s at exe+0x%llX%s", r.name.c_str(), r.fromIni ? "(ini)" : "", (unsigned long long)(hits[0] - g_base),
                n > 1 ? " (identical copies)" : "");
        } else {
            Log("signature %-18s %s %s", r.name.c_str(), r.fromIni ? "(ini)" : "",
                n < 0 ? "is malformed" : n == 0 ? "NOT FOUND" : "matched more than once - refusing to guess");
            missing += " " + r.name;
        }
    }
    if (iniBuild[0] && !useIni)
        Log("ignoring [Signatures] in the ini: made for game build %s, running %s", iniBuild, buildId.c_str());

    // Functions
    GameLayout& g = g_game;
    g.startActivation = Match("StartActivation");
    g.stopActivation = Match("StopActivation");
    g.performActivation = Match("PerformActivation");
    g.garageItemUpdate = Match("GarageItemUpdate");
    g.garageItemDraw = Match("GarageItemDraw");
    g.triggerUpdate = Match("TriggerUpdate");
    g.markerCreate = Match("MarkerCreate");
    g.markerPlace = Match("MarkerPlace");
    g.markerDraw = Match("MarkerDraw");
    g.markerRelease = Match("MarkerRelease");

    // Offsets and data, each checked for plausibility
    std::string problems;
    uint32_t slotType2 = 0, stopVslot = 0, iconAnim = 0, iconModel = 0;
    bool slotOk = Op("slot_type", g.slotType) && Op("slot_type_2", slotType2) && Op("slot_item", g.slotItem) &&
                  Op("stop_vslot", stopVslot) && Plausible("slot_type", g.slotType, 4, problems) &&
                  Plausible("slot_item", g.slotItem, 8, problems) && Plausible("stop_vslot", stopVslot, 8, problems);
    if (slotOk && slotType2 != g.slotType) {
        problems += " slot_type differs between start and perform;";
        slotOk = false;
    }
    bool garageOk = Op("item_node", g.itemNode) && Op("item_garage", g.itemGarage) && Op("garage_status", g.garageStatus) &&
                    Plausible("item_node", g.itemNode, 8, problems) && Plausible("item_garage", g.itemGarage, 8, problems) &&
                    Plausible("garage_status", g.garageStatus, 4, problems);
    bool triggerOk = g.triggerUpdate && Op("trigger_manager", g.triggerManager) && Op("trigger_list_head", g.triggerListHead) &&
                     Op("trigger_list_tail", g.triggerListTail) && Plausible("trigger_manager", g.triggerManager, 8, problems) &&
                     Plausible("trigger_list_head", g.triggerListHead, 8, problems) &&
                     g.triggerListTail == g.triggerListHead + 0x10;
    if (Op("icon_anim_table", iconAnim) && Op("icon_model_table", iconModel)) {
        g.serviceIconAnim = TableEntry(iconAnim, "/model/activation/service.pma", problems);
        g.serviceIconModel = TableEntry(iconModel, "/model/activation/service.pmd", problems);
    }

    g.core = g.startActivation && g.stopActivation && g.performActivation && g.garageItemUpdate && slotOk && garageOk;
    g.triggers = g.core && triggerOk;
    g.icon = g.triggers && g.garageItemDraw && g.markerCreate && g.markerPlace && g.markerDraw && g.markerRelease &&
             g.serviceIconModel && g.serviceIconAnim;
    if (!problems.empty()) Log("layout problems:%s", problems.c_str());
    Log("layout: slot type +0x%X item +0x%X | garage item node +0x%X garage +0x%X status +0x%X | triggers +0x%X",
        g.slotType, g.slotItem, g.itemNode, g.itemGarage, g.garageStatus, g.triggerManager);

    char buf[256];
    snprintf(buf, sizeof(buf), "%d/%d signatures matched%s", found, kMaxSig,
             overrides ? (useIni ? " (using ini repairs)" : "") : "");
    summary = buf;
    if (!missing.empty()) summary += ", missing:" + missing;
}

}  // namespace sag
