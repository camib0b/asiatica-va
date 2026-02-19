"""Qt binding compatibility layer for AVA.

Defaults to PySide6 and falls back to PyQt6 when needed.
"""

try:
    from PySide6 import QtCore, QtGui, QtWidgets, QtMultimedia, QtMultimediaWidgets

    Signal = QtCore.Signal
    Slot = QtCore.Slot
    BINDING = "PySide6"
except ImportError:  # pragma: no cover
    from PyQt6 import QtCore, QtGui, QtWidgets, QtMultimedia, QtMultimediaWidgets

    Signal = QtCore.pyqtSignal
    Slot = QtCore.pyqtSlot
    BINDING = "PyQt6"

__all__ = [
    "QtCore",
    "QtGui",
    "QtWidgets",
    "QtMultimedia",
    "QtMultimediaWidgets",
    "Signal",
    "Slot",
    "BINDING",
]
