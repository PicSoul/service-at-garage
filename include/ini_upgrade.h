#pragma once

#include <string>

namespace sag {

// Brings an existing ini up to date with the plugin's current default ini (the template):
// - keys the template has keep the player's value (first occurrence, as Windows' ini reader uses);
// - keys missing from the file are added with their default value and comment;
// - keys the template no longer has are dropped (from the template's sections only);
// - sections the template does not have (e.g. [Signatures] from the update tool) are kept verbatim.
// The result is the template's layout and comments with the player's values filled in.
// Returns "" if nothing is missing or obsolete (then `out` is untouched and the file should be left alone),
// otherwise a short description such as "added car_fuel; removed icon_offset_away".
std::string UpgradeIni(const std::string& existing, const std::string& tmpl, std::string& out);

}  // namespace sag
