$env:PATH = "C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;" + $env:PATH
if (!(Test-Path build)) {
    & "cmake" -B build -G Ninja -DCMAKE_C_COMPILER="clang-cl.exe" -DCMAKE_CXX_COMPILER="clang-cl.exe" -DCMAKE_LINKER="lld-link.exe" -DCMAKE_TOOLCHAIN_FILE="../vcpkg/scripts/buildsystems/vcpkg.cmake"
}
& "cmake" --build build
if ($?) {
    if (Test-Path "build\bin\AliceViewer.exe") {
        & ".\build\bin\AliceViewer.exe" --headless
    } else {
        & ".\build\AliceViewer.exe" --headless
    }
}
