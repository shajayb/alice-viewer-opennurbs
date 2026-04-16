<#
.SYNOPSIS
Autonomous Multi-Agent Orchestrator Loop.
Manages a live REPL Executor and a Headless Architect.
#>

[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("REPL", "HEADLESS")]
    [string]$Mode = "REPL",

    [Parameter()]
    [int]$StartStep = 1
)

$ExecutorMode = $Mode

# ====================================================================
# GLOBAL TELEMETRY INITIALIZATION
# ====================================================================
$WallClock = [System.Diagnostics.Stopwatch]::StartNew()
$Telemetry = @{
    ExecutorTimeSec = 0
    ArchitectTimeSec = 0
    ExecutorTokens = 0
    ArchitectTokens = 0
    LoopsCompleted = 0
}

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "INITIALIZING ORCHESTRATOR IN [$ExecutorMode] MODE" -ForegroundColor Cyan
if ($StartStep -gt 1) { Write-Host "FAST-FORWARDING TO STEP: $StartStep" -ForegroundColor Yellow }
Write-Host "=======================================================" -ForegroundColor Cyan

$GoalFile = "Z_ORC_high_level_prompt.txt"
$ArchitectSystemFile = "Z_ORC_architect_sop.md"
$ExecutorSystemFile = "Z_ORC_executor_SOP.md"

# 1. Validate File Dependencies & Security
$MissingFiles = @()
if (-Not (Test-Path $GoalFile)) { $MissingFiles += $GoalFile }
if (-Not (Test-Path $ArchitectSystemFile)) { $MissingFiles += $ArchitectSystemFile }
if (-Not (Test-Path $ExecutorSystemFile)) { $MissingFiles += $ExecutorSystemFile }

if ($MissingFiles.Count -gt 0) {
    Write-Host "FATAL: Required system files are missing." -ForegroundColor Red
    foreach ($file in $MissingFiles) { Write-Host " -> Missing: $file" -ForegroundColor Red }
    exit
}

# CRITICAL SECURITY: Ensure API_KEYS.TXT is gitignored
$GitignorePath = ".gitignore"
if (-Not (Test-Path $GitignorePath)) {
    Set-Content -Path $GitignorePath -Value "API_KEYS.TXT`n" -Encoding UTF8
    Write-Host "[SECURITY] Created .gitignore and secured API_KEYS.TXT" -ForegroundColor Green
} else {
    $GitignoreContent = Get-Content $GitignorePath -Raw
    if ($GitignoreContent -notmatch "(?m)^API_KEYS\.TXT$") {
        Add-Content -Path $GitignorePath -Value "`nAPI_KEYS.TXT" -Encoding UTF8
        Write-Host "[SECURITY] Added API_KEYS.TXT to existing .gitignore" -ForegroundColor Green
    }
}

# CRITICAL: Create a UTF-8 encoding object that explicitly disables the BOM
$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $False

# ====================================================================
# SECURE API KEY INJECTION (Process Scope)
# ====================================================================
function Initialize-SecureEnvironment {
    $KeyFilePath = ".\API_KEYS.TXT"
    if (Test-Path $KeyFilePath) {
        $KeyFound = $false
        switch -Regex -File $KeyFilePath {
            '^\s*GOOGLE_API_KEY=(.*)' { 
                $ExtractedKey = $Matches[1].Trim()
                [Environment]::SetEnvironmentVariable('GOOGLE_API_KEY', $ExtractedKey, [EnvironmentVariableTarget]::Process)
                $KeyFound = $true
            }
            '^\s*GEMINI_API_KEY=(.*)' { 
                $ExtractedKey = $Matches[1].Trim()
                [Environment]::SetEnvironmentVariable('GEMINI_API_KEY', $ExtractedKey, [EnvironmentVariableTarget]::Process)
                $KeyFound = $true
            }
        }
        if ($KeyFound) { Write-Host "[SECURITY] API Keys provisioned to ephemeral Process memory." -ForegroundColor Green }
    } else {
        Write-Warning "[SECURITY] API_KEYS.TXT not found. Environment provisioning bypassed."
    }
}
Initialize-SecureEnvironment

