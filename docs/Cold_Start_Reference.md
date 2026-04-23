# AliceViewer: Cesium Google Earth Photorealistic (GEPR) Development Reference

This document serves as a cold-start reference and comprehensive summary of the development, structure, and optimization of the `alice-viewer-opennurbs` repository, specifically focusing on the implementation of the Cesium Google Earth Photorealistic (GEPR) 3D Tiles rendering engine.

## 1. Repository Structure

The repository is a C++17 OpenGL viewer originally built to handle OpenNURBS, which has been extended to parse and render standard 3D Tiles (`b3dm`/`glb` payloads) using the Cesium Ion API. 

**Key Directories and Files:**
*   **`src/`**: Contains the core application files (`main.cpp`, `AliceViewer.cpp`, `sketch.cpp`).
*   **`include/`**: Contains the critical header-only implementations.
    *   **`CesiumGEPR.h`**: The absolute core of the 3D Tiles rendering engine. This file manages Octree traversal, LOD evaluation, networking, AABB culling, and OpenGL buffer management.
    *   **`AliceViewer.h`**: Manages the application window, input, and camera state.
    *   **`AliceJson.h`**: A custom, lightweight JSON parser used to decode `tileset.json` trees.
    *   **`AliceMemory.h`**: Contains the custom memory arena (`g_Arena`) to enforce Data-Oriented Design (DOD) constraints.
    *   **`ApiKeyReader.h`**: Utility to read Cesium Ion tokens from `API_KEYS.txt`.
*   **`build_and_run.bat`**: The primary build script utilizing `vcvars64.bat`, CMake, and Ninja.
*   **`CMakeLists.txt`**: Configured to build against `vcpkg` dependencies in a single static executable format.
*   **`.agents/executor.md`**: Contains the system prompt, protocols, and DOD constraints imposed on AI agents contributing to the repository.

## 2. Build System & Environment

The project is heavily optimized for a Windows environment using the LLVM `clang-cl` compiler.

*   **Compiler:** `clang-cl.exe` (LLVM).
*   **Build Generator:** Ninja (`ninja -C build`).
*   **Dependencies:** Handled via a local `vcpkg` overlay configured for the `x64-windows-static` triplet. Key dependencies include `glfw3`, `glad`, `imgui`, `glm`, `libcurl`, and `opennurbs`.
*   **Compilation Flags:** Configured for strict Release optimizations by default (`/O2`, `/MT`).
*   **Execution:** The build script originally output a headless framebuffer capture for AI validation, but was updated to support interactive GUI usage.

## 3. Core Technical Breakthroughs (Session History)

Throughout this session, we transformed a buggy, geometric-corrupt rendering script into an industrial-grade, hyperfast 3D Tiles viewer capable of rendering 1.4 million vertices (Christ the Redeemer) at 60 FPS on lightweight hardware.

### Phase 1: Fixing Geometry & Math
1.  **Screen-Space Error (SSE):** We replaced an arbitrary distance-to-camera LOD metric with true NDC (Normalized Device Coordinates) projection error math derived from `cesium-native`. Tiles now refine strictly if their visual error exceeds 16 pixels on screen.
2.  **The Y-Up to Z-Up Crisis:** The viewer suffered from severe "spatial tearing" and giant triangles spanning the sky. We fixed this by correctly applying a `Y_UP_TO_Z_UP` transformation matrix to the local glTF vertices *before* translating them by the high-precision Earth-Centered Earth-Fixed (ECEF) `RTC_CENTER` offset.

### Phase 2: Data-Oriented Design (DOD) Optimizations
To meet the strict crucible mandates of `.agents/executor.md`, we purged the engine of heap fragmentation:
1.  **Eliminated `std::vector` & `std::string`:** Removed all STL containers from the `traverse()` hot loops and `CesiumNetwork` fetching logic.
2.  **Memory Arenas:** Refactored URL string resolution to utilize stack-allocated `char[]` arrays and shifted HTTP payload downloads directly into static 16MB memory blocks (`FetchBuffer`). 

### Phase 3: Hyperfast Asynchronous Loading
To achieve maximum "zippiness" on low-end devices like the Surface Pro:
1.  **`curl_multi_perform` Integration:** Replaced the blocking synchronous `libcurl` calls with a true asynchronous, non-blocking queue. The engine can now download up to 20 tiles concurrently in the background without dropping a single frame on the main OpenGL rendering thread.
2.  **Seamless Refinement:** The system keeps low-resolution parent meshes strictly visible until their high-resolution children are fully downloaded, parsed, and pushed to the GPU, preventing any visual "holes" during traversal.

## 4. Cold Start Development Guidelines

When resuming work on this repository, adhere to the following principles:
*   **DOD Mandate:** Any modifications inside `CesiumGEPR.h::traverse()` or `update()` must strictly avoid dynamic heap allocations. Use the `Alice::g_Arena` or static buffers.
*   **Header-Only Restraints:** The application logic is primarily header-only. Avoid adding new heavy `.cpp` linking requirements if possible.
*   **Concurrency:** OpenGL state mutation (`glGenVertexArrays`, `glBufferData`) *must* happen on the main thread. Do not attempt to thread the VBO uploads. Rely on the `curl_multi` async callback system to handle heavy network loads off the render blocking path.
