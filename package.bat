@echo off
setlocal
rem Builds the plugin and creates dist\Service-At-Garage-v<version>.zip for players:
rem   service_at_garage.dll, README.txt, LICENSE.txt, THIRD_PARTY_NOTICES.txt, tools\ (update checker)
rem The version comes from include\version.h.
cd /d "%~dp0"

call "%~dp0build.bat" >nul || (echo [ERROR] Build failed - run build.bat to see why. & exit /b 1)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$h = Get-Content 'include\version.h' -Raw;" ^
  "$ver = [regex]::Match($h, 'SAG_VERSION \""([^\""]+)\""').Groups[1].Value;" ^
  "$game = [regex]::Match($h, 'SAG_GAME_VERSION \""([^\""]+)\""').Groups[1].Value;" ^
  "$stage = Join-Path $env:TEMP ('sag_pkg_' + [guid]::NewGuid());" ^
  "New-Item -ItemType Directory (Join-Path $stage 'tools') -Force | Out-Null;" ^
  "Copy-Item 'bin\service_at_garage.dll' $stage;" ^
  "Copy-Item 'tools\update_check.py', 'tools\update_check.bat' (Join-Path $stage 'tools');" ^
  "(Get-Content 'packaging\README.txt' -Raw).Replace('{VERSION}', $ver).Replace('{GAME_VERSION}', $game) | Set-Content (Join-Path $stage 'README.txt') -Encoding ascii;" ^
  "Copy-Item 'LICENSE' (Join-Path $stage 'LICENSE.txt');" ^
  "Copy-Item 'THIRD_PARTY_NOTICES.md' (Join-Path $stage 'THIRD_PARTY_NOTICES.txt');" ^
  "New-Item -ItemType Directory 'dist' -Force | Out-Null;" ^
  "$zip = 'dist\Service-At-Garage-v' + $ver + '.zip';" ^
  "if (Test-Path $zip) { Remove-Item $zip };" ^
  "Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip;" ^
  "Remove-Item $stage -Recurse;" ^
  "Write-Host ('Created ' + $zip + ' (v' + $ver + ', ' + $game + ')')" || exit /b 1
