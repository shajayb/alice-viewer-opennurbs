<#
.SYNOPSIS
Autonomous Multi-Agent Orchestrator Loop.
Manages a live REPL Executor and a Headless Architect.
#>

[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("REPL", "HEADLESS")]
    [string]$Mode = "REPL"
)

$ExecutorMode = $Mode

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "INITIALIZING ORCHESTRATOR IN [$ExecutorMode] MODE" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

$GoalFile = "Z_ORC_high_level_prompt.txt"
$ArchitectSystemFile = "Z_ORC_architect_sop.md"
$ExecutorSystemFile = "Z_ORC_executor_SOP.md"

# 1. Validate File Dependencies
$MissingFiles = @()
if (-Not (Test-Path $GoalFile)) { $MissingFiles += $GoalFile }
if (-Not (Test-Path $ArchitectSystemFile)) { $MissingFiles += $ArchitectSystemFile }
if (-Not (Test-Path $ExecutorSystemFile)) { $MissingFiles += $ExecutorSystemFile }

if ($MissingFiles.Count -gt 0)
{
    Write-Host "FATAL: Required system files are missing." -ForegroundColor Red
    foreach ($file in $MissingFiles) { Write-Host " -> Missing: $file" -ForegroundColor Red }
    Write-Host "Halting execution." -ForegroundColor DarkGray
    exit
}

Write-Host "Parsing Master Blueprint..." -ForegroundColor Cyan
$RawPrompt = Get-Content $GoalFile -Raw

# Extract sections using Regex
$GoalMatch      = [regex]::Match($RawPrompt, '(?is)\[GOAL\](.*?)(?=\[PLAN\]|\[BOOTSTRAP\]|$)')
$PlanMatch      = [regex]::Match($RawPrompt, '(?is)\[PLAN\](.*?)(?=\[GOAL\]|\[BOOTSTRAP\]|$)')
$BootstrapMatch = [regex]::Match($RawPrompt, '(?is)\[BOOTSTRAP\](.*?)(?=\[GOAL\]|\[PLAN\]|$)')

$LongTermGoal     = $GoalMatch.Groups[1].Value.Trim()
$MasterPlan       = $PlanMatch.Groups[1].Value.Trim()
$CurrentDirective = $BootstrapMatch.Groups[1].Value.Trim()

if ([string]::IsNullOrWhiteSpace($CurrentDirective))
{
    Write-Host "FATAL: Failed to parse [BOOTSTRAP] directive. Check formatting of $GoalFile." -ForegroundColor Red
    exit
}

Write-Host "LONG TERM GOAL: $LongTermGoal" -ForegroundColor Green
Write-Host "=== BOOTSTRAP PROMPT GENERATED ===" -ForegroundColor Magenta
Write-Host $CurrentDirective
Write-Host "==================================`n"

# 2. Setup Version Control and Snapshots
if (-Not (Test-Path ".git"))
{
    git init | Out-Null
}

if (-Not (Test-Path "state_snapshots"))
{
    New-Item -ItemType Directory -Name "state_snapshots" | Out-Null
}

