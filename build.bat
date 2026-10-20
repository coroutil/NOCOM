@echo off
rem Build from an x86 Native Tools Command Prompt for Synergy's 32-bit process.
rem NOCOM.dll contains the validation gate and all feature code in one binary.
cl /nologo /LD /EHsc /O2 /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fe:NOCOM.dll ^
  src\main.cpp src\drm.cpp src\game.cpp src\settings.cpp src\config.cpp src\render.cpp src\menu.cpp src\features.cpp src\esp.cpp src\combat_feedback.cpp src\entity_names.cpp src\netvar.cpp src\discord.cpp ^
  /link /OUT:NOCOM.dll user32.lib winmm.lib gdiplus.lib comdlg32.lib dbghelp.lib winhttp.lib bcrypt.lib
if errorlevel 1 exit /b 1
echo Built single-file NOCOM.dll.
echo Inject NOCOM.dll only. There is no NOCOMCore.dll.
