"""Pop-up clip review window with navigation across tag clips."""

from __future__ import annotations

from qt_compat import QtCore, QtGui, QtWidgets, QtMultimedia, QtMultimediaWidgets
from styles import set_role, set_variant


class ClipPopupWindow(QtWidgets.QDialog):
    """Review clips around tag timestamps and navigate across tags."""

    PRE_ROLL_MS = 4000
    POST_ROLL_MS = 4000

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Clip Review")
        self.resize(980, 620)

        self._video_path = ""
        self._clips: list[dict[str, object]] = []
        self._current_index = 0
        self._current_clip_start = 0
        self._current_clip_end = 0
        self._duration_ms = 0

        self._build_ui()
        self._wire_signals()

    def _build_ui(self) -> None:
        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        self.title_label = QtWidgets.QLabel("Clip Review", self)
        set_role(self.title_label, "h3")

        self.subtitle_label = QtWidgets.QLabel("", self)
        set_role(self.subtitle_label, "muted")

        root.addWidget(self.title_label)
        root.addWidget(self.subtitle_label)

        self.video_widget = QtMultimediaWidgets.QVideoWidget(self)
        self.video_widget.setMinimumHeight(420)
        root.addWidget(self.video_widget, 1)

        timeline_row = QtWidgets.QHBoxLayout()
        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal, self)
        self.slider.setRange(0, 0)
        self.time_label = QtWidgets.QLabel("00:00.000 / 00:00.000", self)
        set_role(self.time_label, "muted")
        timeline_row.addWidget(self.slider, 1)
        timeline_row.addWidget(self.time_label)
        root.addLayout(timeline_row)

        controls_row = QtWidgets.QHBoxLayout()
        self.prev_button = QtWidgets.QPushButton("Previous Clip", self)
        self.play_pause_button = QtWidgets.QPushButton("Pause", self)
        self.next_button = QtWidgets.QPushButton("Next Clip", self)
        self.fullscreen_button = QtWidgets.QPushButton("Fullscreen", self)
        self.close_button = QtWidgets.QPushButton("Close", self)

        set_variant(self.prev_button, "outline")
        set_variant(self.play_pause_button, "primary")
        set_variant(self.next_button, "outline")
        set_variant(self.fullscreen_button, "ghost")
        set_variant(self.close_button, "ghost")

        self.prev_button.setToolTip("[  Previous clip")
        self.next_button.setToolTip("]  Next clip")
        self.play_pause_button.setToolTip("Space  Play/Pause")
        self.fullscreen_button.setToolTip("F  Fullscreen")
        self.close_button.setToolTip("Esc  Close")

        controls_row.addWidget(self.prev_button)
        controls_row.addWidget(self.play_pause_button)
        controls_row.addWidget(self.next_button)
        controls_row.addStretch(1)
        controls_row.addWidget(self.fullscreen_button)
        controls_row.addWidget(self.close_button)
        root.addLayout(controls_row)

        hint = QtWidgets.QLabel("Right-click any tag to open here. Use [ and ] to move between clips.", self)
        set_role(hint, "muted")
        root.addWidget(hint)

        self.player = QtMultimedia.QMediaPlayer(self)
        self.audio = QtMultimedia.QAudioOutput(self)
        self.player.setAudioOutput(self.audio)
        self.player.setVideoOutput(self.video_widget)

    def _wire_signals(self) -> None:
        self.prev_button.clicked.connect(self.show_previous_clip)
        self.next_button.clicked.connect(self.show_next_clip)
        self.play_pause_button.clicked.connect(self.toggle_play_pause)
        self.fullscreen_button.clicked.connect(self.toggle_fullscreen)
        self.close_button.clicked.connect(self.close)

        self.player.positionChanged.connect(self._on_position_changed)
        self.player.durationChanged.connect(self._on_duration_changed)
        self.player.playbackStateChanged.connect(self._on_playback_state_changed)

        self.slider.sliderMoved.connect(self.player.setPosition)
        self.video_widget.fullScreenChanged.connect(self._on_fullscreen_changed)

    def set_context(self, video_path: str, clips: list[dict[str, object]], current_index: int) -> None:
        self._video_path = video_path
        self._clips = clips
        if not self._clips:
            self.subtitle_label.setText("No clips available")
            return

        if self.player.source().toLocalFile() != self._video_path:
            self.player.setSource(QtCore.QUrl.fromLocalFile(self._video_path))

        current_index = max(0, min(current_index, len(self._clips) - 1))
        self.show_clip_at(current_index)

    def show_clip_at(self, index: int) -> None:
        if not self._clips:
            return
        self._current_index = index % len(self._clips)
        clip = self._clips[self._current_index]

        timestamp_ms = int(clip.get("timestamp_ms", 0))
        display_text = str(clip.get("display_text", "Tag clip"))

        self._current_clip_start = max(0, timestamp_ms - self.PRE_ROLL_MS)
        self._current_clip_end = timestamp_ms + self.POST_ROLL_MS

        self.title_label.setText(f"Clip {self._current_index + 1}/{len(self._clips)}")
        self.subtitle_label.setText(display_text)

        self.player.setPosition(self._current_clip_start)
        self.player.play()

    def show_previous_clip(self) -> None:
        if not self._clips:
            return
        self.show_clip_at((self._current_index - 1) % len(self._clips))

    def show_next_clip(self) -> None:
        if not self._clips:
            return
        self.show_clip_at((self._current_index + 1) % len(self._clips))

    def toggle_play_pause(self) -> None:
        if self.player.playbackState() == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            self.player.play()

    def toggle_fullscreen(self) -> None:
        self.video_widget.setFullScreen(not self.video_widget.isFullScreen())

    def _on_position_changed(self, position: int) -> None:
        if position >= self._current_clip_end and self.player.playbackState() == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
            self.player.setPosition(self._current_clip_end)

        self.slider.blockSignals(True)
        self.slider.setValue(position)
        self.slider.blockSignals(False)
        self.time_label.setText(f"{self._format_ms(position)} / {self._format_ms(self._duration_ms)}")

    def _on_duration_changed(self, duration: int) -> None:
        self._duration_ms = duration
        self.slider.setRange(0, max(0, duration))

    def _on_playback_state_changed(self, state: QtMultimedia.QMediaPlayer.PlaybackState) -> None:
        if state == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState:
            self.play_pause_button.setText("Pause")
        else:
            self.play_pause_button.setText("Play")

    def _on_fullscreen_changed(self, fullscreen: bool) -> None:
        self.fullscreen_button.setText("Exit Fullscreen" if fullscreen else "Fullscreen")

    @staticmethod
    def _format_ms(ms: int) -> str:
        ms = max(0, ms)
        total_seconds, millis = divmod(ms, 1000)
        hours, rem = divmod(total_seconds, 3600)
        minutes, seconds = divmod(rem, 60)
        if hours > 0:
            return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"
        return f"{minutes:02d}:{seconds:02d}.{millis:03d}"

    def keyPressEvent(self, event: QtGui.QKeyEvent) -> None:  # noqa: N802
        if event.key() == QtCore.Qt.Key.Key_BracketLeft:
            self.show_previous_clip()
            event.accept()
            return
        if event.key() == QtCore.Qt.Key.Key_BracketRight:
            self.show_next_clip()
            event.accept()
            return
        if event.key() == QtCore.Qt.Key.Key_Space:
            self.toggle_play_pause()
            event.accept()
            return
        if event.key() == QtCore.Qt.Key.Key_F:
            self.toggle_fullscreen()
            event.accept()
            return
        if event.key() == QtCore.Qt.Key.Key_Escape and self.video_widget.isFullScreen():
            self.video_widget.setFullScreen(False)
            event.accept()
            return
        super().keyPressEvent(event)

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:  # noqa: N802
        self.player.pause()
        if self.video_widget.isFullScreen():
            self.video_widget.setFullScreen(False)
        super().closeEvent(event)
