import sys
import os
import subprocess
import threading
import time
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QSlider, QFileDialog, QComboBox,
    QStatusBar, QFrame, QSizePolicy, QGroupBox, QSpinBox,
    QMessageBox, QProgressDialog, QToolButton
)
from PyQt6.QtCore import (
    Qt, QTimer, QThread, pyqtSignal, QRect, QPoint, QSize, pyqtSlot
)
from PyQt6.QtGui import (
    QImage, QPixmap, QPainter, QPen, QColor, QFont, QIcon,
    QCursor, QKeySequence, QShortcut
)

import cv2
import numpy as np


# ──────────────────────────────────────────────
#  Worker thread for FFmpeg processing
# ──────────────────────────────────────────────
class FFmpegWorker(QThread):
    progress = pyqtSignal(int)
    finished = pyqtSignal(bool, str)

    def __init__(self, cmd, total_frames=0):
        super().__init__()
        self.cmd = cmd
        self.total_frames = total_frames

    def run(self):
        try:
            proc = subprocess.Popen(
                self.cmd,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
            )
            for line in proc.stderr:
                if "frame=" in line and self.total_frames > 0:
                    try:
                        frame_str = line.split("frame=")[1].split()[0].strip()
                        frame_num = int(frame_str)
                        pct = min(int(frame_num / self.total_frames * 100), 99)
                        self.progress.emit(pct)
                    except Exception:
                        pass
            proc.wait()
            if proc.returncode == 0:
                self.progress.emit(100)
                self.finished.emit(True, "")
            else:
                err = proc.stderr.read() if proc.stderr else "Unknown error"
                self.finished.emit(False, err)
        except Exception as e:
            self.finished.emit(False, str(e))


