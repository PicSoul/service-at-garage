@echo off
rem Checks the plugin's signatures against the installed game executables after a game update.
rem   update_check.bat            check only
rem   update_check.bat --write    also store repaired signatures in the installed ini
python "%~dp0update_check.py" %*
echo.
pause
