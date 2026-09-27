#!/bin/bash 
# PLY Viewer v3 Build Script - For Windows Users (copy as .bat)
# Creates executable from ply_viewer.py with Open3D support and proper Gaussian color sigmoid transform

echo "========================================="
echo "PLY Viewer v3 - Building for Windows"
echo "Gaussian Splatting Color Support: ENABLED"
echo "(Sigmoid transform applied to f_dc_0/1/2 SH coefficients)"
echo "========================================="
echo

# First clean build directory
cd /Users/amgad/Desktop/Ai/ply_viewer || { echo "Error: ply_viewer directory not found"; exit 1; }

if [ -d dist ]; then
        rm -rf dist build
    fi
    
if [ -d spec ]; then
    rm -rf spec
else
   mkdir spec  
fi

echo "Installing required Python packages..."
pip install pyinstaller plyfile open3d opencv-python>=4.5.0 2>&1 | tee build_output.txt

echo ""
echo "Building executable with PyInstaller..."
cd /Users/amgad/Desktop/Ai/ply_viewer/src/v1_alpha || { echo "Error: src not found"; exit 1; }

pyinstaller --onefile \
             --windowed \
             --name "PLY_Gaussian_Splatting_v3" \
             --hidden-import plyfile \
             --hidden-import open3d \
             --hidden-import numpy \
             --add-data "ply_viewer.py;" \
             ply_viewer.py

echo ""
echo "========================================="
echo "BUILD COMPLETE!"
echo "Executable location: C:\temp\dist\PLY_Gaussian_Splatting_v3.exe" 
echo "or on Windows after copying build.bat: dist\PLY_Gaussian_Splatting_v3.exe"
echo "========================================="

if [ $? -eq 0 ]; then
    echo ""
    echo "If you see 'BUILD SUCCESSFUL' below, copy the .exe to a USB drive or network location."
    echo "Test by opening: PLY_Gaussian_Splatting_v3.exe"
fi