# ====================================================================
# CONTEXT CACHING & STATeless SESSION MANAGEMENT
# ====================================================================
Write-Host "[ORCHESTRATOR] Initializing Hierarchical Context..." -ForegroundColor Cyan

$ContextFilePath = Join-Path (Get-Location) "GEMINI.md"
$ArchitectSOP = Get-Content $ArchitectSystemFile -Raw
$ExecutorSOP = Get-Content $ExecutorSystemFile -Raw

$ContextContent = @"
# SYSTEM ARCHITECTURE & AGENT SOPS

## ARCHITECT AGENT SOP
$ArchitectSOP

## EXECUTOR AGENT SOP
$ExecutorSOP
"@
[System.IO.File]::WriteAllText($ContextFilePath, $ContextContent, $Utf8NoBomEncoding)
Write-Host "[ORCHESTRATOR] Deployed static context to GEMINI.md (Implicit Caching Enabled)." -ForegroundColor Green

$SettingsPath = Join-Path $env:USERPROFILE ".gemini\settings.json"
$SettingsObject = if (Test-Path $SettingsPath) {
    try { Get-Content $SettingsPath -Raw | ConvertFrom-Json }
    catch { [PSCustomObject]@{ model = [PSCustomObject]@{}; general = [PSCustomObject]@{} } }
} else {
    $null = New-Item -Path (Split-Path $SettingsPath) -ItemType Directory -Force -ErrorAction SilentlyContinue
    [PSCustomObject]@{ model = [PSCustomObject]@{}; general = [PSCustomObject]@{} }
}

if ($null -eq $SettingsObject.model) { $SettingsObject | Add-Member -MemberType NoteProperty -Name "model" -Value ([PSCustomObject]@{}) }
if ($null -eq $SettingsObject.general) { $SettingsObject | Add-Member -MemberType NoteProperty -Name "general" -Value ([PSCustomObject]@{}) }

$SettingsObject.model | Add-Member -MemberType NoteProperty -Name "maxSessionTurns" -Value 9999 -Force
$SettingsObject.model | Add-Member -MemberType NoteProperty -Name "compressionThreshold" -Value 0.6 -Force
$RetentionObj = [PSCustomObject]@{ enabled = $true; maxCount = 5; maxAge = "7d"; minRetention = "1d" }
$SettingsObject.general | Add-Member -MemberType NoteProperty -Name "sessionRetention" -Value $RetentionObj -Force

$JsonString = $SettingsObject | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($SettingsPath, $JsonString, $Utf8NoBomEncoding)

