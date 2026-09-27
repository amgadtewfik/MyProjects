#!/usr/bin/env python3
"""Build Windows executable for PLY Viewer v3"""

import sys
from pathlib import Path

def detect_implementation():
    """Detect and set up the right implementation (open3d-based)"""
    try: 
        import open3d
        return "open3d"
    except ImportError:
        pass
    
    try: 
        import pyvista as pv
        return "pyvista"
    except ImportError: 
        pass
    
    try:
        from PyQt5.QtCore import QLibraryInfo, QGuiApplication
        from PyQt5.QtWidgets import QApplication
        return "qt"
    except:
        return None


def build_opengl_exe():
     """Build executable with proper OpenGL support"""
     
     # Detect available implementation
     impl = detect_implementation()
     
    if not impl:
        print("No visualization library found. Install one of:")
        print("  pip install open3d")
print("Building windows exe...")
