#pragma once

namespace sag {

// Floating service icon in the bay of an eligible garage.
// A service station's icon is an "activation marker" owned by its map item: { model instance, params },
// created from the activation model tables, placed every frame and drawn from the item's draw method.
// The plugin keeps one marker of its own with the service model, placed in the entrance bay along the
// garage node's forward axis (manage-garage icon -> door) and drawn from the draw method of the garage it
// belongs to. Like a real service station it is hidden while
// the prompt is active.

void IconSelectTarget();          // after each trigger scan: pick the garage bay and placement
void IconUpdate();                // every frame from the trigger update hook: create/place the marker
void IconOnPause(bool paused);    // releases the marker on pause (quitting always pauses first)
// Diagnostics: logs the garage's rotation, nearby sleep triggers and the truck in garage-local coordinates.
void IconLogGeometry(void* garageItem, const char* why);

void __fastcall HookGarageItemDraw(void* item, void* ctx);
extern void(__fastcall* OrigGarageItemDraw)(void* item, void* ctx);

}  // namespace sag
