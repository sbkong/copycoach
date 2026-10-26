@echo off
rem Run copycoach.exe from a build folder with Qt on PATH. Usage: tools\dev-run.bat dev-debug
setlocal
if "%~1"=="" (echo usage: dev-run.bat ^<preset^> & exit /b 2)
set "PATH=D:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
cd /d "%~dp0..\build\%1"
start "" copycoach.exe
endlocal
