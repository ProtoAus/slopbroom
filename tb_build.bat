@echo off
REM TrenchBroom build via the CI-proven Ninja generator (avoids the VS-generator
REM /PDBSTRIPPED relative-path LNK1201). Run from a plain shell — vcvarsall sets up MSVC.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
set "PATH=C:\Program Files\CMake\bin;C:\Users\Lex\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%"
cd /d C:\msys64\home\Lex\TrenchBroom
if exist build-ninja rmdir /s /q build-ninja
mkdir build-ninja
cd build-ninja
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 -DTB_ENABLE_PCH=0 -DPANDOC_PATH=C:/Users/Lex/AppData/Local/Pandoc/pandoc.exe || exit /b 1
cmake --build . --target TrenchBroom || exit /b 1
echo TB_BUILD_OK
