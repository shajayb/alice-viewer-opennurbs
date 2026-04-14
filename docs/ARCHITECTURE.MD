Architecture Overview: The Autonomous Visual QA Pipeline
Here is the definitive architectural map of the system we just built. You can save this as ARCHITECTURE.md in your repository so that both you (the Human Lead) and future instances of the CLI Executor understand exactly how this machine operates.

1. The Core Entities
The Human Lead (You): Determines the architectural/algorithmic intent. Writes the high-level goal into prompt.txt and triggers the loop.

The Hub (AliceOrchestrator.exe): A strictly local, native C++ state machine. It manages process execution, file I/O, headless telemetry capture, and raw HTTP API routing. It is the bridge between your local file system and the cloud AI.

The Builder (Local Executor CLI): Your local Gemini CLI agent. It reads the intent, writes the DOD-compliant C++ code, and autonomously fights the LLVM/Ninja compiler in a closed loop until it achieves zero errors. It communicates back to the Hub via executor_report.json.

The Telemetry (AliceViewer.exe): Your core CAD binary, running in a fractional-second --headless-capture mode. It renders the mathematical output of the Builder and dumps a raw framebuffer.png to disk without drawing a UI window.

The Reviewer (Gemini 1.5 Pro API): The strict Cloud Vision AI. It receives the framebuffer.png, the executor_report.json, and a hardcoded System Prompt. It judges the visual accuracy (Phase 2) and memory compliance (Phase 3).

2. The Data Flow (The State Machine)
[INIT] The Human Lead executes ./build/bin/AliceOrchestrator.exe prompt.txt.

[STATE 1: EXECUTE] The Orchestrator hands the terminal (via std::system) to the Executor CLI. The CLI writes the code, loops with Ninja until success, and writes its claims to executor_report.json.

[STATE 2: CAPTURE] The Orchestrator parses the JSON. If the build succeeded, it triggers AliceViewer.exe --headless-capture. The viewer dumps framebuffer.png and instantly closes.

[STATE 3: REVIEW] The Orchestrator converts the image to Base64. It bundles the image, the JSON claims, and the Reviewer's strict system prompt into a single libcurl POST request to the cloud.

[RESOLUTION] * If the math/visuals fail: The Reviewer returns a new string of instructions correcting the math. The Orchestrator feeds this string right back into STATE 1.

If the math/visuals pass: The Reviewer returns <STATUS: PASSED_PHASE_3>. The Orchestrator powers down and hands control back to the Human.