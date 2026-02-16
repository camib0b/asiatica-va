from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput
from PySide6.QtMultimediaWidgets import QVideoWidget

import sys
from pathlib import Path
from confetti import ConfettiWidget
from PySide6.QtWidgets import (
    QApplication, QWidget, QLabel, QVBoxLayout,
    QPushButton, QLineEdit, QHBoxLayout, QDateEdit, QFileDialog, QSizePolicy,
    QGroupBox, QFormLayout, QMainWindow, QStatusBar, QMessageBox,
    QSlider, QSplitter, QListWidget, QComboBox
)

from PySide6.QtCore import QDate, Qt, QUrl
from PySide6.QtGui import QDragEnterEvent, QDropEvent, QKeySequence, QAction

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

        self.player = None
        self.audio_output = None
        self.video_widget = None
        self.slider = None
        self.time_label = None
        self.play_btn = None
        self.tag_list = None
        self.tag_combo = None
        self.tags = [] # list of (milliseconds, event_name)
        self.metadata_group = None # will hold the QGroupBox

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

        # === MENU + SHORTCUTS ===
        menubar = self.menuBar()

        file_menu = menubar.addMenu("&File")
        open_act = QAction("&Open Video", self)
        open_act.setShortcut(QKeySequence("Ctrl+O"))
        open_act.triggered.connect(self.import_video)
        file_menu.addAction(open_act)

        file_menu.addSeparator()
        quit_act = QAction("E&xit", self)
        quit_act.setShortcut(QKeySequence("Ctrl+Q"))
        quit_act.triggered.connect(self.close)
        file_menu.addAction(quit_act)

        help_menu = menubar.addMenu("&Help")
        about_act = QAction("&About AVA", self)
        about_act.triggered.connect(self.show_about)
        help_menu.addAction(about_act)

        # Store references for later tab order
        self.button_confirm_metadata = None   # will be set when created

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
        self.metadata_added = True

        self.metadata_group = QGroupBox("Game Metadata")
        form = QFormLayout(self.metadata_group)
        form.setLabelAlignment(Qt.AlignRight)
        form.setSpacing(12)

        self.input_home_team = QLineEdit()
        self.input_home_team.setProperty("role", ROLE_METADATA_INPUT)
        self.input_home_team.setPlaceholderText("Home Team")
        self.input_home_team.textChanged.connect(self._validate_confirm)
        self.input_home_team.returnPressed.connect(self._focus_next_field) # ← new

        self.input_away_team = QLineEdit()
        self.input_away_team.setProperty("role", ROLE_METADATA_INPUT)
        self.input_away_team.setPlaceholderText("Away Team")
        self.input_away_team.textChanged.connect(self._validate_confirm)
        self.input_away_team.returnPressed.connect(self._focus_next_field)

        self.input_game_date = QDateEdit()
        self.input_game_date.setProperty("role", ROLE_METADATA_INPUT)
        self.input_game_date.setDate(QDate.currentDate())
        self.input_game_date.setCalendarPopup(True)
        self.input_game_date.dateChanged.connect(self.update_game_date)
        self.input_game_date.returnPressed.connect(self._try_confirm) # ← new

        self.button_confirm_metadata = QPushButton("Begin Analysis")
        self.button_confirm_metadata.setProperty("role", "confirm")
        self.button_confirm_metadata.setEnabled(False)
        self.button_confirm_metadata.clicked.connect(self.begin_analysis)

        form.addRow("Home Team:", self.input_home_team)
        form.addRow("Away Team:", self.input_away_team)
        form.addRow("Game Date:", self.input_game_date)
        form.addRow("", self.button_confirm_metadata)

        self.layout.addWidget(self.metadata_group)

        # === TAB ORDER (logical flow) ===
        self.setTabOrder(self.button_import_video, self.input_home_team)
        self.setTabOrder(self.input_home_team, self.input_away_team)
        self.setTabOrder(self.input_away_team, self.input_game_date)
        self.setTabOrder(self.input_game_date, self.button_confirm_metadata)
        self.setTabOrder(self.button_confirm_metadata, self.button_import_video)
        self.button_confirm_metadata.setFocusPolicy(Qt.TabFocus)

        # Auto-focus first field after video is loaded
        self.input_home_team.setFocus()

    def _refresh_metadata_label(self):
        """Update the top description label with the final metadata."""
        if not hasattr(self, 'input_home_team'):
            return

        home = self.input_home_team.text().strip() or "Home"
        away = self.input_away_team.text().strip() or "Away"
        date_str = self.input_game_date.date().toString("dd-MM-yyyy")

        self.label_description.setText(f"{home} vs {away} • {date_str}")

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
        """Called when user clicks 'Begin Analysis'"""
        self._refresh_metadata_label()          # updates the top description once
        if hasattr(self, 'metadata_group') and self.metadata_group:
            self.metadata_group.hide()

        self._setup_analysis_ui()               # ← builds the video + tags UI

        self.statusBar().showMessage(
            "Video ready • Space = play/pause • T = add tag • ← → = ±5 s"
        )

    def _setup_analysis_ui(self):
        """Build the video + tags interface after metadata is confirmed."""
        # Video player
        self.video_widget = QVideoWidget()
        self.player = QMediaPlayer(self)
        self.audio_output = QAudioOutput(self)
        self.player.setAudioOutput(self.audio_output)
        self.player.setVideoOutput(self.video_widget)

        # Controls
        controls = QHBoxLayout()
        self.play_btn = QPushButton("Play")
        self.play_btn.clicked.connect(self.toggle_play)
        controls.addWidget(self.play_btn)

        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.sliderMoved.connect(self.seek)
        controls.addWidget(self.slider)

        self.time_label = QLabel("00:00 / 00:00")
        controls.addWidget(self.time_label)

        video_layout = QVBoxLayout()
        video_layout.addWidget(self.video_widget)
        video_layout.addLayout(controls)

        video_container = QWidget()
        video_container.setLayout(video_layout)

        # Tags panel
        tag_panel = QWidget()
        tag_layout = QVBoxLayout(tag_panel)

        label = QLabel("Event Tags")
        label.setStyleSheet("font-weight: bold; font-size: 14px;")
        tag_layout.addWidget(label)

        self.tag_combo = QComboBox()
        self.tag_combo.addItems([
            "Goal", "Penalty Corner", "Field Goal", "Save",
            "Turnover", "Free Hit", "Green Card", "Yellow Card"
        ])
        tag_layout.addWidget(self.tag_combo)

        add_btn = QPushButton("Add Tag (T)")
        add_btn.clicked.connect(self.add_current_tag)
        tag_layout.addWidget(add_btn)

        self.tag_list = QListWidget()
        self.tag_list.itemClicked.connect(self.seek_to_tag)
        tag_layout.addWidget(self.tag_list)

        # Splitter: tags | video
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(tag_panel)
        splitter.addWidget(video_container)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)

        self.layout.addWidget(splitter)

        # Connect player signals
        self.player.durationChanged.connect(self.update_duration)
        self.player.positionChanged.connect(self.update_position)
        self.player.playbackStateChanged.connect(self.update_play_button)

        # Load and start video
        self.player.setSource(QUrl.fromLocalFile(self.video_path))
        self.player.play()

    def show_about(self):
        QMessageBox.about(self, "About AVA",
            "AVA v0.1\nGame Footage Analysis Tool\nby Camila Escudero")

    def update_game_date(self, date):
        self.game_date = date.toString("dd-MM-yyyy")

    def _focus_next_field(self):
        """Move focus when user presses Enter in a line edit."""
        widget = self.focusWidget()
        if widget == self.input_home_team:
            self.input_away_team.setFocus()
        elif widget == self.input_away_team:
            self.input_game_date.setFocus()

    def _try_confirm(self):
        """If the confirm button is enabled, trigger it when user presses Enter in date field."""
        if self.button_confirm_metadata.isEnabled():
            self.begin_analysis()

    def format_time(self, ms: int) -> str:
        if ms < 0:
            return "00:00"
        seconds = ms // 1000
        minutes = seconds // 60
        seconds = seconds % 60
        return f"{minutes:02d}:{seconds:02d}"

    def toggle_play(self):
        if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            self.player.play()

    def update_play_button(self, state):
        self.play_btn.setText("Pause" if state == QMediaPlayer.PlaybackState.PlayingState else "Play")

    def update_duration(self, duration):
        self.slider.setRange(0, duration)

    def update_position(self, position):
        self.slider.blockSignals(True)
        self.slider.setValue(position)
        self.slider.blockSignals(False)
        self.time_label.setText(
            f"{self.format_time(position)} / {self.format_time(self.player.duration())}"
        )

    def seek(self, position):
        if self.player:
            self.player.setPosition(position)

    def add_current_tag(self):
        if not self.player or self.player.duration() == 0:
            return
        ms = self.player.position()
        event = self.tag_combo.currentText()
        self.tags.append((ms, event))

        item_text = f"{self.format_time(ms)} – {event}"
        self.tag_list.addItem(item_text)

        # Optional: auto-scroll to bottom
        self.tag_list.scrollToBottom()

    def seek_to_tag(self, item):
        row = self.tag_list.row(item)
        ms, _ = self.tags[row]
        if self.player:
            self.player.setPosition(ms)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Space:
            self.toggle_play()
            event.accept()
        elif event.key() == Qt.Key_T:
            self.add_current_tag()
            event.accept()
        elif event.key() == Qt.Key_Left:
            if self.player:
                self.player.setPosition(max(0, self.player.position() - 5000))
            event.accept()
        elif event.key() == Qt.Key_Right:
            if self.player:
                self.player.setPosition(min(self.player.duration(), self.player.position() + 5000))
            event.accept()
        else:
            super().keyPressEvent(event)

app = QApplication(sys.argv)
app.setStyleSheet(load_stylesheet())
window = MainWindow()
window.show()
sys.exit(app.exec())
