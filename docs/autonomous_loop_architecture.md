# Autonomous Self-Healing Agent Architecture

## Overview
This document summarizes the autonomous, self-healing "Ping-Pong" loop architecture designed to enable relentless, unassisted C++ development and visual rendering refinement. The architecture operates on a strict division of labor between two distinct agent personas, orchestrated via a continuous feedback loop until mathematically and visually verifiable success is achieved.

## The Agents

### 1. The Architect (Root Agent)
**Configuration File:** `gemini.md`
**Role:** Lead C++ Graphics Architect.
**Responsibilities:**
- Manages the `LONG TERM GOAL` and the `MASTER PLAN`.
- Formulates highly specific `DIRECTIVE`s for the Executor sub-agent.
- **The Adversarial Mandate:** Never inherently trusts the Executor. The Architect rigorously interrogates the Executor's output (build logs, console output, and crucially, semantic visual analysis of rendered framebuffers). 
- If the Executor fails, crashes, or hallucinates success (e.g., claiming a geometry rendered when the framebuffer is blank), the Architect *must not* yield back to the user. Instead, it immediately invokes the Executor again with a highly critical `REWORK` directive.

### 2. The Executor (Sub-Agent)
**Configuration File:** `.agents/executor.md`
**Role:** Aggressively fast C++ implementer.
**Responsibilities:**
- Strictly obeys the Architect's `DIRECTIVE`.
- Implements C++ code strictly adhering to a Data-Oriented Design (DOD) paradigm (e.g., avoiding heap allocations).
- Handles the compilation pipeline (CMake/Ninja) and programmatic execution of tests.
- Always concludes its run by emitting an `[AGENT_HANDOFF]` JSON payload containing the build status, execution exit code, and paths to the generated proof (logs/images), before yielding back to the Architect.

## The "Ping-Pong" Protocol (The Loop)
The core of the autonomy lies in the forced continuous execution loop:
1. **Plan & Direct:** Architect formulates Step N of the Master Plan into a `DIRECTIVE`.
2. **Execute:** Architect invokes the Executor.
3. **Implement & Test:** Executor modifies code, runs `ninja -C build`, executes the headless binary, captures the framebuffer, and returns the `[AGENT_HANDOFF]` status.
4. **Interrogate:** Architect evaluates the handoff.
   - **Failure/Hallucination:** Architect generates a `REWORK` directive pointing out exact log failures or visual discrepancies (e.g., "The image is black, the normals are inverted") and triggers Step 2.
   - **Success:** Architect moves to Step N+1 of the Master Plan.
5. **Halt Condition:** The loop *only* terminates and returns to the human user when 100% of the Success Criteria are verifiably met.

## The Unit Test (`UNIT_TEST.prompt`)
To benchmark and harden this architecture, a "Green Screen Protocol" (later evolved to the "Red Cube/Sphere" test) was established.
- **Goal:** Produce a specific geometric layout (e.g., 5x5x5m Red Cube, 2.5m hovering Red Sphere, 25x25m White Ground Plane) using a defined camera perspective and lighting.
- **Compile Criteria:** 0 warnings, 0 errors, strict DOD adherence.
- **Semantic Criteria:** The Architect must perform semantic image analysis on `framebuffer_beauty.png` to confirm the geometric shapes, colors, framing, and cast shadows mathematically align with the prompt requirements. This prevents the Executor from bypassing the test with empty or incorrect renders.

## Key Resilience Mechanisms
- **Headless Pipeline Integration:** The loop relies heavily on the program's ability to run a single frame and dump `.png` framebuffers natively without opening OS windows, ensuring the Architect can "see" the output in a CI/CD environment.
- **Refusal to Halt:** The root persona is explicitly forbidden from halting the process to ask the human for help regarding compilation errors or logic bugs, forcing it to self-heal.
