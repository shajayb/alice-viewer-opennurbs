Architecture Summary: Multi-Agent CLI Orchestrator
We have successfully engineered an autonomous, multi-agent C++ development pipeline managed entirely via Windows PowerShell and the Gemini CLI. The system elegantly balances a live, self-healing execution loop with a strict, headless code-review layer while circumventing native OS and CLI limitations.

1. System Components
orchestrator.ps1 (The Brain): The master PowerShell script. It is responsible for parsing goals, maintaining the Git state, concatenating prompts, managing background OS jobs to break CLI locks, and routing the logic based on JSON schemas.

high_level_prompt.txt (The Blueprint): A human-readable text file containing the [GOAL], [PLAN], and [BOOTSTRAP] sections. This seeds the initial state without requiring an LLM inference call.

The Executor Agent (The Muscle): Powered by executor_SOP.md. This agent runs in a live, stateful REPL mode (gemini --yolo). It writes code, runs CMake/Ninja, reads compiler errors, and loops autonomously until a build succeeds.

The Architect Agent (The Reviewer): Powered by architect_sop.md. This agent runs in a stateless, headless mode. It reads Git diffs and execution reports, evaluates code against strict performance rules, and outputs a rigid JSON routing decision (PROCEED, REWORK, HALT).

2. The Execution Loop
Bootstrap: Orchestrator extracts the first directive from high_level_prompt.txt.

Execution Phase: Orchestrator cleans whitespace, launches a background watcher job, and starts the Executor in REPL mode. The Executor's stdout is bound to the console for live monitoring.

Handoff: Upon successful compilation, the Executor creates handoff.trigger. The PowerShell watcher job detects this, forcefully kills the CLI process, and unblocks the script.

Review Phase: Orchestrator stages Git diffs, reads executor_report.json, and packages the state into a temporary text file (temp_architect_prompt.txt).

Evaluation: Orchestrator invokes the Architect headlessly by piping the temp file via cmd.exe /c "gemini < ..." (bypassing the Windows 8KB command-line length limit).

Routing: The Architect responds with strict JSON. The Orchestrator parses the "action" field, commits to Git if approved, generates the next directive, and loops.


{
  "session_state": "ARCHITECTURE_STABILIZED",
  "project": "Autonomous C++ Development Orchestrator",
  "environment": {
    "os": "Windows",
    "shell": "PowerShell",
    "cli": "Gemini CLI",
    "build_system": "CMake + Ninja + LLVM"
  },
  "agents": {
    "executor": {
      "mode": "REPL / Interactive",
      "flags": "--yolo",
      "responsibilities": ["Code generation", "Local compilation", "Self-healing loops"],
      "output_artifacts": ["executor_report.json", "handoff.trigger"]
    },
    "architect": {
      "mode": "Headless / Single-shot",
      "invocation": "stdin pipe via cmd.exe",
      "responsibilities": ["Code review", "Scope pacing", "Halting evaluation"],
      "output_artifacts": ["Strict JSON schema (action, assessment, next_directive)"]
    }
  },
  "engineering_hurdles_resolved": [
    {
      "issue": "REPL Lock",
      "symptom": "PowerShell script hung indefinitely waiting for Executor to finish.",
      "solution": "Implemented an asynchronous Start-Job watcher in PowerShell to listen for a 'handoff.trigger' file and kill the Node process."
    },
    {
      "issue": "Whitespace / Parsing Errors",
      "symptom": "CLI failed to parse concatenated prompts.",
      "solution": "Added regex sanitization to strip leading/trailing spaces and normalize line endings before invocation."
    },
    {
      "issue": "Windows Path/Command Limit (8191 Characters)",
      "symptom": "CLI crashed with 'filename or extension is too long' when Git diffs grew too large.",
      "solution": "Shifted Architect invocation from positional string arguments (-p) to writing a temporary text file and redirecting via stdin (cmd.exe /c '<')."
    }
  ],
  "file_manifest": [
    "orchestrator.ps1",
    "high_level_prompt.txt",
    "architect_sop.md",
    "executor_SOP.md"
  ]
}