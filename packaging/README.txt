Service At Garage v{VERSION}
for American Truck Simulator and Euro Truck Simulator 2 ({GAME_VERSION})
https://github.com/PicSoul/service-at-garage
====================================================================

Repair and service your truck at your own garage instead of driving to a
service center.

Pull into the bay of a garage you own and stop. Instead of the sleep prompt
you get the game's normal service prompt - press Enter and the regular
service screen opens. After you use it (or after 10 seconds) the prompt
switches back to sleep, so you can still sleep in your garage. A floating
green service icon marks the bay, just like at a real service center.

It works with any garage the game knows about, including garages added by
map mods, as long as the bay has a sleep zone. Nothing on the map is
changed, so it does not conflict with map mods.


INSTALL
-------
1. Copy service_at_garage.dll into the game's plugins folder:

     ATS:  ...\steamapps\common\American Truck Simulator\bin\win_x64\plugins\
     ETS2: ...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\

   (Create the "plugins" folder if it does not exist. In Steam: right-click
   the game > Manage > Browse local files > bin > win_x64.)
   Playing both games? Put a copy in each.

2. Start the game. It asks whether to allow "SDK plugins" - accept.

3. Press ~ to open the console. You should see:
     [Service At Garage] v{VERSION} active


SETTINGS
--------
The plugin creates service_at_garage.ini next to itself on first start.
Settings are read when the game starts.

  enabled=1            1 = on, 0 = off
  require_owned=1      1 = only garages you own, 0 = every garage
  min_level=1          1 = any owned garage, 2 = small and up,
                       3 = only fully upgraded garages
  radius=50            max distance (m) between truck and garage
  restore_sleep_sec=10 seconds until the sleep prompt comes back if you
                       don't use the service screen
  icon=1               1 = show the floating service icon in the bay
  icon_offset_away=4   metres to move the icon away from the
                       manage-garage icon
  icon_offset_side=0   metres to move the icon sideways (negative =
                       other side)
  log=0                0 = no log file, 1 = normal, 2 = verbose
                       (only needed for troubleshooting)


AFTER A GAME UPDATE
-------------------
If the console says "[Service At Garage] INACTIVE", the update moved code the
plugin relies on. The plugin then does nothing at all (your game is not
affected). To fix it without waiting for a new version:

1. Install Python 3 (python.org) if you don't have it.
2. Run tools\update_check.bat. It checks every installed game.
3. If it reports signatures it can repair, run
     tools\update_check.bat --write
   It stores the repaired signatures in service_at_garage.ini, only for that
   exact game build, and keeps a backup of the ini.
4. If it reports NEEDS MANUAL WORK, the game changed too much for an
   automatic repair - check the GitHub page above for an updated version.


UNINSTALL
---------
Delete service_at_garage.dll (and service_at_garage.ini / .log) from the
game's bin\win_x64\plugins folder.


MULTIPLAYER
-----------
SCS Convoy: only affects your own prompts on your own screen.
TruckersMP has its own rules about client modifications and may treat
memory-modifying plugins as a violation. Use it there at your own risk.


License: MIT - see LICENSE.txt. Includes MinHook and SCS SDK headers - see
THIRD_PARTY_NOTICES.txt.
