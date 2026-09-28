@echo off
setlocal enabledelayedexpansion
rem Installs (or removes) the plugin in every installed SCS truck game (ATS and/or ETS2, found through Steam):
rem   install.bat                      build if needed and install
rem   install.bat /uninstall           remove the plugin
rem   install.bat "D:\...\win_x64"     install into this game bin\win_x64 folder only
rem The plugin writes its service_at_garage.ini on first start; an existing one is kept.
cd /d "%~dp0"

set "UNINSTALL="
if /i "%~1"=="/uninstall" set "UNINSTALL=1"

set "LIST=%TEMP%\sag_games_%RANDOM%.txt"
if defined UNINSTALL goto find_games
if "%~1"=="" goto find_games
echo CUSTOM^|%~1> "%LIST%"
goto have_games
:find_games
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\find_games.ps1" > "%LIST%"
:have_games

set "FOUND="
for /f "usebackq tokens=1,* delims=|" %%A in ("%LIST%") do set "FOUND=1"
if not defined FOUND (
    del "%LIST%" 2>nul
    echo [ERROR] Could not find American Truck Simulator or Euro Truck Simulator 2 through Steam.
    echo Run:  install.bat "^<game install^>\bin\win_x64"
    echo or copy bin\service_at_garage.dll into ^<game install^>\bin\win_x64\plugins\ yourself.
    exit /b 1
)

if not defined UNINSTALL if not exist "bin\service_at_garage.dll" (
    call "%~dp0build.bat" || (del "%LIST%" 2>nul & exit /b 1)
)

set "FAILED="
for /f "usebackq tokens=1,* delims=|" %%A in ("%LIST%") do (
    set "PLUGINS=%%B\plugins"
    if defined UNINSTALL (
        del /q "!PLUGINS!\service_at_garage.dll" "!PLUGINS!\service_at_garage.ini" "!PLUGINS!\service_at_garage.log" 2>nul
        echo [%%A] removed from "!PLUGINS!"
    ) else (
        if not exist "!PLUGINS!" mkdir "!PLUGINS!"
        copy /Y "bin\service_at_garage.dll" "!PLUGINS!\" >nul
        if errorlevel 1 (
            echo [%%A] [ERROR] copy failed - is the game running?
            set "FAILED=1"
        ) else (
            echo [%%A] installed to "!PLUGINS!"
        )
    )
)
del "%LIST%" 2>nul
if defined FAILED exit /b 1
if not defined UNINSTALL echo Start the game, accept the SDK plugin prompt, and look for "[Service At Garage] ... active" in the console (~).
