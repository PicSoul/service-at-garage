#include "ini_upgrade.h"

#include <ctype.h>

#include <map>
#include <set>
#include <vector>

namespace sag {

static std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) b++;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

// "[name]" -> lower-case name; anything else -> false.
static bool SectionName(const std::string& line, std::string& name) {
    std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    name = Lower(Trim(t.substr(1, t.size() - 2)));
    return true;
}

// "key = value" -> lower-case key; comments and blank lines -> false.
static bool KeyName(const std::string& line, std::string& key) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    key = Lower(Trim(t.substr(0, eq)));
    return !key.empty();
}

std::string UpgradeIni(const std::string& existing, const std::string& tmpl, std::string& out) {
    // Template sections and keys ("section.key").
    std::set<std::string> ownedSections, templateKeys;
    std::vector<std::string> templateOrder;
    std::string section, key;
    for (const std::string& line : Lines(tmpl)) {
        if (SectionName(line, section)) {
            ownedSections.insert(section);
        } else if (KeyName(line, key) && templateKeys.insert(section + "." + key).second) {
            templateOrder.push_back(section + "." + key);
        }
    }

    // The player's values in the template's sections; everything in other sections is kept as is.
    std::map<std::string, std::string> values;
    std::vector<std::string> fileOrder, kept;
    bool owned = false;
    section.clear();
    for (const std::string& line : Lines(existing)) {
        if (SectionName(line, section)) {
            owned = ownedSections.count(section) != 0;
            if (!owned) kept.push_back(line);
            continue;
        }
        if (!owned) {
            if (!section.empty()) kept.push_back(line);  // text before the first section is the template's header
            continue;
        }
        if (!KeyName(line, key)) continue;
        std::string full = section + "." + key;
        if (values.count(full)) continue;  // first occurrence wins
        std::string t = Trim(line);
        values[full] = Trim(t.substr(t.find('=') + 1));
        fileOrder.push_back(full);
    }

    std::string added, removed;
    for (const std::string& k : templateOrder)
        if (!values.count(k)) added += (added.empty() ? "" : ", ") + k.substr(k.find('.') + 1);
    for (const std::string& k : fileOrder)
        if (!templateKeys.count(k)) removed += (removed.empty() ? "" : ", ") + k.substr(k.find('.') + 1);
    if (added.empty() && removed.empty()) return "";

    // Template text with the player's values filled in, then the kept sections.
    std::string result;
    section.clear();
    for (const std::string& line : Lines(tmpl)) {
        std::string out_line = line;
        if (!SectionName(line, section) && KeyName(line, key)) {
            auto v = values.find(section + "." + key);
            if (v != values.end()) {
                size_t eq = line.find('=');
                size_t valueStart = eq + 1;
                while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
                out_line = line.substr(0, valueStart) + v->second;
            }
        }
        result += out_line + "\n";
    }
    if (!kept.empty()) {
        while (!kept.empty() && Trim(kept.back()).empty()) kept.pop_back();
        if (!result.empty() && result.size() >= 2 && result.substr(result.size() - 2) != "\n\n") result += "\n";
        for (const std::string& line : kept) result += line + "\n";
    }
    out = result;

    std::string note;
    if (!added.empty()) note += "added " + added;
    if (!removed.empty()) note += (note.empty() ? "" : "; ") + std::string("removed ") + removed;
    return note;
}

}  // namespace sag
