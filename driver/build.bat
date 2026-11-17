@echo off
rem driver/build.bat - kernel build for RrSvc.sys (WDK + MSVC, x64).
rem
rem The driver is intentionally outside CMake: run this batch by hand.
rem Output: build_driver\RrSvc.sys

setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build.bat] vcvars64.bat introuvable ou en echec.
    exit /b 1
)

set "WDKROOT=C:\Program Files (x86)\Windows Kits\10"

rem Newest installed kit that actually ships km headers and x64 km libs.
set "KITVER="
for /f "delims=" %%v in ('dir /b /ad /o-n "%WDKROOT%\Include\10.*" 2^>nul') do (
    if not defined KITVER if exist "%WDKROOT%\Lib\%%v\km\x64\ntoskrnl.lib" set "KITVER=%%v"
)
if not defined KITVER (
    echo [build.bat] Aucun kit WDK x64 utilisable sous "%WDKROOT%".
    exit /b 1
)

set "KMINC=%WDKROOT%\Include\%KITVER%\km"
set "KMLIB=%WDKROOT%\Lib\%KITVER%\km\x64"

pushd "%~dp0.."
if not exist "build_driver" mkdir "build_driver"

echo [build.bat] kit %KITVER%
echo [build.bat] cl driver\Source.cpp

cl /nologo /kernel /GS- /GR- /O2 /W4 /std:c++17 /D_AMD64_ ^
   /I "%KMINC%" /I "%KMINC%\crt" /I "driver" ^
   /c "driver\Source.cpp" /Fo"build_driver\RrSvc.obj"
if errorlevel 1 goto fail

echo [build.bat] link RrSvc.sys

link /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB ^
   /LIBPATH:"%KMLIB%" /OUT:"build_driver\RrSvc.sys" ^
   "build_driver\RrSvc.obj" ntoskrnl.lib
if errorlevel 1 goto fail

del /q "build_driver\RrSvc.obj"
echo [build.bat] OK : build_driver\RrSvc.sys
popd
exit /b 0

:fail
del /q "build_driver\RrSvc.obj" 2>nul
echo [build.bat] ECHEC
popd
exit /b 1