# ──────────────────────────────────────────────
#  Video frame widget with crop overlay
# ──────────────────────────────────────────────
class VideoWidget(QLabel):
    crop_changed = pyqtSignal(QRect)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumSize(640, 360)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setStyleSheet("background-color: #0a0a0f; border: 1px solid #1e1e2e;")

        self._crop_rect = QRect()
        self._drag_start = QPoint()
        self._dragging = False
        self._crop_mode = False

        # Video dimensions
        self.vid_w = 0
        self.vid_h = 0
        self._current_pixmap = None

    def set_crop_mode(self, enabled: bool):
        self._crop_mode = enabled
        if enabled:
            self.setCursor(QCursor(Qt.CursorShape.CrossCursor))
        else:
            self.setCursor(QCursor(Qt.CursorShape.ArrowCursor))
        self.update()

    def clear_crop(self):
        self._crop_rect = QRect()
        self.crop_changed.emit(self._crop_rect)
        self.update()

    def get_crop_in_video_coords(self):
        """Return crop rect in original video pixel coordinates."""
        if self._crop_rect.isNull() or not self._current_pixmap:
            return None
        pw = self._current_pixmap.width()
        ph = self._current_pixmap.height()
        if pw == 0 or ph == 0:
            return None
        # Find where the pixmap is drawn inside the label (centered)
        lw, lh = self.width(), self.height()
        ox = (lw - pw) // 2
        oy = (lh - ph) // 2
        # Crop relative to pixmap
        rx = self._crop_rect.x() - ox
        ry = self._crop_rect.y() - oy
        rw = self._crop_rect.width()
        rh = self._crop_rect.height()
        # Scale to video coords
        sx = self.vid_w / pw
        sy = self.vid_h / ph
        x = max(0, int(rx * sx))
        y = max(0, int(ry * sy))
        w = min(self.vid_w - x, int(rw * sx))
        h = min(self.vid_h - y, int(rh * sy))
        if w > 0 and h > 0:
            return (x, y, w, h)
        return None

    def set_frame(self, pixmap: QPixmap):
        self._current_pixmap = pixmap
        self.update()

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Draw current frame
        if self._current_pixmap:
            lw, lh = self.width(), self.height()
            pm = self._current_pixmap.scaled(
                lw, lh,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation
            )
            ox = (lw - pm.width()) // 2
            oy = (lh - pm.height()) // 2
            painter.drawPixmap(ox, oy, pm)
            self._current_pixmap = self._current_pixmap  # keep ref

        # Draw crop overlay
        if not self._crop_rect.isNull():
            # Dark overlay outside selection
            lw, lh = self.width(), self.height()
            overlay = QColor(0, 0, 0, 130)
            painter.fillRect(0, 0, lw, self._crop_rect.top(), overlay)
            painter.fillRect(0, self._crop_rect.bottom(), lw, lh - self._crop_rect.bottom(), overlay)
            painter.fillRect(0, self._crop_rect.top(), self._crop_rect.left(), self._crop_rect.height(), overlay)
            painter.fillRect(self._crop_rect.right(), self._crop_rect.top(),
                             lw - self._crop_rect.right(), self._crop_rect.height(), overlay)

            # Crop border
            pen = QPen(QColor("#00f5d4"), 2, Qt.PenStyle.SolidLine)
            painter.setPen(pen)
            painter.drawRect(self._crop_rect)

            # Corner handles
            handle_size = 8
            painter.setBrush(QColor("#00f5d4"))
            corners = [
                self._crop_rect.topLeft(),
                self._crop_rect.topRight() - QPoint(handle_size, 0),
                self._crop_rect.bottomLeft() - QPoint(0, handle_size),
                self._crop_rect.bottomRight() - QPoint(handle_size, handle_size),
            ]
            for c in corners:
                painter.drawRect(c.x(), c.y(), handle_size, handle_size)

            # Dimensions label
            vc = self.get_crop_in_video_coords()
            if vc:
                x, y, w, h = vc
                label = f"{w} × {h}  ({x},{y})"
                painter.setPen(QColor("#00f5d4"))
                painter.setFont(QFont("Consolas", 9))
                painter.fillRect(
                    self._crop_rect.left(), self._crop_rect.top() - 22,
                    len(label) * 7 + 8, 20, QColor(0, 0, 0, 180)
                )
                painter.drawText(
                    self._crop_rect.left() + 4, self._crop_rect.top() - 6, label
                )

        # Crop mode hint
        if self._crop_mode:
            painter.setPen(QColor("#00f5d4"))
            painter.setFont(QFont("Consolas", 9))
            painter.drawText(8, self.height() - 8, "✚  CROP MODE — click and drag to select region")

        painter.end()

    def mousePressEvent(self, event):
        if self._crop_mode and event.button() == Qt.MouseButton.LeftButton:
            self._drag_start = event.position().toPoint()
            self._dragging = True
            self._crop_rect = QRect(self._drag_start, QSize(0, 0))

    def mouseMoveEvent(self, event):
        if self._dragging and self._crop_mode:
            end = event.position().toPoint()
            self._crop_rect = QRect(self._drag_start, end).normalized()
            self.crop_changed.emit(self._crop_rect)
            self.update()

    def mouseReleaseEvent(self, event):
        if self._dragging:
            self._dragging = False
            if self._crop_rect.width() < 10 or self._crop_rect.height() < 10:
                self._crop_rect = QRect()
            self.crop_changed.emit(self._crop_rect)
            self.update()


