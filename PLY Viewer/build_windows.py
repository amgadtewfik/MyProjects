#!/usr/bin/env python3
"""
Build script for creating Windows executable.
Run this script on a Windows machine to generate the .exe file.
"""

import subprocess
import sys
import os

def main():
    print("=" * 60)
    print("PLY Point Cloud Viewer - Windows Build Script")
    print("=" * 60)
    
    # Check if we're on Windows
    if sys.platform != 'win32':
        print("\n⚠️  WARNING: This script should be run on Windows.")
        print("    PyInstaller cannot cross-compile to Windows from other platforms.")
        print("\n    To build for Windows:")
        print("    1. Copy this folder to a Windows machine")
        print("    2. Run: python build_windows.py")
        print("\n    Continuing anyway for testing purposes...\n")
    
    # Install dependencies
    print("\n[1/3] Installing dependencies...")
    subprocess.run([sys.executable, "-m", "pip", "install", "--upgrade", "pip"], check=True)
    subprocess.run([sys.executable, "-m", "pip", "install", "open3d", "pyinstaller"], check=True)
    
    # Create spec content for PyInstaller
    print("\n[2/3] Creating PyInstaller configuration...")
    
    spec_content = '''# -*- mode: python ; coding: utf-8 -*-

block_cipher = None

a = Analysis(
    ['ply_viewer.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[
        'open3d',
        'open3d.visualization',
        'open3d.geometry',
        'open3d.io',
        'open3d.utility',
        'numpy',
        'PIL',
        'tkinter',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='PLY_Viewer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,  # Set to True if you want to see console output
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,  # Add path to .ico file if you have one
)
'''
    
    with open('ply_viewer.spec', 'w') as f:
        f.write(spec_content)
    
    # Build executable
    print("\n[3/3] Building executable...")
    subprocess.run([
        sys.executable, "-m", "PyInstaller",
        "--clean",
        "ply_viewer.spec"
    ], check=True)
    
    print("\n" + "=" * 60)
    print("Build complete!")
    print("=" * 60)
    print(f"\nExecutable location: {os.path.join(os.getcwd(), 'dist', 'PLY_Viewer.exe')}")
    print("\nUsage:")
    print("  - Double-click PLY_Viewer.exe to open the GUI")
    print("  - Or drag & drop a .ply file onto the executable")
    print("  - Or run from command line: PLY_Viewer.exe <file.ply>")


if __name__ == "__main__":
    main()
