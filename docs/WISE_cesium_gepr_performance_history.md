# CesiumGEPR Performance & Architecture History

This document serves as a historical record of the performance bottlenecks, memory leaks, and clipping issues encountered during the development of the `CesiumGEPR.h` renderer (specifically while targeting the high-fidelity Christ the Redeemer Cesium Ion dataset), along with the solutions that succeeded and failed.

## 1. Memory Leaks & OOM (Out of Memory) Crashes
**The Problem:** The application was frequently crashing due to OOM errors when traversing deep into the 3D Tileset hierarchy. The constant allocation and deallocation of `std::vector` objects for payloads and geometry buffers caused severe heap fragmentation.
**Solutions Tried:**
- ❌ *Standard C++ RAII / Smart Pointers:* Initially tried wrapping payloads in `std::unique_ptr` and carefully tracking lifecycle. This failed because the raw volume of allocations still overwhelmed the system and caused stuttering.
- ✅ *Data-Oriented Design (DOD) & Linear Arena:* We completely stripped out `new`/`delete` and `std::vector` inside the hot paths. We introduced `Alice::LinearArena` pre-allocated at startup (e.g., 2GB-4GB). All tile structures, JSON parsing trees, and render resources are now allocated linearly from this arena. 
- ✅ *Pre-allocated Scratch Buffers:* We replaced per-tile `vbo`/`ebo` vectors with global `g_ScratchVbo` and `g_ScratchEbo` arrays (reused every frame) for `cgltf` mesh extraction.
- ✅ *Network Buffer Clamping:* Updated the `libcurl` write callback (`wc`) to respect a strict `FetchBuffer.capacity`. If a fetched tile exceeds the memory boundary, the download is aborted to protect against OOM.

## 2. Rendering Stutter & Network Blocking
**The Problem:** The viewer would completely freeze while downloading tile payloads (`.b3dm` or `.json`), ruining the interactive experience.
**Solutions Tried:**
- ❌ *`std::thread` per request:* Spawning a new OS thread for every tile fetch quickly exhausted thread pools and caused synchronization nightmares with OpenGL context boundaries.
- ✅ *Asynchronous `curl_multi` I/O:* Transitioned the network layer to use `curl_multi_perform` within `CesiumNetwork::UpdateAsync()`. 
- ✅ *Concurrency Limits:* Introduced a hard cap on concurrent async requests (e.g., `< 20`) to prevent overwhelming the network pipe. We also added an `isLoading` boolean to the `Tile` struct to prevent redundant duplicate requests for the same tile.

## 3. Screen-Space Error (SSE) & LOD Skipping
**The Problem:** The engine was wasting time downloading and rendering low-resolution proxy tiles instead of jumping to the high-detail geometry.
**Solutions Tried:**
- ❌ *Static Depth Limits:* Hardcoding traversal depth limits resulted in either missing geometry or loading too much invisible data.
- ✅ *Rigorous SSE Math:* We fixed the distance-to-camera calculation by explicitly transforming the tile's AABB center into View Space and extracting `-viewPos.z`. We then projected the `geometricError` into Normalized Device Coordinates (NDC) to calculate pixel error accurately.
- ✅ *Aggressive Payload Skipping (`skipPayload`):* If a tile's SSE dictates that it needs refinement (error > 16 pixels) and it possesses children, we actively *skip* loading its payload and immediately traverse to its children, saving massive amounts of bandwidth.

## 4. Near Clip Precision & Z-Fighting
**The Problem:** Shadows were clipping incorrectly, and distant terrain suffered from massive Z-fighting. Close-up geometry would also clip through the camera due to standard near-plane limitations.
**Solutions Tried:**
- ❌ *Pushing the Near Plane:* Changing the near clip plane from `0.1f` to `1.0f` or `10.0f` broke the immersion when zooming in close to the statue.
- ✅ *Infinite Reversed-Z Projection:* We abandoned the standard OpenGL depth range (`GL_LESS`, clear to `1.0`) and implemented an `InfiniteReversedZProjRH` matrix.
  - Set `glClearDepth(0.0f)`.
  - Set `glDepthFunc(GL_GEQUAL)`.
  - This maps the near plane to `Z=1.0` (high float precision) and the infinite far plane to `Z=0.0`, entirely eliminating Z-fighting on distant Earth geometry while preserving `0.1f` near-clip precision.

## 5. Coordinate Spaces & Inverted Normals
**The Problem:** The geometry appeared black, inside-out, or poorly lit due to inverted normals and wrong axis orientations.
**Solutions Tried:**
- ✅ *Axis Swapping (Y-Up to Z-Up):* glTF payloads inherently use a Y-Up coordinate system, whereas 3D Tiles and Cesium Earth use Z-Up. We explicitly injected an axis swap during extraction (`up_x = lp.x, up_y = -lp.z, up_z = lp.y`).
- ✅ *RTC_CENTER Extraction:* Extremely large coordinate values caused floating-point precision loss. We parsed the `RTC_CENTER` (Relative To Center) offset from the `b3dm` feature table header and added it back *after* local transformations to maintain high-precision vertex positioning.
