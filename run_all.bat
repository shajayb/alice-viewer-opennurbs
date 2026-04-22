@echo off
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl.exe -DCMAKE_CXX_COMPILER=clang-cl.exe -DCMAKE_LINKER=lld-link.exe -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_OVERLAY_PORTS=%~dp0vcpkg-overlay-ports/ports -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_MAKE_PROGRAM="C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
ninja -C build
.\build\bin\AliceViewer.exe --headless --headless-capture > executor_console.log 2>&1
