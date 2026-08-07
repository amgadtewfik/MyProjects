# cropcut.spec  — PyInstaller build config
# Run: pyinstaller cropcut.spec

from PyInstaller.building.build_main import Analysis, PYZ, EXE, COLLECT
import sys
import os

block_cipher = None

a = Analysis(
    ['main.py'],
    pathex=['.'],
    binaries=[],
    datas=[
        ('video-icon-8021-Windows.ico', '.'),
    ],
    hiddenimports=[
        'cv2',
        'numpy',
        'PyQt6',
        'PyQt6.QtCore',
        'PyQt6.QtGui',
        'PyQt6.QtWidgets',
        'PyQt6.sip',
        'PyQt6.QtMultimedia',
        'PyQt6.QtNetwork',
        'PyQt6.QtSql',
        'PyQt6.QtXml',
        'PyQt6.QtTest',
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
    name='CropCut',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,          # False = no console window on Windows
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,  # macOS uses .icns; set to 'CropCut.icns' if needed
)
