#pragma once

namespace sag {

// Floating service icon in the bay of an eligible garage.
// A service station's icon is an "activation marker" owned by its map item: { model instance, params },
// created from the activation model tables, placed every frame and drawn from the item's draw method.
// The plugin keeps one marker of its own with the service model, placed at the bay's sleep trigger and
// drawn from the draw method of the garage it belongs to. Like a real service station it is hidden while
// the prompt is active.

void IconSelectTarget();          // after each trigger scan: pick the garage bay and placement
void IconUpdate();                // every frame from the trigger update hook: create/place the marker
void IconOnPause(bool paused);    // releases the marker on pause (quitting always pauses first)

void __fastcall HookGarageItemDraw(void* item, void* ctx);
extern void(__fastcall* OrigGarageItemDraw)(void* item, void* ctx);

}  // namespace sag
