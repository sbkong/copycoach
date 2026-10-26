@echo off
rem Configure + build + test one preset from a plain shell (CLion users open CMakePresets.json instead).
rem Usage: tools\dev-build.bat dev-debug
setlocal
if "%~1"=="" (echo usage: dev-build.bat ^<preset^> & exit /b 2)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem English compiler messages so Ninja's /showIncludes dependency parsing matches on a Korean Windows (set the same in CLion toolchain env)
set VSLANG=1033
set "CMAKE_BIN=C:\Program Files\JetBrains\CLion 2025.1.3\bin\cmake\win\x64\bin"
set "PATH=C:\Program Files\JetBrains\CLion 2025.1.3\bin\ninja\win\x64;%CMAKE_BIN%;%PATH%"
cd /d "%~dp0.."
cmake --preset %1 || exit /b 1
cmake --build --preset %1 || exit /b 1
ctest --preset %1 || exit /b 1
endlocal
