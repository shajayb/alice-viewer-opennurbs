# Alice Viewer 2 - Pipeline Architecture

## 1. Infinite Reversed-Z Depth Buffer
To eliminate Z-fighting across massive CAD scales (1mm to 10km) and provide near-perfect depth precision at any distance, the pipeline implements an **Infinite Reversed-Z** strategy.

### Mathematical Foundations
- **Clip Control**: `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` remaps the depth range from `[-1, 1]` to `[0, 1]`, aligning with modern APIs like Metal and DX12.
- **Polarity**: The depth test is inverted to `GL_GREATER` with `glClearDepth(0.0)`. Near geometry resides at `Z=1.0` and far geometry at `Z=0.0`.
- **Projection Matrix**: The standard perspective matrix is modified to project the far plane to infinity:
  ```
  P[10] = 0.0f
  P[14] = zNear
  ```
  This distributes the 24-bit/32-bit floating-point depth precision exponentially where it is needed most: near the camera.

## 2. Flat-Shaded Branchless Rendering
The pipeline utilizes a high-performance fragment shader that computes screen-space normals on-the-fly to ensure sharp CAD edges without requiring vertex normal buffers.

- **Screen-Space Normals**: Computed via `normalize(cross(dFdx(v_ViewPos), dFdy(v_ViewPos)))`.
- **Branchless Logic**: Shading intensities are computed using pure arithmetic to maximize GPU occupancy and eliminate pipeline stalls.
- **Wireframe Overlay**: Achieved via a zero-cost dual-pass `glDrawArrays` on the same VBO. The "Fill" pass uses `glPolygonOffset(-1.0, -1.0)` to push shaded geometry slightly away from the camera (towards 0.0 in Reversed-Z space), allowing the "Line" pass to overlay without depth artifacts.

## 3. Chunked Vectorization Strategy
To minimize CPU-to-GPU overhead and maximize memory throughput, mesh ingestion is fully vectorized.

- **DOD Batching**: The `PrimitiveBatch` system utilizes a single contiguous `Vertex` array mapped to a `GL_STREAM_DRAW` VBO.
- **Vectorized Ingestion**: `AliceViewer::drawTriangleArray` copies mesh data in aligned chunks (multiples of 3) directly into the batch buffer. This reduces the number of function calls from $O(N)$ triangles to $O(N / BatchSize)$, significantly lowering the overhead of the C++ main loop.
- **Topological Alignment**: Flushes are strictly gated to primitive boundaries. This prevents "mesh fragmentation" where a single triangle might otherwise be split across two separate GPU draw calls.

## 4. Memory Authority
All transient mesh data and ingestion buffers are managed by the `Alice::LinearArena`. This ensures:
- **Zero Heap Allocations** during the active rendering loop.
- **Instant Deallocation** via arena reset.
- **Deterministic Footprint**: Pre-allocated 512MB region provides a hard ceiling on memory usage.
