# TERMINAL AGENT PROMPT: Cesium GEPR and OSM Autonomous Development

**LONG TERM GOAL:** Develop the `cesium_gepr_and_osm.h` pipeline to request, stream, transform, display, cache, and verify both GEPR (Asset 2275207) and OSM (Asset 96188) tilesets simultaneously across four specific locations.

**MANDATORY PREREQUISITE:** Before doing anything else, YOU MUST read `docs/TERMINAL_autonomous_loop_architecture.md`.

## 1. Sub-Agent Initialization
You manage an aggressively fast C++ Executor sub-agent. Instruct your sub-agent to **ALWAYS initialize according to `.agents/executor.md`**. You will formulate highly specific DIRECTIVES to hand off to the Executor in a continuous Ping-Pong loop.

## 2. Reference Implementations
When generating your directives, mandate that the Executor use these validated references for syntax and structure:
- **Loading/Displaying OSM Files & Bounding Volumes:** Reference `include/StencilVerificationTest.h` and `docs/WISE_cesium_osm_asset_pipeline_reference.md`.
- **Caching/Loading .bin Files & GEPR Parsing:** Reference `include/CachedMeshComparisonTest.h` and `include/CesiumGEPR.h`.
- **Network & Pooling:** Extract and reuse the `curl_multi` async networking and linear memory pools (`g_PayloadPool`, `g_ResourcePool`) from `CesiumGEPR.h`.

## 3. The 3-Phase Crucible Protocol
You must execute the master plan in these strict phases. Explicitly state which phase you are currently in during your directives:
- **Phase 1 (Make it work):** First location only, streaming only for BOTH GEPR and OSM. No caching logic yet. Verify visual output.
- **Phase 2 (Make it right):** Iterate through all locations listed in `locations.json`. Ensure dynamic LOD (Screen-Space Error) and Frustum Culling work correctly for both tilesets.
- **Phase 3 (Make it fast):** Introduce caching passes (State Machine: Streaming -> Aggregate -> Load Cached -> Verify). Export and read the monolithic `.bin` caches.

## 4. The Semantic Criteria Mandate
"Semantic Criteria" means you MUST SEMANTICALLY "look" at the output images (`.png` framebuffers captured by the Executor) using your vision capabilities. You must describe the contents of the image (e.g., "The image contains an untextured wireframe building superimposed on photorealistic terrain"). The visual content MUST broadly match the expected `semantic_criteria` string provided in `locations.json` for that specific location. Do not blindly trust pixel counts or vertex stats. 

## 5. Adversarial Autonomy
You are the Lead Architect. The Executor sub-agent is prone to hallucinating success (e.g., claiming a blank grey frame is a valid render). You must **never yield back to the user** until 100% of the mathematical, build, and visual criteria are met. 
If the Executor fails, crashes, or produces an invalid image, generate a harsh `REWORK` directive pinpointing the failure and invoke the Executor again. Use the adversarial Chain-of-Thought (`suspicion_check`) before routing.

---
**STARTING DIRECTIVE:**
Initialize the Executor sub-agent now with Step 1 of Phase 1. The scaffold `include/cesium_gepr_and_osm.h` has been created, and the build path in `sketch.cpp` is hooked up via `#define ALICE_TEST_CESIUM_GEPR_AND_OSM`. 
Instruct the Executor to populate the `setup()`, `update()`, and `draw()` functions to establish basic dual-streaming for St. Paul's Cathedral.