# 3. Execution Loop
while ($true)
{
    Write-Host "======================================================="
    Write-Host "[EXECUTOR] Preparing Directive..." -ForegroundColor DarkGray
    
    $ExecutorSystemString = Get-Content $ExecutorSystemFile -Raw
    $RawExecutorPrompt = "$ExecutorSystemString`n`n--- YOUR CURRENT DIRECTIVE ---`n$CurrentDirective"

    # --- WHITESPACE SANITIZATION ---
    $CleanExecutorPrompt = $RawExecutorPrompt -replace "`r`n", "`n"
    $CleanExecutorPrompt = $CleanExecutorPrompt -replace '(?m)^[ \t]+', ''
    $CleanExecutorPrompt = $CleanExecutorPrompt -replace '(?m)[ \t]+$', ''
    $CleanExecutorPrompt = [regex]::Replace($CleanExecutorPrompt, '\n{3,}', "`n`n")
    $CleanExecutorPrompt = $CleanExecutorPrompt.Trim()

    if (Test-Path "handoff.trigger")
    {
        Remove-Item "handoff.trigger" -Force
    }

    if ($ExecutorMode -eq "REPL")
    {
        # --- REPL MODE LAUNCH ---
        $WatcherJob = Start-Job -ScriptBlock {
            while ($true)
            {
                Start-Sleep -Seconds 1
                if (Test-Path "handoff.trigger")
                {
                    Stop-Process -Name "gemini" -Force -ErrorAction SilentlyContinue
                    # Stop-Process -Name "node" -Force -ErrorAction SilentlyContinue
                    break
                }
            }
        }

        Write-Host "[EXECUTOR] Launching REPL (Self-Healing Mode)..." -ForegroundColor Yellow
        
        gemini --yolo "$CleanExecutorPrompt"
        
        Stop-Job $WatcherJob | Out-Null
        Remove-Job $WatcherJob | Out-Null
    }
    else 
    {
        # --- HEADLESS MODE LAUNCH ---
        Write-Host "[EXECUTOR] Launching HEADLESS Mode..." -ForegroundColor Yellow
        
        $TempExecutorFile = "temp_executor_prompt.txt"
        Set-Content -Path $TempExecutorFile -Value $CleanExecutorPrompt -Encoding UTF8

        cmd.exe /c "gemini --yolo < `"$TempExecutorFile`""
        
        if (Test-Path $TempExecutorFile) { Remove-Item $TempExecutorFile -Force }
    }

    # 4. Verification & Context Gathering
    if (-Not (Test-Path "executor_report.json"))
    {
        Write-Host "`n>>> FAIL-SAFE TRIGGERED: EXECUTOR ARTIFACT MISSING <<<" -ForegroundColor Red
        Write-Host "Context: The Executor CLI process terminated, but 'executor_report.json' was not found." -ForegroundColor DarkRed
        Write-Host "Possible Causes: CLI crash, syntax hallucination, or failure to trigger final handoff correctly." -ForegroundColor DarkRed
        Write-Host "Last Directive Attempted:`n$CurrentDirective" -ForegroundColor DarkGray
        break
    }

    $ReportContext = Get-Content "executor_report.json" -Raw
    
    git add .
    $DiffContext = git diff --cached
    
    $ArchitectContext = @"
LONG TERM GOAL: 
$LongTermGoal

MASTER PLAN:
$MasterPlan

CURRENT DIRECTIVE JUST ATTEMPTED: 
$CurrentDirective

=== EXECUTOR REPORT ===
$ReportContext

=== GIT DIFF ===
$DiffContext
"@

    $ArchitectSystemString = Get-Content $ArchitectSystemFile -Raw
    $RawArchitectPrompt = "$ArchitectSystemString`n`n--- CURRENT SYSTEM STATE ---`n$ArchitectContext"
    
    $CleanArchitectPrompt = $RawArchitectPrompt -replace "`r`n", "`n"
    $CleanArchitectPrompt = $CleanArchitectPrompt -replace '(?m)^[ \t]+', ''
    $CleanArchitectPrompt = $CleanArchitectPrompt -replace '(?m)[ \t]+$', ''
    $CleanArchitectPrompt = [regex]::Replace($CleanArchitectPrompt, '\n{3,}', "`n`n")
    $CleanArchitectPrompt = $CleanArchitectPrompt.Trim()

    Write-Host "`n[ARCHITECT] Evaluating Executor outputs..." -ForegroundColor Cyan
    
    $TempArchitectFile = "temp_architect_prompt.txt"
    Set-Content -Path $TempArchitectFile -Value $CleanArchitectPrompt -Encoding UTF8

    $ArchitectResponseRaw = cmd.exe /c "gemini < `"$TempArchitectFile`"" 2>&1
    
    if (Test-Path $TempArchitectFile) { Remove-Item $TempArchitectFile -Force }

    # --- GUARDRAILS ---
    if ([string]::IsNullOrWhiteSpace($ArchitectResponseRaw) -or $ArchitectResponseRaw -match "ApplicationFailedException" -or $ArchitectResponseRaw -match "filename or extension is too long")
    {
        Write-Host "`n>>> FAIL-SAFE TRIGGERED: ARCHITECT INVOCATION FAILED <<<" -ForegroundColor Red
        Write-Host "Context: The system failed to execute the Architect process, or the process returned empty data." -ForegroundColor DarkRed
        Write-Host "Raw CLI Error Output:" -ForegroundColor DarkGray
        Write-Host $ArchitectResponseRaw -ForegroundColor DarkGray
        break
    }

    try
    {
        $Decision = $ArchitectResponseRaw | ConvertFrom-Json
        
        if ($null -eq $Decision.action)
        {
            throw "Model returned JSON, but it was missing the mandatory 'action' key."
        }
    }
    catch
    {
        Write-Host "`n>>> FAIL-SAFE TRIGGERED: ARCHITECT PARSE ERROR <<<" -ForegroundColor Red
        Write-Host "Context: The Architect agent generated a response, but it could not be mapped to the strict JSON schema." -ForegroundColor DarkRed
        Write-Host "Exception Detail: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "--- RAW MODEL OUTPUT THAT FAILED TO PARSE ---" -ForegroundColor DarkGray
        Write-Host $ArchitectResponseRaw -ForegroundColor DarkGray
        Write-Host "---------------------------------------------" -ForegroundColor DarkGray
        break
    }

    Write-Host "Architect Critique: $($Decision.assessment.critique)" -ForegroundColor Gray

    # 6. State Routing
    if ($Decision.action -eq "HALT")
    {
        Write-Host "`n[HALT TRIGGERED] Architect determined criteria met. Shutting down gracefully." -ForegroundColor Green
        git commit -m "Goal Achieved: $LongTermGoal" | Out-Null
        break
    }
    elseif ($Decision.action -eq "REWORK")
    {
        Write-Host "`n[REWORK] Architect rejected the execution. Generating fix directive." -ForegroundColor Red
        $CurrentDirective = "FIX PREVIOUS MISTAKES: $($Decision.next_directive)"
        git reset HEAD . | Out-Null
    }
    elseif ($Decision.action -eq "PROCEED")
    {
        Write-Host "`n[PROCEED] Architect approved. Advancing state." -ForegroundColor Green
        $CurrentDirective = $Decision.next_directive
        git commit -m "Autocommit: Approved Step" | Out-Null
    }
}