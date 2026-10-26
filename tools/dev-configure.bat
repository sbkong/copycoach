@echo off
rem Configure only (vcpkg install + CMake) for one preset; same environment as dev-build.bat.
rem Usage: tools\dev-configure.bat dev-debug
setlocal
if "%~1"=="" (echo usage: dev-configure.bat ^<preset^> & exit /b 2)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set VSLANG=1033
set "CMAKE_BIN=C:\Program Files\JetBrains\CLion 2025.1.3\bin\cmake\win\x64\bin"
set "PATH=C:\Program Files\JetBrains\CLion 2025.1.3\bin\ninja\win\x64;%CMAKE_BIN%;%PATH%"
cd /d "%~dp0.."
cmake --preset %1 || exit /b 1
endlocal
