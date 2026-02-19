"""Video player wrapper with controls, timeline, and keyboard shortcuts."""

from qt_compat import QtCore, QtGui, QtWidgets, QtMultimedia, QtMultimediaWidgets, Signal
from .timeline_bar import TimelineBar
from .video_controls_bar import VideoControlsBar


class VideoPlayer(QtWidgets.QWidget):
    video_closed = Signal()
    position_changed_ms = Signal(int)

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._duration_ms = 0
        self._playback_rate = 1.0
        self._was_playing_before_scrub = False
        self._build_ui()
        self._wire_signals()
        self._build_shortcuts()
        self.set_controls_enabled(False)

    def _build_ui(self) -> None:
        self._video_widget = QtMultimediaWidgets.QVideoWidget(self)
        self._video_widget.setMinimumHeight(360)
        self._video_widget.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Expanding,
            QtWidgets.QSizePolicy.Policy.Expanding,
        )
        self._video_widget.setAttribute(QtCore.Qt.WidgetAttribute.WA_Hover, True)

        self._controls_bar = VideoControlsBar(self)
        self._timeline_bar = TimelineBar(self)

        self._player = QtMultimedia.QMediaPlayer(self)
        self._audio_output = QtMultimedia.QAudioOutput(self)
        self._player.setAudioOutput(self._audio_output)
        self._player.setVideoOutput(self._video_widget)

        self.set_controls_visible(False)
        self._video_widget.installEventFilter(self)

    def _wire_signals(self) -> None:
        self._controls_bar.play_requested.connect(self._on_play_clicked)
        self._controls_bar.pause_requested.connect(self._on_pause_clicked)
        self._controls_bar.slower_requested.connect(self._on_slower_clicked)
        self._controls_bar.faster_requested.connect(self._on_faster_clicked)
        self._controls_bar.reset_speed_requested.connect(self._on_reset_speed_clicked)
        self._controls_bar.mute_toggled.connect(self._on_mute_toggled)
        self._controls_bar.seek_requested_ms.connect(self._seek_by_ms)

        self._player.playbackStateChanged.connect(self._on_state_changed)
        self._player.durationChanged.connect(self._on_duration_changed)
        self._player.positionChanged.connect(self._on_position_changed)

        self._timeline_bar.scrub_started.connect(self._on_scrub_started)
        self._timeline_bar.scrub_seek_to.connect(self._player.setPosition)
        self._timeline_bar.scrub_finished.connect(self._on_scrub_finished)

    def _build_shortcuts(self) -> None:
        def make_action(seq: QtGui.QKeySequence, handler):
            action = QtGui.QAction(self)
            action.setShortcut(seq)
            action.setShortcutContext(QtCore.Qt.ShortcutContext.ApplicationShortcut)
            action.triggered.connect(handler)
            self.addAction(action)
            return action

        self._slower_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_BraceLeft), self._shortcut_slow)
        self._faster_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_BraceRight), self._shortcut_fast)
        self._reset_speed_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_Backslash), self._shortcut_reset)
        self._toggle_play_pause_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_Space), self._shortcut_toggle)
        self._seek_small_back_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_Left), lambda: self._seek_by_ms(-250))
        self._seek_small_forward_action = make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_Right), lambda: self._seek_by_ms(250))
        self._seek_big_back_action = make_action(
            QtGui.QKeySequence(QtCore.Qt.KeyboardModifier.ShiftModifier | QtCore.Qt.Key.Key_Left),
            lambda: self._seek_by_ms(-3000),
        )
        self._seek_big_forward_action = make_action(
            QtGui.QKeySequence(QtCore.Qt.KeyboardModifier.ShiftModifier | QtCore.Qt.Key.Key_Right),
            lambda: self._seek_by_ms(3000),
        )

    def set_controls_visible(self, visible: bool) -> None:
        self._video_widget.setVisible(visible)
        self._controls_bar.setVisible(visible)
        self._timeline_bar.setVisible(visible)

    def set_controls_enabled(self, enabled: bool) -> None:
        self._controls_bar.set_enabled_for_media(enabled)
        self._video_widget.setEnabled(enabled)
        self._timeline_bar.setEnabled(enabled)
        self._timeline_bar.set_enabled_for_media(enabled)

        for action in (
            self._slower_action,
            self._faster_action,
            self._reset_speed_action,
            self._toggle_play_pause_action,
            self._seek_small_back_action,
            self._seek_small_forward_action,
            self._seek_big_back_action,
            self._seek_big_forward_action,
        ):
            action.setEnabled(enabled)

    def video_widget(self) -> QtWidgets.QWidget:
        return self._video_widget

    def controls_bar(self) -> VideoControlsBar:
        return self._controls_bar

    def timeline_bar(self) -> TimelineBar:
        return self._timeline_bar

    def current_position_ms(self) -> int:
        return self._player.position()

    def seek_to_ms(self, pos_ms: int) -> None:
        dur = self._player.duration()
        target = max(0, min(pos_ms, dur)) if dur > 0 else max(0, pos_ms)
        self._player.setPosition(target)

    def load_video_from_file(self, file_path: str) -> None:
        if not file_path:
            return
        self.set_controls_visible(True)
        self._controls_bar.set_enabled_for_media(False)
        self._duration_ms = 0
        self._was_playing_before_scrub = False
        self._timeline_bar.reset()

        self._audio_output.setMuted(False)
        self._controls_bar.set_muted(False)
        self._playback_rate = 1.0
        self._player.setPlaybackRate(self._playback_rate)
        self._controls_bar.set_playback_rate(self._playback_rate)

        self._player.stop()
        self._player.setSource(QtCore.QUrl.fromLocalFile(file_path))
        self.set_controls_enabled(True)
        self._player.setPosition(0)
        self._player.play()

    def _seek_by_ms(self, delta_ms: int) -> None:
        dur = self._player.duration()
        pos = self._player.position()
        next_pos = pos + delta_ms
        target = max(0, min(next_pos, dur)) if dur > 0 else max(0, next_pos)
        self._player.setPosition(target)

    def _on_play_clicked(self) -> None:
        self._player.play()

    def _on_pause_clicked(self) -> None:
        self._player.pause()

    def _on_slower_clicked(self) -> None:
        self._playback_rate = max(0.25, self._playback_rate - 0.25)
        self._player.setPlaybackRate(self._playback_rate)
        self._controls_bar.set_playback_rate(self._playback_rate)

    def _on_reset_speed_clicked(self) -> None:
        self._playback_rate = 1.0
        self._player.setPlaybackRate(self._playback_rate)
        self._controls_bar.set_playback_rate(self._playback_rate)

    def _on_faster_clicked(self) -> None:
        self._playback_rate = min(4.0, self._playback_rate + 0.25)
        self._player.setPlaybackRate(self._playback_rate)
        self._controls_bar.set_playback_rate(self._playback_rate)

    def _on_mute_toggled(self, muted: bool) -> None:
        self._audio_output.setMuted(muted)
        self._controls_bar.set_muted(muted)

    def _on_state_changed(self, state: QtMultimedia.QMediaPlayer.PlaybackState) -> None:
        self._controls_bar.set_playing(state == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState)

    def _on_duration_changed(self, duration: int) -> None:
        self._duration_ms = duration
        self._timeline_bar.set_duration_ms(duration)

    def _on_position_changed(self, position: int) -> None:
        self._timeline_bar.set_position_ms(position)
        self.position_changed_ms.emit(position)

    def _on_scrub_started(self) -> None:
        self._was_playing_before_scrub = (
            self._player.playbackState() == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState
        )
        if self._was_playing_before_scrub:
            self._player.pause()

    def _on_scrub_finished(self, pos_ms: int) -> None:
        self._player.setPosition(pos_ms)
        if self._was_playing_before_scrub:
            self._player.play()
        self._was_playing_before_scrub = False

    def _shortcut_slow(self) -> None:
        self._controls_bar.flash_slower_button()
        self._on_slower_clicked()

    def _shortcut_fast(self) -> None:
        self._controls_bar.flash_faster_button()
        self._on_faster_clicked()

    def _shortcut_reset(self) -> None:
        self._controls_bar.flash_reset_speed_button()
        self._on_reset_speed_clicked()

    def _shortcut_toggle(self) -> None:
        state = self._player.playbackState()
        if state == QtMultimedia.QMediaPlayer.PlaybackState.PlayingState:
            self._controls_bar.flash_pause_button()
            self._player.pause()
        else:
            self._controls_bar.flash_play_button()
            self._player.play()

    def eventFilter(self, obj, event):  # noqa: N802, ANN001
        if obj == self._video_widget and event.type() == QtCore.QEvent.Type.MouseButtonPress:
            if event.button() == QtCore.Qt.MouseButton.LeftButton:
                self._shortcut_toggle()
                return True
        return super().eventFilter(obj, event)
