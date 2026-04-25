# WISE Build Optimization Log

## Overview
This document summarizes the primary optimization strategies implemented to drastically improve the build and execution speeds of the `AliceViewer` headless rendering pipeline.

## 1. Header-Only Isolation Architecture
The most significant reduction in C++ compile times (achieving sub-10-second `ninja` incremental builds) was accomplished by isolating all traversal, memory, rendering, and state-machine logic within a single header file: `cesium_gepr_and_osm.h`.
- **Why it works:** By avoiding extensive modifications to `AliceViewer.cpp` and other core `.cpp` files, the compiler does not have to relink or parse the entire application dependency graph. Only the translation unit that includes the header is touched, leading to extremely fast, iterative compile cycles.

## 2. Runtime Configuration via JSON (`locations.json`)
We transitioned hardcoded asset streaming coordinates, LOD thresholds, and export flags to a JSON-based runtime configuration protocol.
- **Why it works:** This entirely eliminated the need to recompile the C++ executable between rendering tests. Changes to camera distance, pitch, asset flags (`load_osm`, `load_gepr`), or visual semantic criteria are parsed dynamically at launch.

## 3. Headless Pipeline Early Exit Heuristics
The original execution loop relied on arbitrary frame counts (e.g., waiting 1000 frames) before transitioning states or exiting.
- **Why it works:** We implemented an active heuristic monitoring the `CesiumNetwork` queue. The application dynamically exits or transitions states as soon as the tile fetch queue settles (`g_AsyncRequests.size() == 0` and `settledFrames > 2`), dropping headless execution times from minutes down to ~15-25 seconds per view.

## 4. Concurrent Network Fetch Queue Aggression
We removed the previous limitations on concurrent socket requests, scaling the `CesiumNetwork` maximum async connections from 20 up to 128.
- **Why it works:** This saturates the network bandwidth, allowing massive batches of leaf-node architectural tiles (e.g., the St. Paul's Cathedral dense urban layout) to download in parallel rather than blocking sequentially.

## 5. Terminal Spam Suppression
We drastically reduced the I/O bottleneck caused by the C++ engine printing debug telemetry every single frame to standard output.
- **Why it works:** Massive terminal I/O is notoriously slow. By aggregating log output to periodic milestones (e.g., `[GEPR] Frame X, Depth: Y, Vertices: Z`), the Executor sub-agent is no longer throttled by text buffering, further accelerating execution and parsing speed.
