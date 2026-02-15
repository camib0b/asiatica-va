import sys
from pathlib import Path
from confetti import ConfettiWidget
from PySide6.QtWidgets import (
    QApplication, QWidget, QLabel, QVBoxLayout,
    QPushButton, QLineEdit, QHBoxLayout, QDateEdit, QFileDialog, QSizePolicy,
    QGroupBox, QFormLayout, QMainWindow, QStatusBar, QMessageBox
)
from PySide6.QtCore import QDate, Qt
from PySide6.QtGui import QDragEnterEvent, QDropEvent

from styles import (
    load_stylesheet,
    polish_widget,
    ROLE_MAIN_WINDOW,
    ROLE_LABEL_TITLE,
    ROLE_LABEL_DESCRIPTION,
    ROLE_IMPORT_VIDEO,
    ROLE_IMPORT_VIDEO_SELECTED,
    ROLE_METADATA_INPUT,
)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AVA by Camila Escudero")
        self.resize(800, 500)
        self.setMinimumSize(800, 500)   # prevents ugly squashing
        self.setProperty("role", ROLE_MAIN_WINDOW)

        # Central widget:
        central = QWidget()
        self.setCentralWidget(central)
        self.setAcceptDrops(True)
        self.layout = QVBoxLayout(central)
        self.layout.setSpacing(16)
        self.layout.setContentsMargins(24, 24, 24, 24)


        # self.setWindowIcon(QIcon(":/icons/ava.png"))
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        # Metadata:
        self.home_team = None
        self.away_team = None
        self.game_date = None

        self.metadata_added = False
        self.video_imported = False
        self.video_path = None
        self.metadata_added = False

        # Layouts and widgets (layout already created above for central widget):
        # Title:
        self.label_title = QLabel("This is AVA")
        self.label_title.setProperty("role", ROLE_LABEL_TITLE)

        # Description:
        self.label_description = QLabel("AVA is game footage analysis tool for everybody.")
        self.label_description.setProperty("role", ROLE_LABEL_DESCRIPTION)

        # Import video button:
        self.button_import_video = QPushButton("Choose Video")
        self.button_import_video.setProperty("role", ROLE_IMPORT_VIDEO)
        self.button_import_video.clicked.connect(self.import_video)

        # Add widgets to layout:
        self.layout.addWidget(self.label_title)
        self.layout.addWidget(self.label_description)
        self.layout.addWidget(self.button_import_video)
        self.layout.addStretch()

        # Status bar:
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("Ready – choose a video to start")

        # Menu bar:
        menubar = self.menuBar()
        file_menu = menubar.addMenu("&File")
        file_menu.addAction("Import Video").triggered.connect(self.import_video)
        file_menu.addSeparator()
        file_menu.addAction("Exit").triggered.connect(self.close)

        help_menu = menubar.addMenu("&Help")
        help_menu.addAction("About AVA").triggered.connect(self.show_about)

        # Confetti:
        self.confetti = ConfettiWidget(self)
        self.confetti.hide()

    def dragEnterEvent(self, event: QDragEnterEvent):
        """Only accept video files when we haven't started analysis yet."""
        self.setStyleSheet("QMainWindow { background-color: #e0f2fe; }")  # light blue tint
        if self.metadata_added:
            event.ignore()
            return

        mime = event.mimeData()
        if mime.hasUrls():
            for url in mime.urls():
                if url.isLocalFile():
                    path = url.toLocalFile()
                    if path.lower().endswith(('.mp4', '.avi', '.mov', '.mkv')):
                        event.acceptProposedAction()
                        self.statusBar().showMessage("Drop to load video", 1500)
                        return
        event.ignore()

    def dropEvent(self, event: QDropEvent):
        """Handle the actual drop."""
        for url in event.mimeData().urls():
            if url.isLocalFile():
                path = url.toLocalFile()
                if path.lower().endswith(('.mp4', '.avi', '.mov', '.mkv')):
                    self.process_video_file(path)
                    return

    def process_video_file(self, file_path: str):
        """Common logic for both QFileDialog and drag & drop."""
        if self.metadata_added:
            self.statusBar().showMessage("Analysis already started.")
            return

        self.video_path = file_path
        self.video_imported = True

        filename = Path(file_path).name
        self.button_import_video.setText(f"Video: {filename}")
        self.button_import_video.setProperty("role", ROLE_IMPORT_VIDEO_SELECTED)
        polish_widget(self.button_import_video)

        self.statusBar().showMessage(f"Video loaded: {filename}", 4000)

        self._add_metadata_section()





    def import_video(self):
        if self.metadata_added:
            self.label_description.setText("Analysis already started.")
            return

        file_path, _ = QFileDialog.getOpenFileName(
            self, "Choose Video", "", "Video Files (*.mp4 *.avi *.mov *.mkv)"
        )
        if file_path:
            self.process_video_file(file_path)

    def _add_metadata_section(self):
        if self.metadata_added:
            return

        group = QGroupBox("Game Metadata")
        form = QFormLayout(group)
        form.setLabelAlignment(Qt.AlignRight)
        form.setSpacing(12)

        self.input_home_team = QLineEdit()
        self.input_home_team.setProperty("role", ROLE_METADATA_INPUT)
        self.input_home_team.setPlaceholderText("Home Team")
        self.input_home_team.textChanged.connect(self._validate_confirm)

        self.input_away_team = QLineEdit()
        self.input_away_team.setProperty("role", ROLE_METADATA_INPUT)
        self.input_away_team.setPlaceholderText("Away Team")
        self.input_away_team.textChanged.connect(self._validate_confirm)

        self.input_game_date = QDateEdit()
        self.input_game_date.setProperty("role", ROLE_METADATA_INPUT)
        self.input_game_date.setDate(QDate.currentDate())
        self.input_game_date.setCalendarPopup(True)
        self.input_game_date.dateChanged.connect(self.update_game_date)

        self.button_confirm_metadata = QPushButton("Begin Analysis")
        self.button_confirm_metadata.setProperty("role", "confirm")
        self.button_confirm_metadata.setEnabled(False)
        self.button_confirm_metadata.clicked.connect(self.begin_analysis)

        form.addRow("Home Team:", self.input_home_team)
        form.addRow("Away Team:", self.input_away_team)
        form.addRow("Game Date:", self.input_game_date)
        form.addRow("", self.button_confirm_metadata)

        self.layout.addWidget(group)
        self.metadata_added = True

    def _validate_confirm(self):
        home = self.input_home_team.text().strip()
        away = self.input_away_team.text().strip()
        enabled = bool(home and away)

        self.button_confirm_metadata.setEnabled(enabled)

        if enabled:
            self.statusBar().showMessage("Ready to analyse – click Begin Analysis")
        else:
            self.statusBar().showMessage("Enter both team names")

    def begin_analysis(self):
        self.statusBar().showMessage("Starting analysis…", 3000)
        self.confetti.setGeometry(0, 0, self.width(), self.height())
        self.confetti.start()

    def show_about(self):
        QMessageBox.about(self, "About AVA",
            "AVA v0.1\nGame Footage Analysis Tool\nby Camila Escudero")

    def update_game_date(self, date):
        self.game_date = date.toString("dd-MM-yyyy")


app = QApplication(sys.argv)
app.setStyleSheet(load_stylesheet())
window = MainWindow()
window.show()
sys.exit(app.exec())