# ──────────────────────────────────────────────
#  Main Window
# ──────────────────────────────────────────────
class VideoEditorApp(QMainWindow):
    def __init__(self):
        super().__init__()
        
        # Set window icon
        try:
            import os
            app_dir = os.path.dirname(os.path.abspath(__file__))
            icon_path = os.path.join(app_dir, 'video-icon-8021-Windows.ico')
            if not os.path.exists(icon_path):
                icon_path = os.path.join(app_dir, '..', 'CropCut.app', 'Contents', 'Resources', 'CropCut.icns')
            if os.path.exists(icon_path):
                self.setWindowIcon(QIcon(icon_path))
        except Exception:
            pass
        
        self.setWindowTitle("CropCut — Video Crop & Upscale Editor")
        self.setMinimumSize(1100, 720)

        self.video_path = None
        self.cap = None
        self.total_frames = 0
        self.fps = 30
        self.vid_w = 0
        self.vid_h = 0
        self.current_frame = 0
        self.is_playing = False
        self._worker = None

        self._apply_theme()
        self._build_ui()
        self._connect_signals()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self._advance_frame)

    # ── Theme ─────────────────────────────────
    def _apply_theme(self):
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #0d0d1a;
                color: #c8c8e8;
                font-family: 'SF Pro Text', 'San Francisco', -apple-system, sans-serif;
                font-size: 13px;
            }
            QPushButton {
                background-color: #1a1a2e;
                border: 1px solid #2d2d50;
                border-radius: 6px;
                padding: 7px 16px;
                color: #c8c8e8;
                font-size: 13px;
            }
            QPushButton:hover { background-color: #252545; border-color: #00f5d4; color: #00f5d4; }
            QPushButton:pressed { background-color: #00f5d4; color: #0d0d1a; }
            QPushButton:disabled { color: #44445a; border-color: #1e1e3a; }
            QPushButton#accent {
                background-color: #00f5d4;
                color: #0d0d1a;
                font-weight: 600;
                border: none;
            }
            QPushButton#accent:hover { background-color: #00d4b8; }
            QPushButton#accent:disabled { background-color: #1a3a35; color: #2a5550; }
            QPushButton#danger {
                border-color: #f55050;
                color: #f55050;
            }
            QPushButton#danger:hover { background-color: #3a1515; }
            QSlider::groove:horizontal {
                height: 4px; background: #1e1e3a; border-radius: 2px;
            }
            QSlider::sub-page:horizontal {
                background: #00f5d4; border-radius: 2px;
            }
            QSlider::handle:horizontal {
                background: #00f5d4; width: 14px; height: 14px;
                margin: -5px 0; border-radius: 7px;
            }
            QSlider::handle:horizontal:hover { background: #ffffff; }
            QLabel { color: #c8c8e8; }
            QLabel#dim { color: #606080; font-size: 11px; }
            QLabel#info { color: #00f5d4; font-family: 'Consolas', monospace; font-size: 11px; }
            QComboBox {
                background-color: #1a1a2e; border: 1px solid #2d2d50;
                border-radius: 5px; padding: 5px 10px; color: #c8c8e8;
                min-width: 100px;
            }
            QComboBox:hover { border-color: #00f5d4; }
            QComboBox::drop-down { border: none; width: 20px; }
            QComboBox QAbstractItemView {
                background-color: #1a1a2e; border: 1px solid #2d2d50;
                selection-background-color: #252545; color: #c8c8e8;
            }
            QGroupBox {
                border: 1px solid #1e1e3a; border-radius: 8px;
                margin-top: 8px; padding: 12px 10px;
                font-size: 11px; color: #606080;
            }
            QGroupBox::title {
                subcontrol-origin: margin; subcontrol-position: top left;
                padding: 0 6px; color: #808090;
            }
            QStatusBar { background: #080810; color: #606080; font-size: 11px; }
            QFrame#sep { background: #1e1e3a; max-height: 1px; }
        """)

    # ── UI Builder ────────────────────────────
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Left panel (video + controls)
        left = QWidget()
        left.setMinimumWidth(700)
        lv = QVBoxLayout(left)
        lv.setContentsMargins(16, 12, 8, 12)
        lv.setSpacing(10)

        # Title bar
        title_row = QHBoxLayout()
        title_lbl = QLabel("🎬  CropCut")
        title_lbl.setStyleSheet("font-size: 18px; font-weight: 700; color: #ffffff; letter-spacing: 1px;")
        subtitle = QLabel("Video Crop & Resolution Enhancer")
        subtitle.setObjectName("dim")
        title_row.addWidget(title_lbl)
        title_row.addWidget(subtitle)
        title_row.addStretch()
        lv.addLayout(title_row)

        sep = QFrame(); sep.setObjectName("sep"); sep.setFixedHeight(1)
        lv.addWidget(sep)

        # Video widget
        self.video_widget = VideoWidget()
        lv.addWidget(self.video_widget, 1)

        # Placeholder text
        self.placeholder = QLabel("Drop an MP4 here  or  Open Video File ↓")
        self.placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.placeholder.setStyleSheet("color: #303055; font-size: 20px; font-style: italic;")
        self.video_widget.setLayout(QVBoxLayout())
        self.video_widget.layout().addWidget(self.placeholder)
        self.video_widget.layout().setAlignment(Qt.AlignmentFlag.AlignCenter)

        # Seek slider
        seek_row = QHBoxLayout()
        self.time_label = QLabel("0:00 / 0:00")
        self.time_label.setObjectName("info")
        self.time_label.setMinimumWidth(120)
        self.seek_slider = QSlider(Qt.Orientation.Horizontal)
        self.seek_slider.setRange(0, 1000)
        seek_row.addWidget(self.time_label)
        seek_row.addWidget(self.seek_slider, 1)
        lv.addLayout(seek_row)

        # Transport controls
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(8)

        self.btn_open = QPushButton("📂  Open Video")
        self.btn_play = QPushButton("▶  Play")
        self.btn_play.setFixedWidth(100)
        self.btn_stop = QPushButton("⏹  Stop")

        self.speed_label = QLabel("Speed:")
        self.speed_label.setObjectName("dim")
        self.speed_combo = QComboBox()
        self.speed_combo.addItems(["0.25×", "0.5×", "1×", "1.5×", "2×"])
        self.speed_combo.setCurrentText("1×")
        self.speed_combo.setFixedWidth(80)

        vol_label = QLabel("Vol:")
        vol_label.setObjectName("dim")
        self.vol_slider = QSlider(Qt.Orientation.Horizontal)
        self.vol_slider.setRange(0, 100)
        self.vol_slider.setValue(80)
        self.vol_slider.setFixedWidth(80)

        ctrl_row.addWidget(self.btn_open)
        ctrl_row.addWidget(self.btn_play)
        ctrl_row.addWidget(self.btn_stop)
        ctrl_row.addStretch()
        ctrl_row.addWidget(self.speed_label)
        ctrl_row.addWidget(self.speed_combo)
        lv.addLayout(ctrl_row)

        root.addWidget(left, 1)

        # ── Right sidebar
        sidebar = QWidget()
        sidebar.setFixedWidth(300)
        sidebar.setStyleSheet("background-color: #080810; border-left: 1px solid #1a1a2e;")
        sv = QVBoxLayout(sidebar)
        sv.setContentsMargins(14, 16, 14, 16)
        sv.setSpacing(14)

        # Video info group
        info_grp = QGroupBox("VIDEO INFO")
        info_layout = QVBoxLayout(info_grp)
        self.info_path  = QLabel("—"); self.info_path.setWordWrap(True)
        self.info_res   = QLabel("—")
        self.info_dur   = QLabel("—")
        self.info_fps   = QLabel("—")
        for lbl in [self.info_path, self.info_res, self.info_dur, self.info_fps]:
            lbl.setObjectName("info")
        info_layout.addWidget(QLabel("File:")); info_layout.addWidget(self.info_path)
        info_layout.addWidget(QLabel("Resolution:")); info_layout.addWidget(self.info_res)
        info_layout.addWidget(QLabel("Duration:")); info_layout.addWidget(self.info_dur)
        info_layout.addWidget(QLabel("Frame Rate:")); info_layout.addWidget(self.info_fps)
        sv.addWidget(info_grp)

        # Crop group
        crop_grp = QGroupBox("CROP SELECTION")
        crop_layout = QVBoxLayout(crop_grp)

        self.btn_crop_mode = QPushButton("✚  Enable Crop Tool")
        self.btn_crop_mode.setCheckable(True)
        crop_layout.addWidget(self.btn_crop_mode)

        self.crop_info = QLabel("No crop selected")
        self.crop_info.setObjectName("info")
        self.crop_info.setWordWrap(True)
        crop_layout.addWidget(self.crop_info)

        self.btn_clear_crop = QPushButton("✕  Clear Crop")
        self.btn_clear_crop.setObjectName("danger")
        self.btn_clear_crop.setEnabled(False)
        crop_layout.addWidget(self.btn_clear_crop)

        sv.addWidget(crop_grp)

        # Upscale group
        upscale_grp = QGroupBox("RESOLUTION ENHANCEMENT")
        upscale_layout = QVBoxLayout(upscale_grp)

        scale_row = QHBoxLayout()
        scale_lbl = QLabel("Upscale:")
        self.scale_combo = QComboBox()
        self.scale_combo.addItems(["Original (no upscale)", "1.5×", "2×", "3×", "4×"])
        scale_row.addWidget(scale_lbl)
        scale_row.addWidget(self.scale_combo, 1)
        upscale_layout.addLayout(scale_row)

        algo_row = QHBoxLayout()
        algo_lbl = QLabel("Algorithm:")
        self.algo_combo = QComboBox()
        self.algo_combo.addItems(["Lanczos (best quality)", "Bicubic (balanced)", "Bilinear (fast)"])
        algo_row.addWidget(algo_lbl)
        algo_row.addWidget(self.algo_combo, 1)
        upscale_layout.addLayout(algo_row)

        crf_row = QHBoxLayout()
        crf_lbl = QLabel("Quality (CRF):")
        self.crf_spin = QSpinBox()
        self.crf_spin.setRange(0, 51)
        self.crf_spin.setValue(18)
        self.crf_spin.setToolTip("Lower = better quality (0=lossless, 18=excellent, 23=default, 28=fast)")
        self.crf_spin.setStyleSheet("""
            QSpinBox { background: #1a1a2e; border: 1px solid #2d2d50;
                       border-radius: 4px; padding: 4px; color: #c8c8e8; }
            QSpinBox::up-button, QSpinBox::down-button { background: #252545; border: none; }
        """)
        crf_row.addWidget(crf_lbl)
        crf_row.addWidget(self.crf_spin)
        upscale_layout.addLayout(crf_row)

        self.output_size_label = QLabel("")
        self.output_size_label.setObjectName("info")
        upscale_layout.addWidget(self.output_size_label)

        sv.addWidget(upscale_grp)

        # Format group
        fmt_grp = QGroupBox("OUTPUT FORMAT")
        fmt_layout = QVBoxLayout(fmt_grp)
        fmt_row = QHBoxLayout()
        fmt_lbl = QLabel("Container:")
        self.fmt_combo = QComboBox()
        self.fmt_combo.addItems(["MP4 (H.264)", "MP4 (H.265/HEVC)", "WebM (VP9)", "MOV (ProRes)"])
        fmt_row.addWidget(fmt_lbl); fmt_row.addWidget(self.fmt_combo, 1)
        fmt_layout.addLayout(fmt_row)
        sv.addWidget(fmt_grp)

        sv.addStretch()

        self.btn_save = QPushButton("💾  Export / Save Video")
        self.btn_save.setObjectName("accent")
        self.btn_save.setFixedHeight(44)
        self.btn_save.setEnabled(False)
        sv.addWidget(self.btn_save)

        root.addWidget(sidebar)

        # Status bar
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.status.showMessage("Ready — Open a video file to start")

    # ── Signal connections ─────────────────────
    def _connect_signals(self):
        self.btn_open.clicked.connect(self._open_video)
        self.btn_play.clicked.connect(self._toggle_play)
        self.btn_stop.clicked.connect(self._stop_video)
        self.seek_slider.sliderMoved.connect(self._seek)
        self.seek_slider.sliderPressed.connect(lambda: self.timer.stop())
        self.seek_slider.sliderReleased.connect(self._seek_released)
        self.btn_crop_mode.toggled.connect(self._toggle_crop_mode)
        self.btn_clear_crop.clicked.connect(self._clear_crop)
        self.video_widget.crop_changed.connect(self._on_crop_changed)
        self.btn_save.clicked.connect(self._save_video)
        self.scale_combo.currentIndexChanged.connect(self._update_output_size_label)
        # Keyboard shortcuts
        QShortcut(QKeySequence("Space"), self, self._toggle_play)
        QShortcut(QKeySequence("Left"), self, lambda: self._step_frame(-1))
        QShortcut(QKeySequence("Right"), self, lambda: self._step_frame(1))

    # ── Video open ────────────────────────────
    def _open_video(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open Video", "",
            "Video files (*.mp4 *.mov *.avi *.mkv *.webm *.m4v);;All files (*)"
        )
        if not path:
            return
        self._load_video(path)

    def _load_video(self, path):
        if self.cap:
            self.cap.release()
        self.cap = cv2.VideoCapture(path)
        if not self.cap.isOpened():
            QMessageBox.critical(self, "Error", f"Could not open video:\n{path}")
            return
        self.video_path = path
        self.total_frames = int(self.cap.get(cv2.CAP_PROP_FRAME_COUNT))
        self.fps = self.cap.get(cv2.CAP_PROP_FPS) or 30
        self.vid_w = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.vid_h = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.video_widget.vid_w = self.vid_w
        self.video_widget.vid_h = self.vid_h
        self.current_frame = 0

        # Hide placeholder
        self.placeholder.hide()

        # Update info panel
        name = Path(path).name
        self.info_path.setText(name if len(name) < 30 else "..." + name[-27:])
        self.info_res.setText(f"{self.vid_w} × {self.vid_h}")
        dur_s = self.total_frames / self.fps
        self.info_dur.setText(f"{int(dur_s//60):02d}:{dur_s%60:05.2f}")
        self.info_fps.setText(f"{self.fps:.2f} fps  ({self.total_frames} frames)")

        self._update_output_size_label()
        self._render_frame(0)
        self.seek_slider.setValue(0)
        self.btn_save.setEnabled(True)
        self.btn_play.setText("▶  Play")
        self.is_playing = False
        self.status.showMessage(f"Loaded: {path}")

    # ── Playback ──────────────────────────────
    def _get_speed(self):
        text = self.speed_combo.currentText()
        return float(text.replace("×", ""))

    def _toggle_play(self):
        if not self.cap:
            return
        if self.is_playing:
            self.timer.stop()
            self.is_playing = False
            self.btn_play.setText("▶  Play")
        else:
            interval = max(1, int(1000 / (self.fps * self._get_speed())))
            self.timer.start(interval)
            self.is_playing = True
            self.btn_play.setText("⏸  Pause")

    def _stop_video(self):
        self.timer.stop()
        self.is_playing = False
        self.btn_play.setText("▶  Play")
        self._render_frame(0)
        self.seek_slider.setValue(0)

    def _advance_frame(self):
        if not self.cap:
            return
        ret, frame = self.cap.read()
        if not ret:
            self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            self.timer.stop()
            self.is_playing = False
            self.btn_play.setText("▶  Play")
            return
        self.current_frame = int(self.cap.get(cv2.CAP_PROP_POS_FRAMES))
        self._display_bgr(frame)
        pos = int(self.current_frame / max(1, self.total_frames) * 1000)
        self.seek_slider.setValue(pos)
        self._update_time_label()

    def _render_frame(self, frame_idx):
        if not self.cap:
            return
        self.cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
        ret, frame = self.cap.read()
        if ret:
            self.current_frame = frame_idx
            self._display_bgr(frame)
            self._update_time_label()

    def _display_bgr(self, frame):
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb.shape
        img = QImage(rgb.data, w, h, ch * w, QImage.Format.Format_RGB888)
        pm = QPixmap.fromImage(img)
        # Scale pixmap to fit widget, keeping aspect ratio
        lw = self.video_widget.width()
        lh = self.video_widget.height()
        scaled = pm.scaled(lw, lh, Qt.AspectRatioMode.KeepAspectRatio,
                           Qt.TransformationMode.SmoothTransformation)
        self.video_widget.set_frame(scaled)
        self.video_widget.setPixmap(scaled)

    def _seek(self, value):
        if not self.cap:
            return
        frame_idx = int(value / 1000 * self.total_frames)
        self._render_frame(frame_idx)

    def _seek_released(self):
        if self.is_playing:
            interval = max(1, int(1000 / (self.fps * self._get_speed())))
            self.timer.start(interval)

    def _step_frame(self, delta):
        if not self.cap:
            return
        nf = max(0, min(self.total_frames - 1, self.current_frame + delta))
        self._render_frame(nf)
        self.seek_slider.setValue(int(nf / max(1, self.total_frames) * 1000))

    def _update_time_label(self):
        cur = self.current_frame / self.fps
        tot = self.total_frames / self.fps
        self.time_label.setText(
            f"{int(cur//60):02d}:{cur%60:04.1f} / {int(tot//60):02d}:{tot%60:04.1f}"
        )

    # ── Crop ──────────────────────────────────
    def _toggle_crop_mode(self, checked):
        self.video_widget.set_crop_mode(checked)
        if checked:
            self.btn_crop_mode.setText("✚  Crop Tool Active")
            self.status.showMessage("Drag on the video to select crop area")
        else:
            self.btn_crop_mode.setText("✚  Enable Crop Tool")
            self.status.showMessage("")

    def _clear_crop(self):
        self.video_widget.clear_crop()
        self.btn_clear_crop.setEnabled(False)
        self.crop_info.setText("No crop selected")

    def _on_crop_changed(self, rect):
        vc = self.video_widget.get_crop_in_video_coords()
        if vc:
            x, y, w, h = vc
            self.crop_info.setText(f"X:{x}  Y:{y}\nW:{w}  H:{h}\nAspect: {w/max(1,h):.2f}")
            self.btn_clear_crop.setEnabled(True)
            self._update_output_size_label()
        else:
            self.crop_info.setText("No crop selected")
            self.btn_clear_crop.setEnabled(False)

    # ── Output size preview ───────────────────
    def _update_output_size_label(self):
        if not self.cap:
            self.output_size_label.setText("")
            return
        vc = self.video_widget.get_crop_in_video_coords()
        w, h = (vc[2], vc[3]) if vc else (self.vid_w, self.vid_h)
        scale_text = self.scale_combo.currentText()
        if scale_text.startswith("Original"):
            ow, oh = w, h
        else:
            factor = float(scale_text.replace("×", ""))
            ow = int(w * factor)
            oh = int(h * factor)
            # Round to even
            ow = ow + (ow % 2)
            oh = oh + (oh % 2)
        self.output_size_label.setText(f"Output: {ow} × {oh} px")

    # ── Save ──────────────────────────────────
    def _save_video(self):
        if not self.video_path:
            return
        # Pick output path
        fmt_text = self.fmt_combo.currentText()
        if "WebM" in fmt_text:
            ext = ".webm"
            filt = "WebM (*.webm)"
        elif "MOV" in fmt_text:
            ext = ".mov"
            filt = "MOV (*.mov)"
        else:
            ext = ".mp4"
            filt = "MP4 (*.mp4)"

        base = Path(self.video_path).stem
        default_out = str(Path(self.video_path).parent / f"{base}_cropped{ext}")
        out_path, _ = QFileDialog.getSaveFileName(self, "Save Video As", default_out, filt)
        if not out_path:
            return

        # Build ffmpeg command
        cmd = self._build_ffmpeg_cmd(out_path)
        if cmd is None:
            return

        # Stop playback
        self.timer.stop()
        self.is_playing = False
        self.btn_play.setText("▶  Play")

        # Progress dialog
        progress = QProgressDialog("Exporting video…", "Cancel", 0, 100, self)
        progress.setWindowTitle("Exporting")
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(0)
        progress.setStyleSheet("""
            QProgressDialog { background: #0d0d1a; color: #c8c8e8; }
            QProgressBar { border: 1px solid #2d2d50; background: #1a1a2e;
                           border-radius: 4px; text-align: center; }
            QProgressBar::chunk { background: #00f5d4; border-radius: 3px; }
        """)
        progress.show()

        self._worker = FFmpegWorker(cmd, self.total_frames)
        self._worker.progress.connect(progress.setValue)
        self._worker.finished.connect(lambda ok, err: self._on_export_done(ok, err, out_path, progress))
        progress.canceled.connect(lambda: self._worker.terminate() if self._worker else None)
        self._worker.start()

    def _build_ffmpeg_cmd(self, out_path):
        # Check ffmpeg is available
        try:
            subprocess.run(["ffmpeg", "-version"], capture_output=True, check=True)
        except Exception:
            QMessageBox.critical(self, "FFmpeg Not Found",
                "FFmpeg is required for export.\n\n"
                "Install it:\n"
                "  Windows: https://ffmpeg.org/download.html\n"
                "  macOS:   brew install ffmpeg\n"
                "  Linux:   sudo apt install ffmpeg")
            return None

        cmd = ["ffmpeg", "-y", "-i", self.video_path]

        # Build vf filters
        vf_parts = []

        # Crop filter
        vc = self.video_widget.get_crop_in_video_coords()
        if vc:
            x, y, w, h = vc
            vf_parts.append(f"crop={w}:{h}:{x}:{y}")

        # Scale filter
        scale_text = self.scale_combo.currentText()
        if not scale_text.startswith("Original"):
            factor = float(scale_text.replace("×", ""))
            algo_text = self.algo_combo.currentText()
            if "Lanczos" in algo_text:
                sws = "lanczos"
            elif "Bicubic" in algo_text:
                sws = "bicubic"
            else:
                sws = "bilinear"

            # Calculate output dimensions
            src_w = vc[2] if vc else self.vid_w
            src_h = vc[3] if vc else self.vid_h
            ow = int(src_w * factor)
            oh = int(src_h * factor)
            ow += ow % 2
            oh += oh % 2
            vf_parts.append(f"scale={ow}:{oh}:flags={sws}")

        if vf_parts:
            cmd += ["-vf", ",".join(vf_parts)]

        # Codec selection
        fmt_text = self.fmt_combo.currentText()
        crf = self.crf_spin.value()
        if "H.265" in fmt_text:
            cmd += ["-c:v", "libx265", "-crf", str(crf), "-preset", "medium", "-tag:v", "hvc1"]
        elif "VP9" in fmt_text:
            cmd += ["-c:v", "libvpx-vp9", "-crf", str(crf), "-b:v", "0"]
        elif "ProRes" in fmt_text:
            cmd += ["-c:v", "prores_ks", "-profile:v", "3"]
        else:
            cmd += ["-c:v", "libx264", "-crf", str(crf), "-preset", "medium"]

        # Audio copy
        cmd += ["-c:a", "aac", "-b:a", "192k"]
        cmd.append(out_path)
        return cmd

    def _on_export_done(self, ok, err, out_path, progress):
        progress.close()
        if ok:
            QMessageBox.information(self, "Export Complete",
                f"✅  Video saved successfully!\n\n{out_path}")
            self.status.showMessage(f"Saved: {out_path}")
        else:
            QMessageBox.critical(self, "Export Failed",
                f"❌  FFmpeg error:\n\n{err[:500]}")
            self.status.showMessage("Export failed")

    def closeEvent(self, event):
        if self.cap:
            self.cap.release()
        event.accept()


# ──────────────────────────────────────────────
#  Entry point
# ──────────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CropCut")
    app.setOrganizationName("CropCut")
    win = VideoEditorApp()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
