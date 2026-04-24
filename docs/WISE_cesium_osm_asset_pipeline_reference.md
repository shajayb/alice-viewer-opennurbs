# CESIUM OSM BUILDINGS: Asset Pipeline Reference

This document serves as the definitive reference manual for cold-starting development on Cesium Ion OpenStreetMap (OSM) Buildings (`Asset ID: 96188`). It covers the end-to-end pipeline from network discovery to rendering and local caching, strictly focusing on OSM specifics and incorporating critical bugs resolved in previous autonomous runs.

## 1. Requesting & Network Discovery
*   **Asset ID**: `96188`
*   **Handshake**: Use `IonDiscovery::discover` to request an endpoint from `https://api.cesium.com/v1/assets/96188/endpoint`.
*   **Authentication**: Requires a valid `CESIUM_ION_TOKEN`. The handshake returns a JSON object containing the `url` (Base URL) and a new session `accessToken`. This token MUST be propagated into the HTTP `Authorization: Bearer` headers of all subsequent `.json` and `.glb` requests.

## 2. Loading & Streaming Meshes
*   **Region Bounding Volumes**: Unlike GEPR, OSM tiles heavily rely on `region` bounding volumes (south, west, north, east in radians). This must be correctly parsed and mathematically converted into an ECEF bounding sphere (center point and radius) to allow for frustum culling.
*   **Root Placeholder & SSE**: The root node of the OSM global tileset is often an empty placeholder. The `RenderList` must dynamically progress deeper than depth 1. If your Screen-Space Error (SSE) refinement logic is improperly tuned for OSM scale, the tileset will stall at the root, resulting in a blank grey screen.
*   **OSM Child Overload**: OSM nodes can have hundreds of children. To avoid stack overflows during recursive `traverse()` calls, ensure child arrays are dynamically allocated on the heap or a linear arena (`Alice::g_Arena`), rather than using fixed-size stack arrays.

## 3. Displaying Streamed Meshes
*   **Coordinate Dequantization**: OSM tiles frequently store massive absolute ECEF coordinates directly within glTF Accessors (often without `RTC_CENTER`). To prevent catastrophic 32-bit float truncation, you MUST use `cgltf_accessor_read_float` to safely extract and dequantize coordinates into `double` precision before projection.
*   **ENU Transformation**: Transform the ECEF positions against `g_OriginEcef` using a dynamic East-North-Up matrix (`g_EnuMatrix`).
*   **Visual Clarity (The White Blob)**: OSM Buildings are untextured meshes. Without basic depth or normal shading, they merge into an unrecognizable white blob. For headless validation, use a flat grey geometry color (`0.8f`) with double-pass rendering (Fill + Wireframe Overlay) to extract structural edges.
*   **Z-Clipping Terrain Skirts**: OSM building data includes deep underground "skirts" to seamlessly integrate with terrain. Implement Z-clipping in the fragment shader (e.g., `if (vVP.z < -2.0) discard;`) to hide these downward-stretching artifacts.

## 4. Serializing to Cache (Aggregation)
*   **State Aggregation**: To export the active streaming scene to disk, loop over the `RenderList` once streaming stabilizes.
*   **Monolithic Buffers**: For each valid tile, transform its vertex positions using its world matrix multiplied by a Y-Up alignment matrix (`{1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1}`). Concatenate all `float` coordinates and `uint32_t` indices into monolithic `std::vector` buffers.
*   **Binary Output**: Write the structure sequentially to a binary file (e.g., `cache_0.bin`):
    1. `uint32_t vertexCount`
    2. `uint32_t indexCount`
    3. `float vboData[]`
    4. `uint32_t eboData[]`

## 5. Deserializing from Cache & Displaying
*   **Loading Cache**: Open the target `.bin` file, read the two `uint32_t` counts, and dump the remaining binary data directly into pre-allocated memory buffers.
*   **VRAM Upload**: Generate a single VAO, VBO, and EBO. Upload the contiguous arrays to the GPU.
*   **Rendering**: Draw the entire scene with a single `glDrawElements(GL_TRIANGLES, totalIndices, GL_UNSIGNED_INT, 0)` call. This bypasses the entire bounding volume / frustum traversal hierarchy and provides maximum framerate performance.
