# Cesium GEPR Implementation: Positive Discoveries & Performance Wins

This document highlights the successful architectural decisions and clever implementation tricks discovered during the integration of Google Earth Photorealistic (GEPR) 3D Tiles.

## BRILLIANT (Architectural Wins)

### 1. Zero-Allocation Render Loop (Alice::g_Arena)
*   **The Discovery:** By using a pre-allocated `LinearArena` (512MB), we achieved zero heap allocations during the high-frequency traversal and rendering stages. 
*   **The Impact:** This eliminates GC pauses and fragmentation, ensuring that even as thousands of tiles are parsed and uploaded to the GPU, the CPU frame time remains perfectly consistent.

### 2. High-Precision Origin Shifting (ECEF to local ENU)
*   **The Discovery:** Standard 32-bit floats jitter when using raw ECEF coordinates (e.g., 6.3 million meters). By calculating a local ENU (East-North-Up) matrix once at the Christ the Redeemer origin and performing all vertex offsets in double precision during the upload phase, we achieved rock-steady rendering.
*   **The Code:**
    ```cpp
    CesiumMath::DVec3 enu = {
        g_EnuMatrix[0]*(wp.x-g_OriginEcef.x) + g_EnuMatrix[1]*(wp.y-g_OriginEcef.y) + g_EnuMatrix[2]*(wp.z-g_OriginEcef.z),
        // ...
    };
    ```

### 3. Automated Visual Validation via Occlusion Queries
*   **The Discovery:** Using `GL_SAMPLES_PASSED` queries to programmatically verify that "something is actually on screen."
*   **The Impact:** This allows the AI agent to mathematically prove success without needing a human to look at the screenshot. If `pixel_coverage_percentage > 2%`, we know the camera is correctly looking at the terrain.

---

## CLEVER (Implementation Tricks)

### 1. The "Query-Sticky" URI Resolver
*   **The Discovery:** Realizing that GEPR tiles require the `?session=...&key=...` query string to be appended not just to the root, but to every recursively loaded sub-tile and `.glb`.
*   **The Trick:** Modified `resolveUri` to extract the query string from the initial discovery endpoint and automatically merge it into every child request, handling both relative `/` and absolute `http` paths.

### 2. Google-Style Discovery Fallback
*   **The Discovery:** Google's asset discovery JSON structure differs from standard Cesium Ion responses (URL is nested in `options`).
*   **The Trick:** Implemented a non-strict JSON parser that uses `.value("options", ...)` to look for the URL in multiple possible locations, ensuring compatibility with all Cesium Ion asset types.

### 3. Headless Framebuffer Capture
*   **The Discovery:** Successfully integrating `stb_image_write` directly into the render loop to trigger a PNG save on exactly frame 600.
*   **The Trick:** Combining `glReadPixels` with `stbi_flip_vertically_on_write(1)` to create a production-ready reference image in a purely headless Linux environment.

---

## HANDY (Workflow & Tooling Tips)

### 1. xvfb-run for Graphics CI
*   **Tip:** Using `xvfb-run -a ./AliceViewer --headless` allowed us to run a full OpenGL 4.0 core profile pipeline on a server without a physical monitor or GPU driver.

### 2. Log Throttling via Frame Modulo
*   **Tip:** Using `if (g_FrameCount % 100 == 0)` to print camera ECEF coordinates. This kept the `executor_console.log` small enough for the AI context window while still providing enough data to debug camera movement.

### 3. Culling as a Speed Multiplier
*   **Tip:** Hardcoding a 100km "Rio-Sphere" culling check during the initial tileset parse. This prevented the engine from even attempting to load data from the other side of the planet, reducing setup time from minutes to seconds.

### 4. Throttled Frame-Based Streaming
*   **The Discovery:** Synchronous network fetching of a deep tileset caused the master orchestrator to kill the app due to long frame times.
*   **The Trick:** Implementation of a `g_TilesLoadedThisFrame` counter. By limiting the fetch to 1-2 tiles per frame, the app remained responsive and bypassed the anti-hang watcher.

### 5. Atomic Resource Commitment
*   **The Discovery:** Summing vertex counts from tiles while they were being loaded in a background thread (or mid-fetch) resulted in garbage data and integer overflows.
*   **The Trick:** Fully populating a local `RenderResources` struct and only assigning it to the `Tile*` at the very end of a successful load.
