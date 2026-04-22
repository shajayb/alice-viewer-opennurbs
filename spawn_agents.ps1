# spawn_agents.ps1
# Executor Implementation: Phase 1 (Local Implementation)
# Upgraded for N-Agent Swarm Execution

param (
    [string[]]$AgentNames = @("gamma", "delta", "epsilon", "zeta", "eta")
)

# 1. State Check & Save
$status = git status --porcelain
if ($status) {
    Write-Host "[Executor] Uncommitted changes detected. Snapshotting state..." -ForegroundColor Yellow
    git add .
    git commit -m "Orchestrator Snapshot before spawning swarm"
} else {
    Write-Host "[Executor] Clean state. Proceeding." -ForegroundColor Green
}

# 2. Branch & Worktree Allocation
$repoRoot = Get-Location
$parentDir = Split-Path -Path $repoRoot.Path -Parent

function Setup-Worktree {
    param(
        [string]$Path,
        [string]$Branch
    )

    Write-Host "[Executor] Preparing worktree at $Path for branch $Branch..." -ForegroundColor Cyan
    
    # Cleanup existing worktree or directory
    if (Test-Path $Path) {
        $existingWorktree = git worktree list | Select-String [regex]::Escape($Path)
        if ($existingWorktree) {
            Write-Host "  Removing existing worktree registration..." -ForegroundColor Gray
            git worktree remove -f $Path
        }
        if (Test-Path $Path) {
            Write-Host "  Deleting existing directory..." -ForegroundColor Gray
            Remove-Item -Recurse -Force $Path -ErrorAction SilentlyContinue
        }
    }

    # Cleanup existing branch to ensure -b works
    $branchExists = git branch --list $Branch
    if ($branchExists) {
        Write-Host "  Deleting existing branch $Branch..." -ForegroundColor Gray
        git branch -D $Branch
    }

    # Add worktree
    git worktree add -b $Branch $Path
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to add worktree at $Path"
        exit $LASTEXITCODE
    }
}

$agentPaths = @{}

foreach ($agent in $AgentNames) {
    $branchName = "feature-$agent"
    $worktreePath = Join-Path $parentDir "worktree_$agent"
    Setup-Worktree -Path $worktreePath -Branch $branchName
    $agentPaths[$agent] = $worktreePath
}

# 3. Prompt Acquisition
Write-Host "`n[Executor] Prompt Acquisition Phase" -ForegroundColor Magenta

$agentPrompts = @{}
foreach ($agent in $AgentNames) {
    $upperAgent = $agent.ToUpper()
    $prompt = Read-Host "Enter prompt for Agent $upperAgent (or press Enter for default)"
    if ([string]::IsNullOrWhiteSpace($prompt)) { 
        $prompt = "Analyze the current worktree and wait for instructions to implement CesiumGEPR.h." 
    }
    $agentPrompts[$agent] = $prompt
}

# 4. IPC Hardening
foreach ($agent in $AgentNames) {
    $path = $agentPaths[$agent]
    $prompt = $agentPrompts[$agent]
    Set-Content -Path (Join-Path $path "init_prompt.txt") -Value $prompt
}

# 5. Execution & Spawning
function Launch-Agent {
    param(
        [string]$Path,
        [string]$Label
    )

    $initFile = Join-Path $Path "init_prompt.txt"
    $command = "Set-Location -LiteralPath '$Path'; Get-Content -Raw -LiteralPath '$initFile' | Set-Clipboard; Clear-Host; Write-Host '=== $Label AGENT TERMINAL ===' -ForegroundColor Green; Write-Host 'Prompt has been copied to clipboard.' -ForegroundColor Cyan; Write-Host 'ACTION: Press CTRL+V followed by ENTER to begin.' -ForegroundColor Yellow; gemini -y"

    Start-Process powershell -ArgumentList "-NoExit", "-Command", $command
}

Write-Host "`n[Executor] Spawning Agent Swarm..." -ForegroundColor Green
foreach ($agent in $AgentNames) {
    $upperAgent = $agent.ToUpper()
    Write-Host "Spawning Agent $upperAgent..." -ForegroundColor Green
    Launch-Agent -Path $agentPaths[$agent] -Label $upperAgent
}

$agentCount = $AgentNames.Count
Write-Host "`n[SUCCESS] Orchestration complete. $agentCount agents are active in separate terminals." -ForegroundColor Green
