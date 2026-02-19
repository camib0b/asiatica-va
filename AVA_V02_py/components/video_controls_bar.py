"""Video transport controls used by the AVA player."""

from qt_compat import QtCore, QtWidgets, Signal
from styles import set_role, set_size, set_state, set_variant


class VideoControlsBar(QtWidgets.QWidget):
    play_requested = Signal()
    pause_requested = Signal()
    seek_requested_ms = Signal(int)
    slower_requested = Signal()
    faster_requested = Signal()
    reset_speed_requested = Signal()
    mute_toggled = Signal(bool)
    fullscreen_toggle_requested = Signal()

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._playback_rate = 1.0
        self._flash_timers: dict[QtWidgets.QPushButton, QtCore.QTimer] = {}
        self._build_ui()
        self._wire_signals()
        self.set_enabled_for_media(False)
        self.set_playing(False)
        self.set_playback_rate(1.0)
        self.set_muted(False)

    def _build_ui(self) -> None:
        layout = QtWidgets.QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)

        self.play_button = QtWidgets.QPushButton("Play", self)
        self.pause_button = QtWidgets.QPushButton("Pause", self)
        self.back_button = QtWidgets.QPushButton("⟵ 2s", self)
        self.forward_button = QtWidgets.QPushButton("2s ⟶", self)
        self.slower_button = QtWidgets.QPushButton("Slower", self)
        self.reset_speed_button = QtWidgets.QPushButton("Reset 1.0x", self)
        self.faster_button = QtWidgets.QPushButton("Faster", self)
        self.mute_button = QtWidgets.QPushButton("Mute", self)
        self.fullscreen_button = QtWidgets.QPushButton("Fullscreen", self)
        self.mute_button.setCheckable(True)
        self.fullscreen_button.setCheckable(True)

        for button in (
            self.play_button,
            self.pause_button,
            self.back_button,
            self.forward_button,
            self.slower_button,
            self.reset_speed_button,
            self.faster_button,
            self.mute_button,
            self.fullscreen_button,
        ):
            set_size(button, "md")
            button.setFocusPolicy(QtCore.Qt.FocusPolicy.NoFocus)

        set_variant(self.play_button, "primary")
        set_variant(self.pause_button, "secondary")
        set_variant(self.back_button, "outline")
        set_variant(self.forward_button, "outline")
        set_variant(self.slower_button, "ghost")
        set_variant(self.reset_speed_button, "ghost")
        set_variant(self.faster_button, "ghost")
        set_variant(self.mute_button, "outline")
        set_variant(self.fullscreen_button, "outline")

        self.speed_label = QtWidgets.QLabel(self)
        set_role(self.speed_label, "muted")

        layout.addWidget(self.play_button)
        layout.addWidget(self.pause_button)
        layout.addSpacing(8)
        layout.addWidget(self.back_button)
        layout.addWidget(self.forward_button)
        layout.addSpacing(8)
        layout.addWidget(self.slower_button)
        layout.addWidget(self.reset_speed_button)
        layout.addWidget(self.faster_button)
        layout.addStretch(1)
        layout.addWidget(self.speed_label)
        layout.addSpacing(8)
        layout.addWidget(self.mute_button)
        layout.addWidget(self.fullscreen_button)

        self.play_button.setToolTip("space  Play")
        self.pause_button.setToolTip("space  Pause")
        self.back_button.setToolTip("⟵  Back")
        self.forward_button.setToolTip("⟶  Forward")
        self.slower_button.setToolTip("{  Slower")
        self.faster_button.setToolTip("}  Faster")
        self.reset_speed_button.setToolTip("\\  Reset speed")
        self.mute_button.setToolTip("Mute/Unmute audio")
        self.fullscreen_button.setToolTip("F  Fullscreen")
        self.speed_label.setToolTip("{ / }  Change speed, \\ reset")

    def _wire_signals(self) -> None:
        self.play_button.clicked.connect(self.flash_play_button)
        self.play_button.clicked.connect(self.play_requested)

        self.pause_button.clicked.connect(self.flash_pause_button)
        self.pause_button.clicked.connect(self.pause_requested)

        self.back_button.clicked.connect(self.flash_seek_back_button)
        self.back_button.clicked.connect(lambda: self.seek_requested_ms.emit(-2000))

        self.forward_button.clicked.connect(self.flash_seek_forward_button)
        self.forward_button.clicked.connect(lambda: self.seek_requested_ms.emit(2000))

        self.slower_button.clicked.connect(self.flash_slower_button)
        self.slower_button.clicked.connect(self.slower_requested)

        self.faster_button.clicked.connect(self.flash_faster_button)
        self.faster_button.clicked.connect(self.faster_requested)

        self.reset_speed_button.clicked.connect(self.flash_reset_speed_button)
        self.reset_speed_button.clicked.connect(self.reset_speed_requested)

        self.mute_button.clicked.connect(self.flash_mute_button)
        self.mute_button.toggled.connect(self.mute_toggled)

        self.fullscreen_button.clicked.connect(self.flash_fullscreen_button)
        self.fullscreen_button.clicked.connect(self.fullscreen_toggle_requested)

    def set_enabled_for_media(self, enabled: bool) -> None:
        for button in (
            self.play_button,
            self.pause_button,
            self.back_button,
            self.forward_button,
            self.slower_button,
            self.faster_button,
            self.reset_speed_button,
            self.mute_button,
            self.fullscreen_button,
        ):
            button.setEnabled(enabled)

    def set_playing(self, playing: bool) -> None:
        self.play_button.setEnabled(not playing)
        self.pause_button.setEnabled(playing)

    def set_playback_rate(self, rate: float) -> None:
        self._playback_rate = rate
        self.speed_label.setText(f"Speed: {rate:.2f}x")
        set_state(self.slower_button, "speedActive", rate < 1.0)
        set_state(self.faster_button, "speedActive", rate > 1.0)

    def set_muted(self, muted: bool) -> None:
        self.mute_button.blockSignals(True)
        self.mute_button.setChecked(muted)
        self.mute_button.setText("Unmute" if muted else "Mute")
        self.mute_button.blockSignals(False)

    def set_fullscreen(self, fullscreen: bool) -> None:
        self.fullscreen_button.blockSignals(True)
        self.fullscreen_button.setChecked(fullscreen)
        self.fullscreen_button.setText("Exit Fullscreen" if fullscreen else "Fullscreen")
        self.fullscreen_button.blockSignals(False)

    def _flash_button_border(self, button: QtWidgets.QPushButton) -> None:
        if button not in self._flash_timers:
            timer = QtCore.QTimer(button)
            timer.setSingleShot(True)
            timer.timeout.connect(lambda b=button: set_state(b, "flash", False))
            self._flash_timers[button] = timer

        set_state(button, "flash", True)
        self._flash_timers[button].start(150)

    def flash_play_button(self) -> None:
        self._flash_button_border(self.play_button)

    def flash_pause_button(self) -> None:
        self._flash_button_border(self.pause_button)

    def flash_seek_back_button(self) -> None:
        self._flash_button_border(self.back_button)

    def flash_seek_forward_button(self) -> None:
        self._flash_button_border(self.forward_button)

    def flash_slower_button(self) -> None:
        self._flash_button_border(self.slower_button)

    def flash_faster_button(self) -> None:
        self._flash_button_border(self.faster_button)

    def flash_reset_speed_button(self) -> None:
        self._flash_button_border(self.reset_speed_button)

    def flash_mute_button(self) -> None:
        self._flash_button_border(self.mute_button)

    def flash_fullscreen_button(self) -> None:
        self._flash_button_border(self.fullscreen_button)