function Clear-GeminiSessions {
    Write-Host "[ORCHESTRATOR] Purging session cache to enforce statelessness..." -ForegroundColor DarkGray
    $PathsToNuke = @(
        "$env:USERPROFILE\.gemini\sessions",
        "$env:USERPROFILE\.gemini\tmp"
    )
    foreach ($p in $PathsToNuke) {
        if (Test-Path $p) { Remove-Item -Path "$p\*" -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

Write-Host "[ORCHESTRATOR] Parsing Master Blueprint..." -ForegroundColor Cyan
$RawPrompt = Get-Content $GoalFile -Raw

$GoalMatch      = [regex]::Match($RawPrompt, '(?is)\[GOAL\](.*?)(?=\[PLAN\]|\[BOOTSTRAP\]|$)')
$PlanMatch      = [regex]::Match($RawPrompt, '(?is)\[PLAN\](.*?)(?=\[GOAL\]|\[BOOTSTRAP\]|$)')
$BootstrapMatch = [regex]::Match($RawPrompt, '(?is)\[BOOTSTRAP\](.*?)(?=\[GOAL\]|\[PLAN\]|$)')

$LongTermGoal     = $GoalMatch.Groups[1].Value.Trim()
$MasterPlan       = $PlanMatch.Groups[1].Value.Trim()
$CurrentDirective = $BootstrapMatch.Groups[1].Value.Trim()

if ($StartStep -gt 1) {
    Write-Host "[ORCHESTRATOR] Slicing BOOTSTRAP to start at Step $StartStep..." -ForegroundColor Yellow
    $StepRegex = '(?is)(Step\s+' + $StartStep + ':.*)'
    $StepMatch = [regex]::Match($CurrentDirective, $StepRegex)
    if ($StepMatch.Success) {
        $CurrentDirective = $StepMatch.Groups[1].Value.Trim()
    } else {
        Write-Host "WARNING: Could not find 'Step ${StartStep}:' in [BOOTSTRAP]. Using full block." -ForegroundColor DarkYellow
    }
}

Write-Host "`nLONG TERM GOAL: $LongTermGoal" -ForegroundColor Green
Write-Host "=== BOOTSTRAP PROMPT GENERATED ===" -ForegroundColor Magenta
Write-Host $CurrentDirective
Write-Host "==================================`n"

if (-Not (Test-Path ".git")) { git init | Out-Null }
if (-Not (Test-Path "state_snapshots")) { New-Item -ItemType Directory -Name "state_snapshots" | Out-Null }

$ConsecutiveReworks = 0
$MaxRetries = 15 

# 3. Execution Loop
while ($true)
{
    $Telemetry.LoopsCompleted++
    Write-Host "`n======================================================="
    Write-Host "[ORCHESTRATOR] Loop $($Telemetry.LoopsCompleted) Initiated." -ForegroundColor Cyan
    Write-Host "[EXECUTOR] Preparing Directive..." -ForegroundColor DarkGray
    
    $RawExecutorPrompt = "--- YOUR CURRENT DIRECTIVE ---`n$CurrentDirective"
    $CleanExecutorPrompt = $RawExecutorPrompt -replace "`r`n", "`n"
    $CleanExecutorPrompt = $CleanExecutorPrompt -replace '(?m)^[ \t]+', ''
    $CleanExecutorPrompt = $CleanExecutorPrompt -replace '(?m)[ \t]+$', ''
    $CleanExecutorPrompt = [regex]::Replace($CleanExecutorPrompt, '\n{3,}', "`n`n").Trim()

    if (Test-Path "handoff.trigger") { Remove-Item "handoff.trigger" -Force }

    Clear-GeminiSessions # Wipe history so Executor starts fresh

    # ====================================================================
    # NATIVE FOREGROUND EXECUTION & BACKGROUND TIMEOUT WATCHER
    # ====================================================================
    Write-Host "[EXECUTOR] Launching [$ExecutorMode] Natively..." -ForegroundColor Yellow
    
    $WatcherJob = Start-Job -ScriptBlock {
        $MaxExecutionTimeSec = 1200 # 20 Minute hard timeout
        $Elapsed = 0
        while ($Elapsed -lt $MaxExecutionTimeSec) {
            Start-Sleep -Seconds 1
            $Elapsed++
            if (Test-Path "handoff.trigger") { return }
        }
        Stop-Process -Name "gemini" -Force -ErrorAction SilentlyContinue
        Set-Content -Path "handoff.trigger" -Value "TIMEOUT" -Encoding UTF8
    }

    $TempExecutorFile = "temp_executor_prompt.txt"
    [System.IO.File]::WriteAllText($TempExecutorFile, $CleanExecutorPrompt, $Utf8NoBomEncoding)

    $ExecTimer = [System.Diagnostics.Stopwatch]::StartNew()
    if ($ExecutorMode -eq "REPL") {
        gemini.cmd --approval-mode=yolo "$CleanExecutorPrompt"
    } else {
        cmd.exe /c "gemini.cmd --approval-mode=yolo < `"$TempExecutorFile`""
    }
    $ExecTimer.Stop()
    $Telemetry.ExecutorTimeSec += $ExecTimer.Elapsed.TotalSeconds
    
    Stop-Job $WatcherJob | Out-Null
    Remove-Job $WatcherJob | Out-Null
    if (Test-Path $TempExecutorFile) { Remove-Item $TempExecutorFile -Force }
    # ====================================================================

    if ((Get-Content "handoff.trigger" -ErrorAction SilentlyContinue) -match "TIMEOUT") {
        $SyntheticReport = @{
            agent_status = "FATAL_ERROR"
            build_status = "FAILED"
            highest_phase_completed = 1
            files_modified = @()
            claims = @("Agent timed out.")
            unresolved_compiler_errors = "RUNTIME HANG DETECTED. The application froze during execution. Check for infinite loops."
            optimization_metrics = "Failed."
        } | ConvertTo-Json
        [System.IO.File]::WriteAllText("executor_report.json", $SyntheticReport, $Utf8NoBomEncoding)
    }

    if (-Not (Test-Path "executor_report.json")) {
        Write-Host "`n>>> FAIL-SAFE TRIGGERED: EXECUTOR ARTIFACT MISSING <<<" -ForegroundColor Red
        break
    }

    $ReportContext = Get-Content "executor_report.json" -Raw
    
    git add .
    git reset HEAD GEMINI.md temp_*.txt executor_*.json executor_*.log *.png *.glb *.gltf *.b3dm *.pnts state_snapshots/ 2>$null
    
    # ====================================================================
    # SURGICAL PAYLOAD EXTRACTION
    # ====================================================================
    $SourceCodeContext = ""
    try {
        $ReportObj = $ReportContext | ConvertFrom-Json
        if ($null -ne $ReportObj.files_modified) {
            foreach ($file in $ReportObj.files_modified) {
                if ($file -match '\.(h|cpp|txt|cmake)$' -and (Test-Path $file)) {
                    $SourceCodeContext += "=== FILE: $file ===`n"
                    $SourceCodeContext += Get-Content $file -Raw
                    $SourceCodeContext += "`n`n"
                }
            }
        }
    } catch {
        Write-Warning "[ORCHESTRATOR] Failed to parse files_modified from executor_report.json."
    }

    if ([string]::IsNullOrWhiteSpace($SourceCodeContext)) {
        $SourceCodeContext = "No code files were reported as modified or could not be found."
    }

    $ConsoleLogContext = "No console log found."
    $LogLengthForTelemetry = 0
    if (Test-Path "executor_console.log") {
        $RawLog = Get-Content "executor_console.log" -Raw
        $LogLengthForTelemetry = $RawLog.Length
        if ($RawLog.Length -gt 5000) {
            $ConsoleLogContext = "... [TRUNCATED] ...`n" + $RawLog.Substring($RawLog.Length - 5000)
        } else {
            $ConsoleLogContext = $RawLog
        }
    }

    # Estimate Executor Tokens (Prompt Length + Raw Log Output Output)
    $ExecTokenEstimate = [math]::Round(($CleanExecutorPrompt.Length + $LogLengthForTelemetry) / 4)
    $Telemetry.ExecutorTokens += $ExecTokenEstimate

    # Verify Visual Attachment
    $ImageSize = 0
    $ImageContext = "No visual output generated."
    $ImageArg = ""
    if (Test-Path "framebuffer.png") {
        $ImageSize = (Get-Item "framebuffer.png").Length
        $ImageContext = "framebuffer.png successfully generated and attached to this prompt for visual verification."
        $ImageArg = " `"framebuffer.png`""
    }
    
    $ArchitectContext = @"
--- CURRENT SYSTEM STATE ---
LONG TERM GOAL: 
$LongTermGoal

MASTER PLAN:
$MasterPlan

CURRENT DIRECTIVE JUST ATTEMPTED: 
$CurrentDirective

=== EXECUTOR REPORT ===
$ReportContext

=== EXECUTOR CONSOLE LOG ===
$ConsoleLogContext

=== VISUAL OUTPUT ===
$ImageContext

=== MODIFIED SOURCE CODE ===
$SourceCodeContext
"@
    
    $CleanArchitectPrompt = $ArchitectContext -replace "`r`n", "`n"
    $CleanArchitectPrompt = $CleanArchitectPrompt -replace '(?m)^[ \t]+', ''
    $CleanArchitectPrompt = $CleanArchitectPrompt -replace '(?m)[ \t]+$', ''
    $CleanArchitectPrompt = [regex]::Replace($CleanArchitectPrompt, '\n{3,}', "`n`n").Trim()

    # ====================================================================
    # PAYLOAD TELEMETRY ENGINE
    # ====================================================================
    $LenReport = $ReportContext.Length
    $LenLog = $ConsoleLogContext.Length
    $LenCode = $SourceCodeContext.Length
    $LenTotal = $LenReport + $LenLog + $LenCode + $ContextContent.Length + $CurrentDirective.Length + $MasterPlan.Length
    $ArchTokenEstimate = [math]::Round($LenTotal / 4)

    Write-Host "`n=======================================================" -ForegroundColor Magenta
    Write-Host "[TELEMETRY] SESSION METRICS & PAYLOAD" -ForegroundColor Magenta
    Write-Host "  -> System Wall Clock:    $([math]::Round($WallClock.Elapsed.TotalMinutes, 2)) min" -ForegroundColor DarkGray
    Write-Host "  -> Executor Active Time: $([math]::Round($Telemetry.ExecutorTimeSec / 60, 2)) min" -ForegroundColor DarkGray
    Write-Host "  -> Architect Active Time:$([math]::Round($Telemetry.ArchitectTimeSec / 60, 2)) min" -ForegroundColor DarkGray
    Write-Host "  ---------------------------------------------------" -ForegroundColor Magenta
    Write-Host "  -> Cumulative Executor Tokens: ~$($Telemetry.ExecutorTokens)" -ForegroundColor DarkGray
    Write-Host "  -> Cumulative Architect Tokens: ~$($Telemetry.ArchitectTokens)" -ForegroundColor DarkGray
    Write-Host "  ---------------------------------------------------" -ForegroundColor Magenta
    Write-Host "  -> Current Architect Prompt Size: ~$LenTotal chars (~$ArchTokenEstimate tokens)" -ForegroundColor DarkGray
    if ($ImageSize -gt 0) {
        Write-Host "  -> Visual Attachment: framebuffer.png ($([math]::Round($ImageSize / 1024, 2)) KB)" -ForegroundColor DarkGray
    }
    Write-Host "=======================================================" -ForegroundColor Magenta
    # ====================================================================

    Write-Host "`n[ARCHITECT] Evaluating Executor outputs..." -ForegroundColor Cyan
    
    $TempArchitectFile = "temp_architect_prompt.txt"
    [System.IO.File]::WriteAllText($TempArchitectFile, $CleanArchitectPrompt, $Utf8NoBomEncoding)

    Clear-GeminiSessions # Wipe history again so Architect starts fresh.

    # ====================================================================
    # CAPACITY AWARE JITTER BACKOFF
    # ====================================================================
    $Attempt = 0
    $ArchitectSuccess = $false
    $ArchitectResponseRaw = ""

    $ArchTimer = [System.Diagnostics.Stopwatch]::StartNew()
    
    while (-not $ArchitectSuccess -and $Attempt -lt $MaxRetries) {
        $Attempt++
        Write-Host "[ARCHITECT] Awaiting evaluation (Attempt $Attempt/$MaxRetries)..." -ForegroundColor Cyan
        
        $ArchitectResponseRaw = ""
        $Pipeline = cmd.exe /c "gemini.cmd --approval-mode=yolo$ImageArg < `"$TempArchitectFile`" 2>&1"
        
        $Pipeline | ForEach-Object {
            Write-Host "  > $_" -ForegroundColor DarkGray
            $ArchitectResponseRaw += $_ + "`n"
        }
        
        if ($ArchitectResponseRaw -match "RESOURCE_EXHAUSTED" -or $ArchitectResponseRaw -match "429" -or $ArchitectResponseRaw -match "MODEL_CAPACITY_EXHAUSTED" -or $ArchitectResponseRaw -match "503 Service Unavailable" -or $ArchitectResponseRaw -match "quota exceeded" -or $ArchitectResponseRaw -match "maxSessionTurns") {
            if ($Attempt -ge $MaxRetries) { break } 
            
            if ($ArchitectResponseRaw -match "No capacity available" -or $ArchitectResponseRaw -match "MODEL_CAPACITY_EXHAUSTED") {
                $BaseDelay = [math]::Min(300, (30 * $Attempt)) 
                Write-Host "`n[ARCHITECT] Google Servers at Maximum Capacity." -ForegroundColor Red
            } else {
                $ExponentialMultiplier = [math]::Pow(2, $Attempt - 1)
                $BaseDelay = [math]::Min(60, (5 * $ExponentialMultiplier))
            }

            $JitterFactor = (Get-Random -Minimum 80 -Maximum 120) / 100.0
            $FinalDelay = [math]::Round($BaseDelay * $JitterFactor)
            
            Write-Host "[ORCHESTRATOR] API Rejected Request. Initiating recovery sleep..." -ForegroundColor DarkYellow
            for ($i = $FinalDelay; $i -gt 0; $i--) {
                Write-Progress -Activity "[ARCHITECT] Waiting for API Capacity..." -Status "$i seconds remaining" -PercentComplete (($FinalDelay - $i) / $FinalDelay * 100)
                Start-Sleep -Seconds 1
            }
            Write-Progress -Activity "[ARCHITECT] Waiting for API Capacity..." -Completed
            
        } else {
            $ArchitectSuccess = $true
        }
    }
    
    $ArchTimer.Stop()
    $Telemetry.ArchitectTimeSec += $ArchTimer.Elapsed.TotalSeconds

    # Tally Architect Tokens (Input + Output Estimate)
    $Telemetry.ArchitectTokens += $ArchTokenEstimate + [math]::Round($ArchitectResponseRaw.Length / 4)
    # ====================================================================
    
    if (Test-Path $TempArchitectFile) { Remove-Item $TempArchitectFile -Force }

    if (-not $ArchitectSuccess) {
        Write-Host "`n>>> FAIL-SAFE TRIGGERED: ARCHITECT RATE LIMIT EXCEEDED <<<" -ForegroundColor Red
        break
    }

    if ($ArchitectResponseRaw -match "Error executing tool") {
        Write-Host "`n[ARCHITECT] Tool error encountered. Forcing REWORK loop." -ForegroundColor Yellow
        $Decision = [PSCustomObject]@{
            action = "REWORK"
            assessment = [PSCustomObject]@{ critique = "A tool error disrupted the Architect's evaluation." }
            next_directive = "Your previous evaluation attempt failed due to a tool error. Correct your approach and try again."
        }
    } else {
        try {
            $CleanedRaw = $ArchitectResponseRaw -replace '```json', '' -replace '```', ''
            $JsonMatch = [regex]::Match($CleanedRaw, '\{.*\}', [System.Text.RegularExpressions.RegexOptions]::Singleline)
            
            if ($JsonMatch.Success) {
                $Decision = $JsonMatch.Value | ConvertFrom-Json
            } else {
                throw "No valid JSON object found in Architect output."
            }
            if ($null -eq $Decision.action) { throw "Model returned JSON, but missing mandatory 'action' key." }
        } catch {
            Write-Host "`n>>> FAIL-SAFE TRIGGERED: ARCHITECT PARSE ERROR <<<" -ForegroundColor Red
            Write-Host "Exception Detail: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host $ArchitectResponseRaw -ForegroundColor DarkGray
            break
        }
    }

    Write-Host "`n[ARCHITECT CRITIQUE] $($Decision.assessment.critique)" -ForegroundColor Gray

    # 6. State Routing & Anti-Loop Circuit Breaker
    if ($Decision.action -eq "HALT") {
        Write-Host "`n[ORCHESTRATOR] HALT TRIGGERED. Goal Achieved. Shutting down gracefully." -ForegroundColor Green
        git commit -m "Goal Achieved: $LongTermGoal" | Out-Null
        break
    } elseif ($Decision.action -eq "REWORK") {
        $ConsecutiveReworks++
        Write-Host "`n[ORCHESTRATOR] REWORK REQUIRED (Strike $ConsecutiveReworks/3). Generating fix directive." -ForegroundColor Red
        
        $CurrentDirective = "FIX PREVIOUS MISTAKES: $($Decision.next_directive)"
        
        if ($ConsecutiveReworks -ge 3) {
            Write-Host "[ORCHESTRATOR] CIRCUIT BREAKER TRIPPED: Injecting radical rethink prompt." -ForegroundColor DarkRed
            $CurrentDirective += "`n`n[SYSTEM OVERRIDE]: You have failed this step $ConsecutiveReworks times. You are stuck in a logic loop. Completely rethink your approach, discard previous assumptions, and use a drastically simpler method to achieve the goal."
        }
        git reset HEAD . | Out-Null
    } elseif ($Decision.action -eq "PROCEED") {
        $ConsecutiveReworks = 0 
        Write-Host "`n[ORCHESTRATOR] PROCEED APPROVED. Advancing state." -ForegroundColor Green
        $CurrentDirective = $Decision.next_directive
        git commit -m "Autocommit: Approved Step" | Out-Null
    }
}