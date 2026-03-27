@echo off
echo ========================================================
echo [Pre-Flight] Syncing Framework to Agent Knowledge Base...
echo ========================================================

:: Create the knowledge directory if it doesn't exist
if not exist "knowledge" mkdir "knowledge"

:: Quietly copy include and src files (/Y = yes to overwrite, /E = recursive, /Q = quiet)
xcopy "..\include\*" "knowledge\" /Y /E /Q
xcopy "..\src\*" "knowledge\" /Y /E /Q

echo [Pre-Flight] Sync complete. 
echo [Pre-Flight] Launching Agent Environment via PowerShell...
echo ========================================================

:: Launch PowerShell, keep the window open (-NoExit), and run the agent script
:: NOTE: Replace 'python run_agent.py' with your actual command to start the Gemini agent.
powershell.exe -NoExit -Command "Write-Host 'Starting Gemini Agent...' -ForegroundColor Green; gemini"