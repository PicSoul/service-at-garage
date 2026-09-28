@echo off
setlocal
rem Offline tests: the plugin's signature resolution against every installed game (ATS and/or ETS2),
rem then the update tool's check. Neither starts the game.
cd /d "%~dp0.."

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>nul || exit /b 1
if not exist "bin\obj_test" mkdir "bin\obj_test"
cl /nologo /O2 /MT /W3 /EHsc /std:c++17 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I "include" ^
   tests\sig_test.cpp src\common.cpp src\signatures.cpp /Fo"bin\obj_test\\" /Fe"bin\sig_test.exe" >nul
if errorlevel 1 (
    echo [ERROR] Building the test failed.
    exit /b 1
)

python tools\update_check.py --print-exe | bin\sig_test.exe || exit /b 1
echo.
python tools\update_check.py --dll bin\service_at_garage.dll
