# spawn_agents.ps1
# Executor Implementation: Phase 1 (Local Implementation)

# 1. State Check & Save
$status = git status --porcelain
if ($status) {
    Write-Host "[Executor] Uncommitted changes detected. Snapshotting state..." -ForegroundColor Yellow
    git add .
    git commit -m "Orchestrator Snapshot"
} else {
    Write-Host "[Executor] Clean state. Proceeding." -ForegroundColor Green
}

# 2. Branch & Worktree Allocation
$repoRoot = Get-Location
$parentDir = Split-Path -Path $repoRoot.Path -Parent
$alphaPath = Join-Path $parentDir "worktree_alpha"
$betaPath = Join-Path $parentDir "worktree_beta"

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

Setup-Worktree -Path $alphaPath -Branch "feature-alpha"
Setup-Worktree -Path $betaPath -Branch "feature-beta"

# 3. Prompt Acquisition
Write-Host "`n[Executor] Prompt Acquisition Phase" -ForegroundColor Magenta
$Prompt1 = Read-Host "Enter prompt for Agent 1 (ALPHA)"
$Prompt2 = Read-Host "Enter prompt for Agent 2 (BETA)"

if ([string]::IsNullOrWhiteSpace($Prompt1)) { $Prompt1 = "Analyze the current worktree and wait for instructions." }
if ([string]::IsNullOrWhiteSpace($Prompt2)) { $Prompt2 = "Analyze the current worktree and wait for instructions." }

# 4. IPC Hardening
Set-Content -Path (Join-Path $alphaPath "init_prompt.txt") -Value $Prompt1
Set-Content -Path (Join-Path $betaPath "init_prompt.txt") -Value $Prompt2

# 5. Execution & Spawning
function Launch-Agent {
    param(
        [string]$Path,
        [string]$Label
    )

    $initFile = Join-Path $Path "init_prompt.txt"
    # Note: We use -NoExit so the user can see the output and manually paste if needed.
    # The command sequence: CD to worktree, load prompt to clipboard, provide instructions, launch gemini.
    $command = "Set-Location -LiteralPath '$Path'; Get-Content -Raw -LiteralPath '$initFile' | Set-Clipboard; Clear-Host; Write-Host '=== $Label AGENT TERMINAL ===' -ForegroundColor Green; Write-Host 'Prompt has been copied to clipboard.' -ForegroundColor Cyan; Write-Host 'ACTION: Press CTRL+V followed by ENTER to begin.' -ForegroundColor Yellow; gemini -y"

    Start-Process powershell -ArgumentList "-NoExit", "-Command", $command
}

Write-Host "`n[Executor] Spawning Agent Alpha..." -ForegroundColor Green
Launch-Agent -Path $alphaPath -Label "ALPHA"

Write-Host "[Executor] Spawning Agent Beta..." -ForegroundColor Green
Launch-Agent -Path $betaPath -Label "BETA"

Write-Host "`n[SUCCESS] Orchestration complete. Agents are active in separate terminals." -ForegroundColor Green
