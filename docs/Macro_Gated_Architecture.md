# Architecture: Weak Symbols and Macro-Gated Tests

The AliceViewer framework elegantly combines C++'s weak symbol linkage, single-header design, and macro-gating to allow for extremely fast, isolated iteration. Here is how `AliceViewer.cpp`, `AliceViewer.h`, `sketch.cpp`, and the agent rules in `executor.md` interlock to form this clean build path:

## 1. The Core Application (`AliceViewer.cpp` & `AliceViewer.h`)
`AliceViewer.cpp` contains the heavy, stateful OpenGL initialization (GLFW, GLAD, ImGui) and the main event loop. It relies on a set of `extern "C"` functions (`setup`, `update`, `draw`, `keyPress`, etc.) to delegate the actual rendering logic.

Crucially, it defines these functions as **weak symbols**:
```cpp
#ifdef _MSC_VER
#pragma comment(linker, "/alternatename:setup=def_setup")
// ...
#else
void __attribute__((weak)) setup() {}
// ...
#endif
```
This means `AliceViewer.cpp` provides "dummy" implementations. If the linker finds a strong definition of `setup()` anywhere else in the project, it will seamlessly discard the dummy and use the strong one, without throwing a multiple-definition error.

## 2. The Agent Execution Engine (`.agents/executor.md`)
The `executor.md` explicitly instructs the Executor sub-agent on how to develop features without breaking the core:
* **Architectural Isolation:** "Encapsulate all new algorithms within a single `.h` file in `include/`. Do not cross-pollinate with other WIP headers."
* **Macro-Gated Tests:** "Wrap render calls in `src/sketch.cpp` with `#ifdef ALICE_TEST_[NAME]`."

## 3. The Assembly Point (`src/sketch.cpp` & Single Headers)
The `sketch.cpp` file acts as the primary "switchboard" or testing sandbox. It is compiled alongside `AliceViewer.cpp`, making it the perfect place to inject the strong symbols.

When a feature is developed (like `CesiumGEPR.h`), the code is completely encapsulated as a Data-Oriented, single-header library. At the very bottom of the header, it includes an injection block:
```cpp
#ifdef CESIUM_GEPR_RUN_TEST
extern "C" void setup() { CesiumGEPR::setup(); }
extern "C" void update(float dt) { CesiumGEPR::update(dt); }
extern "C" void draw() { CesiumGEPR::draw(); }
#endif
```

**To run the unit test, the build path is simply assembled by:**
1. Modifying `sketch.cpp` to define the gating macro: `#define CESIUM_GEPR_RUN_TEST`
2. Including the target header: `#include "CesiumGEPR.h"`

## The Clean Build Path Result
This architecture is brilliant for agentic workflows because:
* **Zero Boilerplate:** The Executor doesn't need to modify `main.cpp`, `CMakeLists.txt`, or the core event loop to add a new test.
* **Instant Compilation:** The heavy framework code in `AliceViewer.cpp` is built once and rarely changes. The Executor only modifies `sketch.cpp` and the isolated `.h` file, leading to blazing-fast incremental `ninja` builds.
* **Complete Isolation:** If a test fails or crashes, it is completely contained within its header file. Swapping to a different test (e.g. standard geometry vs Cesium) is just a matter of changing a single macro definition in `sketch.cpp`.
