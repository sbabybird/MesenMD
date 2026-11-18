@echo off
setlocal
rem ---------------------------------------------------------------------------
rem MesenMD - build the SDL2 + ImGui frontend and copy runtime dependencies.
rem Requirements: MinGW-w64 g++ (e.g. D:\ThirdLib\mingw64\bin) on PATH or here.
rem ---------------------------------------------------------------------------

set "MINGW=D:\ThirdLib\mingw64\bin"
set "SDLROOT=third_party\SDL2-2.32.10\x86_64-w64-mingw32"
set "IMGUI=third_party\imgui-master"
set "COREINC=third_party\Genesis-Plus-GX-master\libretro\libretro-common\include"

if not exist "%MINGW%\g++.exe" (
    echo ERROR: g++ not found at "%MINGW%\g++.exe". Edit MINGW path in build.cmd.
    exit /b 1
)

"%MINGW%\g++.exe" -std=c++17 -O2 -mwindows ^
  -Isrc ^
  -I"%IMGUI%" ^
  -I"%IMGUI%\backends" ^
  -I"%SDLROOT%\include\SDL2" ^
  -I"%COREINC%" ^
  -Ithird_party\stb ^
  src\main.cpp src\emulator.cpp src\gl_loader.cpp src\video_filters.cpp src\stb_impl.cpp ^
  "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" ^
  "%IMGUI%\backends\imgui_impl_sdl2.cpp" "%IMGUI%\backends\imgui_impl_opengl3.cpp" ^
  -L"%SDLROOT%\lib" ^
  -lmingw32 -lSDL2main -lSDL2 -lshell32 -lopengl32 -lgdi32 -lwinmm -lcomdlg32 ^
  -static-libgcc -static-libstdc++ ^
  -o MesenMD.exe

if errorlevel 1 (
    echo.
    echo Build FAILED.
    exit /b 1
)

copy /Y "%SDLROOT%\bin\SDL2.dll" . >nul
if exist third_party\Genesis-Plus-GX-master\genesis_plus_gx_libretro.dll (
    copy /Y third_party\Genesis-Plus-GX-master\genesis_plus_gx_libretro.dll . >nul
) else (
    echo WARNING: core DLL not found. Build it first - see README.md.
)

echo.
echo Build complete: MesenMD.exe
endlocal