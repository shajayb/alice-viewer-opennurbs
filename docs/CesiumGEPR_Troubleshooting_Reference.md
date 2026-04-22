# Cesium GEPR Implementation: Troubleshooting & Architectural Reference

This document serves as a post-mortem and reference for the implementation of Google Earth Photorealistic (GEPR) 3D Tiles within the Alice Viewer.

## CATASTROPHIC (Logic & Infrastructure Killers)

### 1. Recursive Frame-Scene Loop
*   **Symptom:** Immediate Stack Overflow or Segmentation Fault on Frame 300.
*   **Root Cause:** The instruction to call `av->frameScene()` at Frame 300 triggered a scene redraw. Since the logic was `if (frame == 300) { av->frameScene(); }`, the redraw call re-entered the draw function at frame 300, creating an infinite loop.
*   **Fix:** Increment the frame counter **before** calling framing logic:
    ```cpp
    if (g_FrameCount == 300) { 
        g_FrameCount++; // Break the trigger condition
        av->frameScene(); 
    }
    ```

### 2. Query String Token Stripping (The 403 Wall)
*   **Symptom:** Every tile request returned `403 Forbidden` despite valid Bearer tokens.
*   **Root Cause:** Google Photorealistic Tiles require a session-specific token and API key passed via query parameters (`?session=...&key=...`). The original URI resolver stripped everything after the file extension.
*   **Fix:** A specialized `resolveUri` that preserves the query string from the initial discovery endpoint and appends it to all recursive tile URLs.

### 3. The "Empty Scene" Fallacy
*   **Symptom:** Logs claimed 200 OK for tiles, but `frustum_vertex_count` remained `0`.
*   **Root Cause:** Traversal was visiting nodes but failed to populate the `renderList`. Data was loaded into the Arena but never handed to the OpenGL draw loop.
*   **Fix:** Explicitly push the node to the render list once loading is confirmed:
    ```cpp
    if (node->isLoaded && node->contentUri[0] != '\0') {
        renderList[renderListCount++] = node;
    }
    ```

---

## PROBLEMATIC (Structural & API Mismatches)

### 1. Non-Standard Discovery JSON
*   **Symptom:** `nlohmann::json` type errors during setup.
*   **Root Cause:** GEPR returns a discovery body where the tileset URL is nested inside an `options` object (`j["options"]["url"]`), unlike standard Ion assets where it is at the root.
*   **Fix:** Use `.value("options", ...)` to robustly handle both Google-style and standard Cesium responses.

### 2. Hyper-Aggressive ECEF Culling
*   **Symptom:** Traversal stopped at the root node.
*   **Root Cause:** A hardcoded "Rio Culling" check intended to optimize speed was too tight. Slight floating-point variations in ECEF origins caused the target site to cull itself.
*   **Fix:** Expanded culling radius to 100km and added "Culled L%" debug logs.

### 3. Reversed-Z Depth Buffering
*   **Symptom:** Massive flickering or black screens.
*   **Root Cause:** The engine uses Reversed-Z (`glDepthFunc(GL_GEQUAL)`). Standard GLM or boilerplate projection matrices use 0-to-1 or -1-to-1 depth, which is incompatible.
*   **Fix:** Mandatory use of `AliceViewer::makeInfiniteReversedZProjRH`.

---

## ANNOYING-BUT-REWARDING (Language & Safety)

### 1. Printf %s on Raw Buffers
*   **Symptom:** `AddressSanitizer: heap-buffer-overflow`.
*   **Root Cause:** `printf`ing a `std::vector<uint8_t>` as a `%s` string without null termination.
*   **Fix:** Use the precision specifier: `printf("%.*s", (int)res.size(), (const char*)res.data())`.

### 2. Forward Reference Header Hell
*   **Symptom:** `Use of undeclared identifier 'g_FrameCount'`.
*   **Root Cause:** C++ top-down parsing. Classes in the middle of the file couldn't see static globals defined at the bottom.
*   **Fix:** Hoisted the `Alice` namespace global block (Arena, FrameCount, Client) to the absolute top of the header.

### 3. Implicit Tiling URL Templates
*   **Symptom:** No children found for high-detail tiles.
*   **Root Cause:** GEPR uses `implicitTiling` where children aren't listed in the JSON but must be generated using `{level}/{x}/{y}` templates.
*   **Fix:** String replacement logic in `traverse` to generate child URLs dynamically.

### 4. Stack Exhaustion via Recursive Traverse
*   **Symptom:** App silently exits with `Code: -1073741571` (0xC0000005) or `Exit Code: 1`.
*   **Root Cause:** Google GEPR tilesets are extremely deep. Deep recursive `traverse` calls exceeded the default 1MB Windows stack.
*   **Fix:** Implementation of an explicit depth limit (capped at 14 for the statue) and increasing the MSVC stack size via `#pragma comment(linker, "/STACK:8388608")`.

### 5. AliceViewer Coordinate Inversion (Alice Y-Up vs ENU)
*   **Symptom:** `pixel_coverage_percentage` was `0.00%` even after successful tile loads.
*   **Root Cause:** ENU (East-North-Up) maps Up to Z, but AliceViewer uses Y-Up. Mapping ENU directly to XYZ resulted in the camera looking at the ground or into space.
*   **Fix:** Corrected mapping: `Alice.X = East`, `Alice.Y = Up`, `Alice.Z = -North`. This correctly oriented the statue to face the camera.
