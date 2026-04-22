@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl.exe -DCMAKE_CXX_COMPILER=clang-cl.exe -DCMAKE_LINKER=lld-link.exe -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_OVERLAY_PORTS=vcpkg-overlay-ports/ports -DVCPKG_TARGET_TRIPLET=x64-windows-static
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
ninja -C build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
dir build\bin
.\build\bin\AliceViewer.exe --headless --headless-capture
