"""Confetti overlay widget for PySide6."""

import random
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import QTimer, QRectF, Qt
from PySide6.QtGui import QPainter, QColor


class ConfettiWidget(QWidget):
    """Overlay that draws falling confetti particles."""

    COLORS = [
        QColor("#FF6B6B"), QColor("#4ECDC4"), QColor("#45B7D1"),
        QColor("#96CEB4"), QColor("#FFEAA7"), QColor("#DDA0DD"),
        QColor("#98D8C8"), QColor("#F7DC6F"), QColor("#BB8FCE"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setStyleSheet("background: transparent;")
        self.particles = []
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._tick)
        self.duration_ms = 2000
        self.elapsed = 0

    def start(self):
        self.particles = []
        w, h = max(self.width(), 700), max(self.height(), 450)
        for _ in range(100):
            self.particles.append({
                "x": random.uniform(0, w),
                "y": random.uniform(-200, h * 0.3),
                "color": random.choice(self.COLORS),
                "size": random.randint(6, 14),
                "vy": random.uniform(2, 8),
                "vx": random.uniform(-2, 2),
                "rotation": random.uniform(0, 360),
                "rotation_speed": random.uniform(-15, 15),
            })
        self.elapsed = 0
        self.timer.start(16)  # ~60 fps
        self.show()
        self.raise_()

    def _tick(self):
        self.elapsed += 16
        for p in self.particles:
            p["y"] += p["vy"]
            p["x"] += p["vx"]
            p["rotation"] += p["rotation_speed"]
        self.update()
        if self.elapsed >= self.duration_ms:
            self.timer.stop()
            self.hide()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        for p in self.particles:
            painter.save()
            painter.translate(p["x"], p["y"])
            painter.rotate(p["rotation"])
            painter.fillRect(
                QRectF(-p["size"] / 2, -p["size"] / 4, p["size"], p["size"] / 2),
                p["color"]
            )
            painter.restore()
