# AliceViewer Pipeline Stabilization & OSM Asset Architecture

## Overall Goal: High-Fidelity OSM Tile Streaming & Rendering
The overarching objective of this project phase was to achieve an autonomous, headless pipeline capable of streaming, rendering, and exporting high-fidelity 3D architectural landmarks (specifically St. Paul's Cathedral) using the **Cesium OpenStreetMap (OSM) Buildings** dataset (Asset ID `96188`).

### Strategies, Mathematics, & Dead Ends
- **Dead End (The Wireframe City):** Initial renders produced low-poly, wireframe-shaded extruded blocks. We mistakenly attempted to hardcode the Google Earth Photorealistic (GEPR) asset (`2275207`) and artificially inflate the `targetSSE` (Screen Space Error) values.
- **Solution (CGLTF Memory Leak & Async Stalls):** The high-fidelity OSM tiles (containing millions of vertices) were silently crashing the pipeline due to an Out-Of-Memory (OOM) error in the `cgltf_alloc` bump allocator (`Alice::g_Arena`). 
  - **Fix:** Switched `cgltf_alloc` to standard heap `malloc` and implemented strict `curl_multi_remove_handle` garbage collection to prevent network stall cascades.
- **Mathematics (Frustum Culling Initialization):** The `AABB::expand()` bounds were correctly computed, but `initialized = true` was never flagged for root OSM tiles, breaking frustum culling. Setting this flag properly ensured the renderer efficiently clipped off-screen tiles, saving critical memory.

---

## Sub-Goal 1: .3DM Export Parity & Precision Culling
The objective was to serialize the high-fidelity geometry residing in GPU memory into a universally readable `.3dm` architectural model that precisely mirrored the renderer's viewport.

### Strategies, Mathematics, & Dead Ends
- **Dead End (The 83MB Planet Dump):** Initial `.3dm` export logic independently traversed the tileset tree, ignoring the active render state. This exported the entire planet's low-res tiles alongside the high-res POI, resulting in bloated 83MB meshes.
- **Strategy (TotalVBO Parity):** We redirected the export pipeline to serialize directly from the `totalVBO` buffer. This guaranteed 1:1 LOD matching with the active camera frustum and inherently encoded the Z-up East-North-Up (ENU) coordinate transformation performed during the `processNode` pass.
- **Dead End (The 47MB Horizon Artifacts):** To cull distant meshes, we initially used a distance filter (`distToOrigin > 1500.0f`) that measured distance to the *center* of the tile's bounding box. Massive overarching landmark tiles (like St. Paul's) have centers far from the origin, causing them to be erroneously deleted while distant horizon quadtree leaf-nodes remained.
- **Mathematics (distToBox):** We replaced the center check with a `distToBox` mathematical formulation (`sqrt(dx*dx + dy*dy + dz*dz)`) to calculate the closest edge of the AABB to the origin. This flawlessly preserved the landmark tiles that intersected the origin while aggressively culling the 47MB background horizon.
- **Success (Additive vs. Replacement LOD):** Even with `distToBox`, St. Paul's Cathedral was missing because Cesium uses `"refine": "ADD"` for photogrammetry landmarks and `"REPLACE"` for generic buildings. Our parser treated everything as `"REPLACE"`. We modified the `Tile` struct to parse and inherit `uint8_t refineMode`. By adjusting the gate to `node->isRefined = shouldRefine && childrenLoaded && (node->refineMode == 0)`, the additive St. Paul's Cathedral was permanently preserved in the `renderList`, resulting in a pristine 13.0MB export.

---

## Sub-Goal 2: Serialization State-Gating (export_bin & Stencils)
The objective was to prevent the continuous orchestration loop from polluting the `OUTPUT` directory with corrupted caches or blank visual buffers when testing isolated variables.

### Strategies & Successes
- **Strategy (Boolean JSON Flags):** We introduced strict structural boolean toggles into `locations.json` (`export_bin`, `export_3dm`, `export_stencil`).
- **Success (State-Gated Caching):** The pipeline was modified so that if `export_bin` is set to `false`, the engine fundamentally skips writing the binary `.bin` mesh cache. Furthermore, we linked the `export_bin` flag to the cached image generation pass. If `.bin` generation is disabled, the system bypasses capturing the `_cached_framebuffer.png` and `_cached_stencil.png`, preventing blank or stale images from triggering false negatives during Semantic Verification.

---

## Agentic System Architecture Changes
To enforce these mechanical fixes, the underlying AI Agent architecture was fundamentally hardened.

### 1. The Autonomous "Ping-Pong" Loop
The system rigidly formalized the "Lead Architect" and "Executor" relationship in `GEMINI.md`. The Architect operates a continuous, headless C++ loop, refuses to yield to the user, and iteratively re-invokes the Executor based on strict success metrics.

### 2. The Adversarial Mandate
We implemented an "Adversarial" approach to interpreting log files. Because the C++ pipeline is prone to hallucinating success (e.g., logging "Export Complete" despite outputting a 0-vertex file), the Architect is mandated to mistrust internal text logs.
- **Semantic Verification:** Success is exclusively defined by the Architect utilizing its multi-modal vision to "look" at the generated `framebuffer.png` and compare it against the `semantic_criteria` defined in `locations.json`. Only when the visual output unequivocally proves structural fidelity does the loop halt.
