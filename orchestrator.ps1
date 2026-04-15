<#
.SYNOPSIS
Autonomous Multi-Agent Orchestrator Loop.
Manages a live REPL Executor and a Headless Architect.
Uses stdin redirection to bypass Windows 8191 character limits.
#>

$GoalFile = "high_level_prompt.txt"
$ArchitectSystemFile = "architect_sop.md"
$ExecutorSystemFile = "executor_SOP.md"

# 1. Validate File Dependencies
if (-Not (Test-Path $GoalFile))
{
    Write-Host "FATAL: $GoalFile not found." -ForegroundColor Red
    exit
}
if (-Not (Test-Path $ArchitectSystemFile) -Or -Not (Test-Path $ExecutorSystemFile))
{
    Write-Host "FATAL: System prompt markdown files missing." -ForegroundColor Red
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
    
    # We still use positional arguments for Executor because it needs live REPL attachment
    gemini --yolo "$CleanExecutorPrompt"
    
    Stop-Job $WatcherJob | Out-Null
    Remove-Job $WatcherJob | Out-Null

    # 4. Verification & Context Gathering
    if (-Not (Test-Path "executor_report.json"))
    {
        Write-Host "FATAL: Executor terminated but failed to produce executor_report.json. Halting." -ForegroundColor Red
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
    
    # --- NEW FILE-BASED PIPELINE TO BYPASS 8KB LIMIT ---
    $TempArchitectFile = "temp_architect_prompt.txt"
    Set-Content -Path $TempArchitectFile -Value $CleanArchitectPrompt -Encoding UTF8

    # We use cmd.exe to native-pipe the file into gemini. 
    # Because it is piped via stdin and lacks a TTY keyboard input, it will execute headless and exit automatically.
    $ArchitectResponseRaw = cmd.exe /c "gemini < `"$TempArchitectFile`"" 2>&1
    
    # Cleanup the temp file
    if (Test-Path $TempArchitectFile) { Remove-Item $TempArchitectFile -Force }

    Write-Host "`n--- RAW ARCHITECT RESPONSE ---" -ForegroundColor DarkGray
    Write-Host $ArchitectResponseRaw
    Write-Host "------------------------------`n" -ForegroundColor DarkGray

    # --- GUARDRAILS ---
    if ([string]::IsNullOrWhiteSpace($ArchitectResponseRaw) -or $ArchitectResponseRaw -match "ApplicationFailedException" -or $ArchitectResponseRaw -match "filename or extension is too long")
    {
        Write-Host "FATAL: Architect CLI invocation failed or returned empty." -ForegroundColor Red
        Write-Host "Halting loop to prevent runaway execution." -ForegroundColor Red
        break
    }

    try
    {
        $Decision = $ArchitectResponseRaw | ConvertFrom-Json
        
        if ($null -eq $Decision.action)
        {
            throw "JSON missing 'action' property. Model hallucinated text."
        }
    }
    catch
    {
        Write-Host "FATAL: Architect returned invalid JSON. Halting." -ForegroundColor Red
        Write-Host "Error details: $($_.Exception.Message)" -ForegroundColor Red
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