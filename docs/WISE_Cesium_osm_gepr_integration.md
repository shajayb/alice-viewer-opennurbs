# WISE: Cesium OSM and GEPR Integration Analysis

## Overview
This document summarizes the architectural differences and challenges discovered when integrating Google Earth Photorealistic (GEPR) assets and OpenStreetMap (OSM) assets within the unified `cesium_gepr_and_osm.h` headless rendering pipeline.

## The Traversal Bomb: Spatial LOD Enforcement vs. Depth Limitations

The primary discrepancy that allowed OSM assets to render beautifully while causing catastrophic stalling for GEPR assets lies in how spatial Level of Detail (LOD) was enforced.

### The OSM Optimization
To guarantee that the target architectural geometry (e.g., St. Paul's Cathedral) and its surrounding urban context loaded correctly, the pipeline utilized an unconditional spatial refinement rule:
```cpp
if (distToOrigin < 2000.0f) shouldRefine = true;
if (depth < 4) shouldRefine = true;
```
**Why it succeeds for OSM:**
OpenStreetMap asset quadtrees are extremely shallow (typically bottoming out at Depth 4 or 5), and the payloads consist of lightweight, untextured polygons. Forcing the engine to refine *everything* within a 2-kilometer radius simply causes it to rapidly hit the leaf nodes, pulling in the entire city blocks efficiently without overloading memory or bandwidth.

### The GEPR Bottleneck
**Why it destroys GEPR performance:**
GEPR tilesets are massive, heavily textured `.b3dm` files with deep hierarchical quadtrees extending to **Depth 25+**. 
Applying the unconditional `2000.0f` radius forced refinement completely overrides the dynamic Screen-Space Error (`targetSSE`) optimizations. As a result, the engine attempts to drill down to Depth 25 for *every single node* within the 2km radius. 

This triggers an explosive combinatorial fetch:
- **Bandwidth Flooding:** Tens of thousands of massive, high-resolution photogrammetry tiles are queued simultaneously.
- **Queue Saturation:** Even with our hyper-aggressive async queue of `128` simultaneous sockets, the bandwidth chokes, and the headless execution stalls waiting for the bottomless depth of GEPR to download.

## The Solution: Clamped Refinement
In the original separated `CesiumGEPR.h` pipeline, forced refinement was strictly clamped by both distance *and* depth to prevent this explosion:
```cpp
if (distToOrigin < 50.0f && depth < 25) shouldRefine = true; 
```
This ensured that only the immediate target (a tiny 50-meter radius) received maximum resolution, while the surrounding 2km radius relied on the standard `targetSSE = 16.0` heuristic, minimizing payload size and bandwidth consumption.

## Future Recommendations for Unified Pipeline
To achieve lightning-speed GEPR loading while maintaining OSM compatibility, the pipeline must:
1. Remove unconditional wide-radius spatial forcing (`distToOrigin < 2000.0f`).
2. Re-implement depth clamping specific to the active asset type.
3. Rely primarily on `targetSSE` for LOD management, adjusting the target error threshold dynamically rather than overriding the quadtree traversal entirely.

## Implemented Solutions for GEPR (Iteration 2)
### 1. Dynamic targetSSE & Depth Clamping
To balance the `targetSSE` heuristic against the massive scale of GEPR root nodes (which have an intrinsic SSE of ~12.89), we implemented a stepped ring of detail based on surface proximity (`distToBox`), falling back to a global target of 8.0 to ensure Earth root nodes refine:
- `< 2000.0f` -> `targetSSE = 4.0` (Low-poly city context)
- `< 800.0f` -> `targetSSE = 2.0` (Medium density urban blocks)
- `< 200.0f` -> `targetSSE = 1.0` (High architectural fidelity)
- `< 150.0f` -> `shouldRefine = true` **WITH** a strict `depth < 20` clamp (Targeting the St. Paul's Cathedral Dome).

### 2. Camera Frustum Precision (Artifact Fix)
When rendering large architectural meshes from an orbital distance (e.g., 1000m), an aggressive near clip plane (e.g., `0.1f`) wastes floating-point precision, causing depth buffer Z-fighting and severe fragmented culling of foreground meshes in the Reversed-Z pipeline.
**Fix:** The near clip plane in `makeInfiniteReversedZProjRH` has been pushed to `2.0f`. This ensures depth bits are concentrated exactly where the Cathedral geometry sits, resolving the fragmented "green screen" artifacts without affecting distant horizons.

### 3. Build & Headless Execution Optimizations
Our fast "Ping-Pong" iteration loop relies on incredibly tight execution cycles. The build path has been audited and structurally hardened to minimize overhead:
- **LLD Linker:** `lld-link.exe` is utilized, which dramatically cuts down linking time for the large pre-compiled `vcpkg` static libraries (Curl, GLM, OpenNurbs).
- **Ninja Build System:** Rapid parallel C++ object compilation.
- **Implicit Release Optimization:** `build_and_run.bat` specifically omits `-DCMAKE_BUILD_TYPE=Debug`. This triggers the `else()` branch in `CMakeLists.txt`, automatically building `AliceViewer.exe` with `/MT /O2 /Ob2`. This guarantees the headless execution utilizes hyper-optimized machine code for the massive tree traversals, saving seconds off the async parsing loop.

## Identified Missing Tricks & Upcoming Exploration Trajectories
A rigorous review of historical successful architectures (e.g. `anti_gravity_cesium_gepr_success.md` and the `avon-ces-int-1` parallel branch) has revealed several critical mechanisms that did NOT survive the unified integration pipeline in `cesium_gepr_and_osm.h`. These form the core of our next exploration trajectories:

### Trajectory 1: Y-Up to Z-Up Coordinate Fix
- **The Issue:** `glTF` payloads inside `.b3dm` files are natively Y-Up, but Cesium's ECEF `RTC_CENTER` is strictly Z-Up. Applying local coordinates directly to the ECEF offset without rotation causes extreme spatial tearing ("giant triangles").
- **The Fix:** We must inject a strict rotation matrix (`x -> x, y -> -z, z -> y`) into the `cgltf` parser *before* applying the `RTC_CENTER` translation.

### Trajectory 2: Aggressive Intermediate LOD Skipping
- **The Issue:** Unconditionally fetching multi-megabyte payloads for Earth-scale nodes when our target is a specific Cathedral causes unnecessary bandwidth flooding.
- **The Fix:** Implement a strict logic bypass: if `shouldRefine == true` and a node has children, we must skip fetching its payload entirely and proceed straight to the highest available detail.

### Trajectory 3: Distance Metric (`distToOrigin` vs. `distToBox`)
- **The Issue:** Using surface proximity (`distToBox`) can cause massive Earth-scale root nodes to constantly evaluate to a distance of `0.0`, triggering unintended forced refinement.
- **The Fix:** Evaluate reverting to `distToOrigin` (box center) to naturally filter out massive parent nodes whose centers are located deep underground or across the ocean.

### Trajectory 4: The Hard Horizon Cull (5KM Limit)
- **The Issue:** Even with dynamic SSE, distant high-contrast geometry on the horizon can still trigger refinement, eating bandwidth.
- **The Fix:** Implement a brute-force distance cull (e.g., `distToBox2 > 25000000.0f` for nodes deeper than depth 8) to explicitly prevent high-resolution textures from loading beyond 5 kilometers.
