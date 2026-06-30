@echo off
REM Incremental rebuild after editing TrenchBroom source. Reuses build-ninja (does
REM NOT wipe it), so only changed files recompile + relink. Fast (seconds-minutes).
REM For a full clean rebuild instead, run tb_build.bat.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set "PATH=C:\Program Files\CMake\bin;C:\Users\Lex\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%"
cd /d C:\msys64\home\Lex\TrenchBroom\build-ninja
cmake --build . --target TrenchBroom
set "BUILD_RC=%ERRORLEVEL%"
if "%BUILD_RC%"=="0" (
    echo. & echo Rebuilt: build-ninja\app\TrenchBroom\TrenchBroom.exe
) else (
    echo. & echo BUILD FAILED ^(rc=%BUILD_RC%^). If it is LNK1104 on TrenchBroom.exe, CLOSE the running TrenchBroom first.
)
exit /b %BUILD_RC%
