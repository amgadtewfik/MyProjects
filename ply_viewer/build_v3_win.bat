@echo off
TITLE PLY Viewer v3 - Build for Windows
cls

echo ============================================
echo Building PLY Viewer v3 with Open3D
echo Gaussian Splatting Color Support Enabled
echo ============================================
echo.

pip uninstall plyfile open3d opencv-python -y 2>nul || echo (packages already absent ok)
pip install pyinstaller plyfile open3d opencv-python>=4.5.0 -q --no-deps 2>nul

cd /Users/amgad/Desktop/Ai/ply_viewer/v3

REM Build Windows GUI executable
pyinstaller --onefile --windowed --name "PLY_Gaussian_Splatting_v3" window_v3.py --hidden-import=tkinter --hidden-import=open3d --add-data "*.dll;." 2>&1

if %errorlevel% == 0 (
    echo.
    echo =========================
    echo BUILD SUCCESSFUL!
    echo Executable: dist\PLY_Gaussian_Splatting_v3.exe
    echo.
    pause
) else (
    echo.
    echo ============================================
    echo BUILD FAILED!
    echo ============================================
    pause
)
