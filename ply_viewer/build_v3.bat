@echo off
cls
echo ============================================
echo PLY Viewer v3 Build Script for Windows
echo ============================================
echo.

python build.py --name "PLY_Gaussian_Splatting_v3" --onefile

if %errorlevel% == 0 (
    echo.
    echo SUCCESS! The executable should be built into ply_viewer.exe
    dir /a -b C:\src\*.exe > output.txt
    
    echo ============================================
    echo Build complete! 
    echo ============================================
    pause
)
