@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "INPUT_DIR=%SCRIPT_DIR%input"
set "OUTPUT_DIR=%SCRIPT_DIR%output"
set "GENERATE_SRT=false"

for %%a in (%*) do (
    if "%%a"=="--srt" set "GENERATE_SRT=true"
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo ===== STEP 1: Converting m2ts to mp4 =====
cd /d "%INPUT_DIR%"

set count=0
set total=0
for %%f in (*.m2ts) do set /a total+=1

echo Found %total% m2ts files

for %%f in (*.m2ts) do (
    set /a count+=1
    set "filename=%%~nf"
    echo [!count!/!total!] Converting: %%f
    python "%SCRIPT_DIR%converter.py" "%INPUT_DIR%\%%f" -o "%OUTPUT_DIR%\!filename!.mp4" -v copy -a copy
)

echo.
echo Step 1 complete: %count% files converted
echo.

if "!GENERATE_SRT!"=="true" (
    echo ===== STEP 2: Generating SRT subtitles =====
    cd /d "%OUTPUT_DIR%"
    
    set srt_count=0
    set srt_total=0
    for %%f in (*.mp4) do set /a srt_total+=1
    
    echo Found !srt_total! mp4 files
    
    for %%f in (*.mp4) do (
        set /a srt_count+=1
        set "filename=%%~nf"
        echo [!srt_count!/!srt_total!] Generating SRT: %%f
        python "%SCRIPT_DIR%converter.py" "%%f" --srt
    )
    
    echo.
    echo Step 2 complete: !srt_count! SRT files generated
)

echo.
echo ===== DONE =====
echo Output: %OUTPUT_DIR%
pause