@echo off
rem Usage: Tools\build.bat [Debug|Release] [extra cmake --build args]
rem   QT_DIR        Qt kit to build the editor with (default C:\Qt\6.9.0\msvc2022_64)
rem   FG_CMAKE_ARGS extra arguments for the configure step (e.g. -DFG_BUILD_EDITOR=OFF)
rem Visual Studio 2022 (any edition, or the Build Tools) is found with vswhere.
setlocal
set ROOT=%~dp0..
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
shift
if not defined QT_DIR set QT_DIR=C:\Qt\6.9.0\msvc2022_64

rem ---- the MSVC environment (skipped when this prompt already has it)
if defined VCINSTALLDIR goto :haveMsvc
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo build.bat: vswhere.exe not found - install Visual Studio 2022 or run from a developer prompt & exit /b 1)
set VSDIR=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo build.bat: no Visual Studio with the C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:haveMsvc

rem ---- Ninja: from the Qt installation when it has one (Visual Studio's CMake component brings one too)
if exist "%QT_DIR%\..\..\Tools\Ninja\ninja.exe" set PATH=%QT_DIR%\..\..\Tools\Ninja;%PATH%
set PATH=%QT_DIR%\bin;%PATH%

set BUILD=%ROOT%\build\%CONFIG%
cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% "-DCMAKE_PREFIX_PATH=%QT_DIR%" -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl %FG_CMAKE_ARGS% || exit /b 1
cmake --build "%BUILD%" %1 %2 %3 %4 %5 %6 %7 %8 %9 || exit /b 1
