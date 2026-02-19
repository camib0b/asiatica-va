"""Team setup screen displayed before opening the work view."""

from qt_compat import QtCore, QtGui, QtWidgets
from styles import set_role, set_size, set_variant


class GameSetupWindow(QtWidgets.QWidget):
    team_setup_confirmed = QtCore.Signal(str, str, str, str, str)
    cancelled = QtCore.Signal()

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("AppRoot")
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_StyledBackground, True)
        self._video_path = ""
        self._build_ui()
        self._wire_signals()

    def set_video_path(self, path: str) -> None:
        self._video_path = path

    def set_team_defaults(self, home_name: str, away_name: str, home_color: str, away_color: str) -> None:
        self.home_name_edit.setText(home_name)
        self.away_name_edit.setText(away_name)
        self.home_color_edit.setText(home_color)
        self.away_color_edit.setText(away_color)

    def set_initial_focus(self) -> None:
        self.home_name_edit.setFocus()

    def _build_ui(self) -> None:
        outer = QtWidgets.QVBoxLayout(self)
        outer.setContentsMargins(24, 24, 24, 24)
        outer.addStretch(1)

        content = QtWidgets.QWidget(self)
        layout = QtWidgets.QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)

        title = QtWidgets.QLabel("Set up teams", content)
        title.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        set_role(title, "h1")

        subtitle = QtWidgets.QLabel("Enter team names and colors for this session.", content)
        subtitle.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        set_role(subtitle, "subhero")

        form = QtWidgets.QFormLayout()
        form.setSpacing(12)

        self.home_name_edit = QtWidgets.QLineEdit(content)
        self.home_name_edit.setPlaceholderText("e.g. Home Team")
        self.home_color_edit = QtWidgets.QLineEdit(content)
        self.home_color_edit.setPlaceholderText("#RRGGBB")

        self.away_name_edit = QtWidgets.QLineEdit(content)
        self.away_name_edit.setPlaceholderText("e.g. Away Team")
        self.away_color_edit = QtWidgets.QLineEdit(content)
        self.away_color_edit.setPlaceholderText("#RRGGBB")

        form.addRow("Home team:", self.home_name_edit)
        form.addRow("Home color:", self.home_color_edit)
        form.addRow("Away team:", self.away_name_edit)
        form.addRow("Away color:", self.away_color_edit)

        button_row = QtWidgets.QHBoxLayout()
        self.back_button = QtWidgets.QPushButton("&Back", content)
        self.back_button.setCursor(QtGui.QCursor(QtCore.Qt.CursorShape.PointingHandCursor))
        set_variant(self.back_button, "ghost")
        set_size(self.back_button, "md")

        self.continue_button = QtWidgets.QPushButton("&Continue", content)
        self.continue_button.setCursor(QtGui.QCursor(QtCore.Qt.CursorShape.PointingHandCursor))
        set_variant(self.continue_button, "welcomeImport")
        set_size(self.continue_button, "lg")

        button_row.addStretch(1)
        button_row.addWidget(self.back_button)
        button_row.addWidget(self.continue_button)
        button_row.addStretch(1)

        layout.addWidget(title)
        layout.addWidget(subtitle)
        layout.addLayout(form)
        layout.addLayout(button_row)

        outer.addWidget(content, 0, QtCore.Qt.AlignmentFlag.AlignCenter)
        outer.addStretch(1)

    def _wire_signals(self) -> None:
        self.back_button.clicked.connect(self.cancelled.emit)
        self.continue_button.clicked.connect(self._emit_continue)

    def _emit_continue(self) -> None:
        self.team_setup_confirmed.emit(
            self._video_path,
            self.home_name_edit.text().strip(),
            self.away_name_edit.text().strip(),
            self.home_color_edit.text().strip(),
            self.away_color_edit.text().strip(),
        )
