#pragma once

namespace sag {

// "Service first, then sleep" in the bay of an eligible garage.
//   Entering the bay: the bay's sleep trigger asks for the sleep prompt; the plugin shows the game's own
//   service prompt instead. After the service screen was used, or after the timeout, the sleep prompt
//   comes back and is kept in the slot while the truck stays in the bay (the game clears it e.g. when
//   the truck is rebuilt after a paint/tuning change). Leaving is read from the bay trigger's state.
//   Where the game gives no sleep prompt at all (ATS car mode), the service prompt is shown on entering
//   the bay and simply stays until the vehicle leaves; no sleep prompt is ever added.

bool BayIdle();  // no service/sleep prompt of ours is active
void BayOnFrame();
// After each trigger scan (gameplay = verified local_gameplay_mode_u or null). Where the game gives no
// sleep prompt (ATS car mode), shows the service prompt on entering an eligible garage's sleep zone.
void BayOnTriggerScan(void* gameplay);

void __fastcall HookStartActivation(void* self, int type, void* data);
void __fastcall HookStopActivation(void* self);
void __fastcall HookPerformActivation(void* self);
extern void(__fastcall* OrigStartActivation)(void* self, int type, void* data);
extern void(__fastcall* OrigStopActivation)(void* self);
extern void(__fastcall* OrigPerformActivation)(void* self);

}  // namespace sag
