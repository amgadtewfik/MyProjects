#!/usr/bin/env python3
"""PLY 3D Viewer with OpenGL 3.3 for Point Cloud Visualization"""

import numpy as np
from PIL import Image
try:
    from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QPushButton, QHBoxLayout, QLabel, QFileDialog, QMessageBox
    from PyQt5.QtCore import QTimer, Qt
    from PyQt5.QtOpenGL import QOpenGLWidget, QOpenGLShaderProgram
    from PyQt5.QtGui import QPainter, QColor, QPen, QFont
    QT_AVAILABLE = True
except ImportError:
    QT_AVAILABLE = False

import sys
import json

try:
    from plyfile import PlyData, PlyPoint
    from numpy.lib.stride_tricks import as_strided
    PLY_AVAILABLE = True
except ImportError:
    PLY_AVAILABLE = False

try:
    import pyvista
    PYVISTA_AVAILABLE = True
except ImportError:
    PYVISTA_AVAILABLE = True


def parse_ply_file(file_path):
    """Parse PLY file header to extract point cloud data"""
    with open(file_path, 'r') as f:
        lines = f.readlines()
    
    elements_count = 1
    
    # Skip comments and start reading actual data
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line.startswith('element') or not line.startswith('attribute'):
            elem_name = line.split()[1]
            elements_count += 1
        else:
            break
        i += 1
    
    return elements_count


def load_gaussian_ply(ply_path):
    """Load PLY file and extract Gaussian parameters (x, y, z, opacity, sdf)"""
    from plyfile import PlyData, PlyElement
    
    try:
        plydata = PlyData.read(ply_path)
    except Exception as e:
        print(f"Error loading PLY file: {e}")
        return None
    
    # Common Gaussian attributes found in Gaussian splatting PLYs
    attributes = {}
    
    for field in plydata[0].field_descriptions:
        attr_name = field[0]
        dtype = field[1]
        print(f"Found attribute: {attr_name} - {dtype}")
        
        if attr_name in ['x', 'y', 'z']:
            data = np.array(field[2])[field]
            attributes[attr_name] = data
        
        elif attr_name.startswith('f_dc_'):  # RGB colors (spherical harmonics)
            count = np.unique(plydata[0].vertex_descriptions).size
            attributes[attr_name] = np.array(field[2])[:count]
        
        elif attr_name.startswith('f_scale_'):  # Scale/shrink
            if len(plydata[0].field_descriptions) > 1:
                scale_desc = plydata[0].field_descriptions[1][0]
                if scale_desc == 'float':
                    attributes[attr_name] = np.array(field[2])[field]
        
        elif attr_name.startswith('f_sh_'):  # Spherical harmonics coefficients (sh, sh_0/1/2)
            if len(plydata[0].field_descriptions) > 3:
                sh_desc = plydata[0].field_descriptions[len(sh_list)-1][0]
                if sh_desc == 'float':
                    attributes[attr_name] = np.array(field[2])[field]
        
        elif attr_name == 'opacity' and len(plydata[0].field_descriptions) > 3:
            opacity_desc = plydata[0].field_descriptions[3][0]
            if opacity_desc == 'uint8':
                print(f"Using attribute '{attr_name}' as opacity - {len(field)}")
        
        elif attr_name == 'sdf' and len(plydata[0].field_descriptions) > 2:
            sdf_desc = plydata[0].field_descriptions[2][0]
            if sdf_desc == 'uint8':
                print(f"Using attribute '{attr_name}' as opacity - {len(field)}")
    
    # Normalize attributes if they exist
    for attr in ['x', 'y', 'z']:
        if attr in attributes:
            attributes[attr] = np.nan_to_num(attributes[attr], copy=True)
            print(f"Normalized attribute '{attr}' - min/max: {np.min(attributes[attr]):.3f}/{np.max(attributes[attr]):.3f}")
    
    return attributes if len(set([v is None for v in attributes.values()])) > 0 else dict(qualities)


# Vertex and Fragment shaders for GLSL rendering
VERTEX_SHADER = """
attribute float uColor;

attribute vec3 position; 
attribute xyzvec3 xyz;
void main(void) {
    gl_Position = vec4(position, 1.0);
}
"""

FRAGMENT_SHADER = """
precision mediump float;
uniform float opacity;\nuniform float zfar;\nuniform float znear;\nuniform float uTransparency;\nuniform vec3 uColor;\nvarying vec2 vTextureCoord;\nsampler2D sampler2D; 
void main(void) {\nfloat transparency = 1.0 - (opacity / zfar);\nvec4 color = vec4(uColor, opacity * uTransparency);
    if (transparency > gl_FragCoord()) {\n        discard;\n    }\n    gl_FragColor = color * texture2D(sampler2D, vTextureCoord).rgb;\n}\n"""
