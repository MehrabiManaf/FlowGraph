@echo off
rem Usage: Tools\build.bat [Debug|Release] [extra cmake --build args]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
shift
set ROOT=%~dp0..
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Qt\Tools\Ninja;C:\Qt\6.9.0\msvc2022_64\bin;%PATH%
set BUILD=%ROOT%\build\%CONFIG%
cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=C:/Qt/6.9.0/msvc2022_64 -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl %FG_CMAKE_ARGS% || exit /b 1
cmake --build "%BUILD%" %1 %2 %3 %4 %5 %6 %7 %8 %9 || exit /b 1
