@echo off
setlocal
rem Builds bin\service_at_garage.dll (x64 telemetry plugin for ATS and ETS2) with Visual Studio 2022.
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] Visual Studio 2022 not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

set "MH=third_party\minhook"
if not exist "%MH%\src\hook.c" (
    echo [ERROR] MinHook is missing. Run:  git submodule update --init
    exit /b 1
)
set "SDK=third_party\scs_sdk\include"
if not exist "bin\obj" mkdir "bin\obj"

cl /nologo /LD /O2 /MT /W3 /EHsc /std:c++17 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS ^
   /I "include" /I "%MH%\include" /I "%SDK%" ^
   src\main.cpp src\common.cpp src\ini_upgrade.cpp src\signatures.cpp src\world.cpp src\bay.cpp src\icon.cpp ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /Fo"bin\obj\\" /Fe"bin\service_at_garage.dll" ^
   /link /DLL /EXPORT:scs_telemetry_init /EXPORT:scs_telemetry_shutdown
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)
del /q "bin\service_at_garage.exp" "bin\service_at_garage.lib" 2>nul
echo Built bin\service_at_garage.dll
