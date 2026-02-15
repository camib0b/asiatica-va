import sys
from PySide6.QtWidgets import (
    QApplication, QWidget, QLabel, QVBoxLayout,
    QPushButton, QLineEdit, QHBoxLayout, QDateEdit, QFileDialog
)
from PySide6.QtCore import QDate

from confetti import ConfettiWidget
from styles import (
    STYLE_LABEL_TITLE,
    STYLE_LABEL_DESCRIPTION,
    STYLE_BUTTON_IMPORT_VIDEO,
    STYLE_BUTTON_IMPORT_VIDEO_SELECTED,
    STYLE_LINE_EDIT,
    STYLE_DATE_EDIT,
    STYLE_MAIN_WINDOW,
)


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AVA by Camila Escudero")
        self.resize(700, 450)
        self.setStyleSheet(STYLE_MAIN_WINDOW)

        # Metadata:
        self.home_team = None
        self.away_team = None
        self.game_date = None

        # Guard flags:
        self.metadata_added = False
        self.video_imported = False

        # Layouts and widgets:
        self.layout = QVBoxLayout()
        self.label_title = QLabel("This is AVA")
        self.label_title.setStyleSheet(STYLE_LABEL_TITLE)

        self.label_description = QLabel("AVA is game footage analysis tool for everybody.")
        self.label_description.setStyleSheet(STYLE_LABEL_DESCRIPTION)

        self.button_import_video = QPushButton("Choose Video")
        self.button_import_video.setStyleSheet(STYLE_BUTTON_IMPORT_VIDEO)
        self.button_import_video.clicked.connect(self.import_video)

        self.layout.addWidget(self.label_title)
        self.layout.addWidget(self.label_description)
        self.layout.addWidget(self.button_import_video)

        self.setLayout(self.layout)

        self.confetti = ConfettiWidget(self)
        self.confetti.hide()

    def import_video(self):
        if self.metadata_added:
            self.label_description.setText("Move on with your life.")
            return

        # 1) Import video logic
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Choose Video", "", "Video Files (*.mp4 *.avi *.mov *.mkv)"
        )
        if not file_path:
            return  # User cancelled

        self.video_path = file_path
        self.video_imported = True
        self.button_import_video.setText(f"Video chosen: {file_path}")
        self.button_import_video.setStyleSheet(STYLE_BUTTON_IMPORT_VIDEO_SELECTED)

        # 2) Metadata handling
        self._add_metadata_section()

    def _add_metadata_section(self):
        """Build and add the metadata form (home team, away team, date, confirm)."""
        if self.metadata_added:
            return
        self.metadata_added = True

        self.layout_metadata = QHBoxLayout()
        self.input_home_team = QLineEdit()
        self.input_home_team.setStyleSheet(STYLE_LINE_EDIT)
        self.input_home_team.setPlaceholderText("Enter Home Team Name Here")
        self.input_home_team.textChanged.connect(self.update_home_team)

        self.input_away_team = QLineEdit()
        self.input_away_team.setStyleSheet(STYLE_LINE_EDIT)
        self.input_away_team.setPlaceholderText("Enter Away Team Name Here")
        self.input_away_team.textChanged.connect(self.update_away_team)

        self.input_game_date = QDateEdit()
        self.input_game_date.setStyleSheet(STYLE_DATE_EDIT)
        self.input_game_date.setDate(QDate.currentDate())
        self.input_game_date.setCalendarPopup(True)
        self.input_game_date.dateChanged.connect(self.update_game_date)

        self.button_confirm_metadata = QPushButton("Begin Analysis")
        self.button_confirm_metadata.clicked.connect(self.begin_analysis)

        self.layout_metadata.addWidget(self.input_home_team)
        self.layout_metadata.addWidget(self.input_away_team)
        self.layout_metadata.addWidget(self.input_game_date)
        self.layout_metadata.addWidget(self.button_confirm_metadata)

        self.layout.addLayout(self.layout_metadata)

    def _refresh_metadata_label(self):
        """Update label_description with current metadata input values."""
        home = self.input_home_team.text()
        away = self.input_away_team.text()
        date_str = self.input_game_date.date().toString("dd-MM-yyyy")
        self.label_description.setText(f"{home} (home) vs {away} (away) on {date_str}")

    def begin_analysis(self):
        self._refresh_metadata_label()
        self.confetti.setGeometry(0, 0, self.width(), self.height())
        self.confetti.start()


    def update_home_team(self, text):
        self.home_team = text
        self._refresh_metadata_label()

    def update_away_team(self, text):
        self.away_team = text
        self._refresh_metadata_label()

    def update_game_date(self, date):
        self.game_date = date.toString("dd-MM-yyyy")
        self._refresh_metadata_label()


app = QApplication(sys.argv)
window = MainWindow()
window.show()
sys.exit(app.exec())
