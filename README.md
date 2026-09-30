# Service At Garage

A plugin for **American Truck Simulator** and **Euro Truck Simulator 2** that lets you **repair and service your truck
at your own garage** instead of driving to a service center.

![Driving up to an owned garage with the floating service icon in the bay, pulling in, and opening the service screen from the service prompt](docs/demo.gif)

*The drive up to the garage is shown at double speed.*

- Pull into the bay of a garage you own and stop: the game's own **service prompt** appears where the sleep prompt
  would. Press Enter and the normal service screen opens.
- After you use it, or after 10 seconds, the prompt switches back to **sleep**, so sleeping at your garage still works.
- A floating green **service icon** marks the bay, like at a real service center.
- **ATS Road Trip cars** get the service prompt too (the game gives cars no sleep prompt in the bay, so the service
  prompt simply stays while you're in the bay), and can refuel at your garages' fuel pumps.
- Works with **any garage the game knows**, including garages added by map mods, as long as the bay has a sleep zone.
  Nothing on the map is changed, so there are no conflicts with map mods.
- Optional: only fully upgraded garages, or every garage.

**Download:** see [Releases](../../releases). The zip contains the DLL, a `README.txt` with install steps, and the
update checker.

## Install

Copy `service_at_garage.dll` into the game's `bin\win_x64\plugins\` folder (create `plugins` if needed), start the
game and accept the "SDK plugins" prompt. The console (`~`) shows `[Service At Garage] v1.0.0 active`.

From source: `install.bat` builds the DLL and copies it into every installed game (ATS and/or ETS2, found through
Steam); `install.bat /uninstall` removes it.

## Configuration (`service_at_garage.ini`)

Written next to the DLL on first start; read when the game starts.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | 1 | Turns the plugin on or off |
| `require_owned` | 1 | 1 = only garages you own, 0 = every garage |
| `min_level` | 1 | 1 = any owned garage, 2 = small and up, 3 = only fully upgraded |
| `radius` | 50 | Max distance (m) between truck and garage when the bay prompt appears |
| `restore_sleep_sec` | 10 | Seconds until the sleep prompt comes back if service isn't used |
| `icon` | 1 | Show the floating service icon |
| `icon_distance` | 10.5 | Metres from the manage-garage icon toward the bay door |
| `icon_offset_side` | 0 | Metres to move the icon sideways (negative = other side) |
| `car_fuel` | 1 | Cars (ATS Road Trip) can refuel at your garages' fuel pumps, like trucks |
| `log` | 0 | 0 = no log file. For troubleshooting: 1 = normal, 2 = verbose (also dumps nearby triggers) |

A `[Signatures]` section, if present, is written by the update tool (see below).

## How it works

The plugin loads through the games' official telemetry SDK. All game addresses and struct offsets are found at
startup from code signatures, never hard-coded; see `src/signatures.cpp`.

| Part | How |
|---|---|
| Garages near you | Hooks the garage map item's per-frame update; reads its position and the `garage_u` status bits (owned, tiny/small/large) |
| Service prompt | The game has one HUD activation slot. When the bay's sleep trigger asks for the sleep prompt near an eligible garage, the plugin calls the game's `start_activation(service, garage item)` instead. Enter then opens the real service screen (it only needs the item's position). |
| Back to sleep | After the service screen was used, or the timeout, the sleep prompt is put back and kept while the truck stays in the bay (the game clears it e.g. when the truck is rebuilt after a paint change) |
| Leaving the bay | Read from the bay's own sleep trigger (inside/outside state) in the game's trigger list, so it works whatever prompt is showing |
| Service icon | One of the game's own activation markers (the model and animation a service station uses), placed along the garage's own bay axis, a set distance from the manage-garage icon (so it stays in the entrance bay in any garage size), and drawn from the garage's draw method. Released on pause, so it never holds game resources at exit. |

Features degrade instead of breaking: if only the icon's signatures fail, the service prompt still works; if the
core signatures fail, the plugin does nothing and prints `[Service At Garage] INACTIVE`. It never guesses.

## After a game update

1. Run **`tools\update_check.bat`**. It finds ATS and ETS2 through Steam, reads the signatures built into each game's
   installed `service_at_garage.dll`, and checks them against that game's new executable (each must match exactly
   once, and the offsets read from it must be plausible).
2. If something is reported as BROKEN, run **`tools\update_check.bat --write`**. It searches the new executable for
   the moved code and writes repaired signatures into that game's `service_at_garage.ini`, stamped with the game
   build they're for (a backup is kept). No recompiling is needed.

   Overrides only apply to that exact game build, so an old ini can never break a newer DLL. To make a repair
   permanent, copy the repaired signatures into `src/signatures.cpp`, bump `include/version.h`, and release.
3. If it reports **NEEDS MANUAL WORK**, the game's code changed shape and that signature has to be re-derived by
   hand. Please open an issue.

## Building

Requirements: Visual Studio 2022 (x64 C++ tools). For the tools and tests: Python 3 (standard library only).

```cmd
git clone --recursive https://github.com/PicSoul/service-at-garage
build.bat               :: bin\service_at_garage.dll
tests\run_tests.bat     :: offline tests: signatures against every installed game + the update tool
install.bat             :: build if needed and copy into every installed game
package.bat             :: dist\Service-At-Garage-v<version>.zip for a release
```

| Folder | Contents |
|---|---|
| `src/`, `include/` | The plugin: `main` (SDK entry, hooks), `signatures`, `world` (garages, triggers), `bay` (prompt logic), `icon`, `common` |
| `tools/` | `update_check.bat/.py`, `find_games.ps1` |
| `tests/` | `sig_test.cpp` (maps the game exe and runs the plugin's own signature resolution) |
| `third_party/` | MinHook (git submodule), SCS telemetry SDK headers |

## Multiplayer

- **SCS Convoy**: only your own prompts, on your own screen.
- **TruckersMP** has its own rules about client modifications and may treat memory-modifying plugins as a
  violation. **Use it there at your own risk.**

## License

MIT, see [LICENSE](LICENSE). Includes [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause) and the SCS
SDK telemetry headers (MIT-style), see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
