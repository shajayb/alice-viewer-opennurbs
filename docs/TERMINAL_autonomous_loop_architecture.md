# Autonomous Self-Healing Agent Architecture

## Overview
This document summarizes the autonomous, self-healing "Ping-Pong" loop architecture designed to enable relentless, unassisted C++ development and visual rendering refinement. The architecture operates on a strict division of labor between two distinct agent personas, orchestrated via a continuous feedback loop until mathematically and visually verifiable success is achieved.

## The Agents

### 1. The Architect (Root Agent)
**Configuration File:** `GEMINI.md`
**Role:** Lead C++ Graphics Architect.
**Responsibilities:**
- The Architect's prompt is parameterized via a `# PARAMETERS` block (e.g., `<APP_NAME>`, `<TARGET_ASSET>`, `<CONFIG_FILE>`, `<OUTPUT_FILE>`, `<EXPECTED_VIEW_COUNT>`).
- The Architect dynamically adapts its 8-step `MASTER PLAN` and strict `SUCCESS CRITERIA` based on these inputs.
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
- **Alignment:** The Architect then compares its visual description against the established ground truth `"semantic_criteria"` located in the `<CONFIG_FILE>`. The two descriptions must match semantically for the test to pass.
- **Data Injection:** Upon successful match, the Architect must inject its semantic analysis text directly into the `image_description` field for that specific view inside the final `<OUTPUT_FILE>`.

## The "Ping-Pong" Protocol (The Loop)
The core of the autonomy lies in the 8-step parameterized execution loop defined in the `MASTER PLAN`:
1. **Revise Code:** The Architect directs the Executor to ensure multi-view camera loading for `<TARGET_ASSET>`.
2. **Build Application:** Executor compiles the `<APP_NAME>`.
3. **Headless Execution:** Executor runs the binary headlessly to stream assets and output framebuffers for **every view** defined in `<CONFIG_FILE>` (totaling `<EXPECTED_VIEW_COUNT>`).
4. **Semantic Analysis:** For *each* generated image, the Architect conducts native multimodal visual analysis.
5. **Similarity Check:** The Architect compares the analysis against the `<CONFIG_FILE>` ground truth.
6. **Data Injection:** The Architect edits the `<OUTPUT_FILE>` to insert the successful semantic description.
7. **Adversarial Looping:** If *any* image fails the similarity check, the Architect formulates a `REWORK` directive and restarts from Step 1.
8. **Termination:** The loop only halts when **ALL IMAGES** achieve Semantic Success.

## Combating LLM Sycophancy & Hallucination
Evaluating visual outputs introduces vulnerabilities, particularly Affirmation Bias (multimodal LLMs trusting a text success report over a visually blank image). To combat this, the architecture relies on:
1. **Adversarial Chain-of-Thought (`suspicion_check`):** The prompt forces the Architect to formulate a counter-narrative *before* making a routing decision: *"Assume the Executor is lying to you and the code failed entirely. What specific visual evidence in the image proves that the render failed?"* This breaks the sycophancy loop.
2. **The Blank Screen Trap Heuristics:** To remove subjective visual confirmation, the Executor is commanded to insert OpenGL occlusion queries (`glBeginQuery(GL_SAMPLES_PASSED)`) around draw calls. The Architect bases its routing decision on empirical hardware metrics alongside the semantic visual matching.

## Key Resilience Mechanisms
- **Headless Pipeline Integration:** The loop relies heavily on the program's ability to run a single frame and dump `.png` framebuffers natively without opening OS windows.
- **Refusal to Halt:** The root persona is explicitly forbidden from halting the process to ask the human for help regarding compilation errors or logic bugs, forcing it to self-heal.
