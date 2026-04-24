# Autonomous Self-Healing Agent Architecture

## Overview
This document summarizes the autonomous, self-healing "Ping-Pong" loop architecture designed to enable relentless, unassisted C++ development and visual rendering refinement. The architecture operates on a strict division of labor between two distinct agent personas, orchestrated via a continuous feedback loop until mathematically and visually verifiable success is achieved.

## The Master Orchestrator (`orchestrator.ps1`)
The entire system is governed by a master Windows PowerShell script (`orchestrator.ps1`). It bypasses traditional linear script execution by instantiating an asynchronous feedback loop.
The pipeline is seeded by `high_level_prompt.txt`, which acts as the genetic blueprint, containing:
1. **[GOAL]:** The terminal end-state.
2. **[PLAN]:** A strictly ordered, sequential array of atomic milestones.
3. **Context payload:** Initial architecture rules to save token bandwidth and prevent early-stage hallucination.

## The Agents

### 1. The Architect (Root Agent)
**Configuration File:** `gemini.md`
**Role:** Lead C++ Graphics Architect.
**State:** Stateless, headless, single-shot mode (`gemini < temp.txt`).
**Responsibilities:**
- Manages the `LONG TERM GOAL` and the `MASTER PLAN`.
- Formulates highly specific `DIRECTIVE`s for the Executor sub-agent.
- **The Adversarial Mandate:** Never inherently trusts the Executor. The Architect rigorously interrogates the Executor's output (build logs, console output, and crucially, semantic visual analysis of rendered framebuffers). 
- If the Executor fails, crashes, or hallucinates success, the Architect *must not* yield back to the user. Instead, it immediately invokes the Executor again with a highly critical `REWORK` directive.

### 2. The Executor (Sub-Agent)
**Configuration File:** `.agents/executor.md`
**Role:** Aggressively fast C++ implementer.
**State:** Stateful Read-Eval-Print Loop (REPL) using `--yolo` flag.
**Responsibilities:**
- Strictly obeys the Architect's `DIRECTIVE`.
- Implements C++ code strictly adhering to a Data-Oriented Design (DOD) paradigm (e.g., avoiding heap allocations).
- Handles the compilation pipeline (CMake/Ninja) and programmatic execution of tests.
- Always concludes its run by emitting an `[AGENT_HANDOFF]` JSON payload and generating a `handoff.trigger`.

## The "Ping-Pong" Protocol (The Six-Stage Loop)
The orchestration follows a cyclical pipeline to overcome CLI limitations:
1. **Bootstrap Extraction:** Orchestrator parses `high_level_prompt.txt` for the [GOAL] and current [PLAN] step.
2. **Execution Phase:** Orchestrator launches the Executor REPL. The Executor modifies code, runs `ninja -C build`, captures the framebuffer, and writes `executor_report.json`.
3. **Asynchronous Handoff:** A background `Start-Job` OS watcher detects the creation of `handoff.trigger` and violently terminates the Executor's REPL CLI process, unblocking the main pipeline.
4. **Review Phase Staging:** The orchestrator stages all generated Git diffs and concatenates them with the executor report into a temporary `temp.txt` file. This bypasses the Windows `cmd.exe` 8,191-character path limit.
5. **Evaluation:** The Architect is invoked, reading `temp.txt` to evaluate the code quality and visual output.
6. **Routing:** The Architect responds with a strict JSON routing command: `PROCEED`, `REWORK`, or `HALT`. The loop *only* terminates and returns to the human user when 100% of the Success Criteria are verifiably met.

## Combating LLM Sycophancy & Hallucination
Evaluating visual outputs introduces vulnerabilities, particularly Affirmation Bias (multimodal LLMs trusting a text success report over a visually blank image). 

To combat this, the architecture relies on two key mechanisms:
1. **Adversarial Chain-of-Thought (`suspicion_check`):** The prompt forces the Architect to formulate a counter-narrative *before* making a routing decision: *"Assume the Executor is lying to you and the code failed entirely. What specific visual evidence in the image proves that the render failed?"* This breaks the sycophancy loop.
2. **The Blank Screen Trap Heuristics:** To remove subjective visual confirmation, the Executor is commanded to insert OpenGL occlusion queries (`glBeginQuery(GL_SAMPLES_PASSED)`) around draw calls. The Architect bases its routing decision exclusively on empirical hardware metrics (e.g., `pixel_coverage_percentage > 2%` and `frustum_vertex_count > 0`), anchoring the loop to undeniable hardware truths.

## The Unit Test (`UNIT_TEST.prompt`)
To benchmark and harden this architecture, a "Green Screen Protocol" (later evolved to the "Red Cube/Sphere" test) was established.
- **Goal:** Produce a specific geometric layout (e.g., 5x5x5m Red Cube, 2.5m hovering Red Sphere, 25x25m White Ground Plane) using a defined camera perspective and lighting.
- **Compile Criteria:** 0 warnings, 0 errors, strict DOD adherence.
- **Semantic Criteria:** The Architect evaluates the hardware pixel coverage and structure to ensure the geometry aligns with the prompt requirements, preventing the Executor from bypassing the test.

## Key Resilience Mechanisms
- **Headless Pipeline Integration:** The loop relies heavily on the program's ability to run a single frame and dump `.png` framebuffers natively without opening OS windows.
- **Refusal to Halt:** The root persona is explicitly forbidden from halting the process to ask the human for help regarding compilation errors or logic bugs, forcing it to self-heal.
