"""Welcome screen shown before video import."""

from qt_compat import QtCore, QtGui, QtWidgets
from styles import set_role, set_size, set_variant


class WelcomeWindow(QtWidgets.QWidget):
    video_import_requested = QtCore.Signal()

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("AppRoot")
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_StyledBackground, True)
        self._build_ui()
        self._wire_signals()
        self._build_shortcuts()
        self.setMinimumSize(250, 250)

    def _build_ui(self) -> None:
        outer = QtWidgets.QVBoxLayout(self)
        outer.setContentsMargins(24, 24, 24, 24)
        outer.addStretch(1)

        content = QtWidgets.QWidget(self)
        layout = QtWidgets.QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        self.header_label = QtWidgets.QLabel("this is ava", content)
        self.header_label.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        set_role(self.header_label, "h1")

        self.subtitle_label = QtWidgets.QLabel("Import a video file to get started", content)
        self.subtitle_label.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        set_role(self.subtitle_label, "subhero")

        self.import_button = QtWidgets.QPushButton("&Select video file", content)
        self.import_button.setCursor(QtGui.QCursor(QtCore.Qt.CursorShape.PointingHandCursor))
        set_variant(self.import_button, "welcomeImport")
        set_size(self.import_button, "lg")
        self.import_button.setMaximumWidth(400)

        layout.addWidget(self.header_label, 0, QtCore.Qt.AlignmentFlag.AlignHCenter)
        layout.addWidget(self.subtitle_label, 0, QtCore.Qt.AlignmentFlag.AlignHCenter)
        layout.addWidget(self.import_button, 0, QtCore.Qt.AlignmentFlag.AlignHCenter)

        outer.addWidget(content, 0, QtCore.Qt.AlignmentFlag.AlignCenter)
        outer.addStretch(1)

    def _wire_signals(self) -> None:
        self.import_button.clicked.connect(self.video_import_requested.emit)

    def _build_shortcuts(self) -> None:
        for key in (
            QtCore.Qt.Key.Key_S,
            QtCore.Qt.Key.Key_Space,
            QtCore.Qt.Key.Key_Return,
            QtCore.Qt.Key.Key_Enter,
        ):
            action = QtGui.QAction(self)
            action.setShortcut(QtGui.QKeySequence(key))
            action.setShortcutContext(QtCore.Qt.ShortcutContext.ApplicationShortcut)
            action.triggered.connect(self._trigger_import)
            self.addAction(action)

    def _trigger_import(self) -> None:
        if self.import_button.isEnabled():
            self.import_button.click()
