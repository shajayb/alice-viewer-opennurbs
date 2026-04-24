# Autonomous Self-Healing Agent Architecture

## Overview
This document summarizes the autonomous, self-healing "Ping-Pong" loop architecture designed to enable relentless, unassisted C++ development and visual rendering refinement. The architecture operates on a strict division of labor between two distinct agent personas, orchestrated via a continuous feedback loop until mathematically and visually verifiable success is achieved.

## The Agents

### 1. The Architect (Root Agent)
**Configuration File:** `GEMINI.md`
**Role:** Lead C++ Graphics Architect.
**Responsibilities:**
- The Architect is seeded with a `LONG TERM GOAL`, a `MASTER PLAN`, and strict `SUCCESS CRITERIA`.
- Autonomously pursues the GOAL and PLAN without yielding back to the user until the SUCCESS criteria are 100% verifiably met.
- Formulates highly specific `DIRECTIVE`s and hands off work to the Executor sub-agent.
- **The Adversarial Mandate:** Never inherently trusts the Executor. The Architect rigorously interrogates the Executor's output (build logs, console output, and importantly, semantic visual analysis of rendered framebuffers). 
- If the Executor fails, crashes, or hallucinates success, the Architect *must not* yield back to the user. Instead, it immediately invokes the Executor again with a highly critical `REWORK` directive.

### 2. The Executor (Sub-Agent)
**Configuration File:** `.agents/executor.md`
**Role:** Aggressively fast C++ implementer.
**Responsibilities:**
- Strictly obeys the Architect's `DIRECTIVE`.
- Implements C++ code strictly adhering to a Data-Oriented Design (DOD) paradigm (e.g., avoiding heap allocations).
- Handles the compilation pipeline (CMake/Ninja) and programmatic execution of tests.
- Always concludes its run by capturing the generated proof (logs/images) and returning it to the Architect.

## Success Criteria
The loop *only* terminates when both criteria paradigms are satisfied:

### 1. COMPILE Criteria
- **Zero Errors/Warnings:** The codebase must compile cleanly via `ninja -C build`.
- **DOD Mandates:** The implementation must adhere to strict Data-Oriented Design rules, ensuring no dynamic heap allocations (`std::vector`, `std::string`, `new`, `malloc`) occur within hot paths or rendering loops.

### 2. SEMANTIC Criteria
- **Visual Validation:** The Architect is mandated to visually analyze the image outputs (`.png` framebuffers) produced by the Executor.
- **Descriptive Matching:** The Architect must explicitly describe the contents of the generated image (e.g., *"tower in an urban district"*, *"dome shaped building in London"*).
- **Alignment:** The Architect then compares its visual description against the input (desired) description provided in the prompt (e.g., *"framed view of The Eiffel tower"*, *"view of the Dome of St Paul's cathedral"*). The two descriptions must match semantically for the test to pass.

## The "Ping-Pong" Protocol (The Loop)
The core of the autonomy lies in the forced continuous execution loop:
1. **Plan & Direct:** Architect formulates Step N of the Master Plan into a `DIRECTIVE`.
2. **Execute:** Architect invokes the Executor.
3. **Implement & Test:** Executor modifies code, compiles, executes the headless binary, captures the framebuffer, and returns the status.
4. **Interrogate:** Architect evaluates the handoff against the COMPILE and SEMANTIC criteria.
   - **Failure/Hallucination:** Architect generates a `REWORK` directive pointing out exact log failures or visual discrepancies (e.g., "The image is blank, the semantic description does not match") and triggers Step 2.
   - **Success:** Architect moves to Step N+1 of the Master Plan.

## Combating LLM Sycophancy & Hallucination
Evaluating visual outputs introduces vulnerabilities, particularly Affirmation Bias (multimodal LLMs trusting a text success report over a visually blank image). To combat this, the architecture relies on:
1. **Adversarial Chain-of-Thought (`suspicion_check`):** The prompt forces the Architect to formulate a counter-narrative *before* making a routing decision: *"Assume the Executor is lying to you and the code failed entirely. What specific visual evidence in the image proves that the render failed?"* This breaks the sycophancy loop.
2. **The Blank Screen Trap Heuristics:** To remove subjective visual confirmation, the Executor is commanded to insert OpenGL occlusion queries (`glBeginQuery(GL_SAMPLES_PASSED)`) around draw calls. The Architect bases its routing decision on empirical hardware metrics alongside the semantic visual matching.

## Key Resilience Mechanisms
- **Headless Pipeline Integration:** The loop relies heavily on the program's ability to run a single frame and dump `.png` framebuffers natively without opening OS windows.
- **Refusal to Halt:** The root persona is explicitly forbidden from halting the process to ask the human for help regarding compilation errors or logic bugs, forcing it to self-heal.
