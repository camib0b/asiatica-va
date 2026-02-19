"""Timeline slider + timestamp label for video scrubbing."""

from qt_compat import QtCore, QtGui, QtWidgets, Signal
from styles import set_role


def _format_ms(ms: int) -> str:
    ms = max(0, ms)
    minutes = ms // 60000
    ms %= 60000
    seconds = ms // 1000
    millis = ms % 1000
    return f"{minutes:02d}:{seconds:02d}.{millis:03d}"


class _ClickSeekSlider(QtWidgets.QSlider):
    def mousePressEvent(self, event: QtGui.QMouseEvent) -> None:  # noqa: N802
        if event.button() == QtCore.Qt.MouseButton.LeftButton:
            pos = event.position().toPoint()
            span = self.width() if self.orientation() == QtCore.Qt.Orientation.Horizontal else self.height()
            raw = pos.x() if self.orientation() == QtCore.Qt.Orientation.Horizontal else pos.y()
            value = QtWidgets.QStyle.sliderValueFromPosition(self.minimum(), self.maximum(), raw, span)
            self.setValue(value)
        super().mousePressEvent(event)


class TimelineBar(QtWidgets.QWidget):
    scrub_started = Signal()
    scrub_seek_to = Signal(int)
    scrub_finished = Signal(int)

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._duration_ms = 0
        self._is_scrubbing = False
        self._enable_live_scrub_seek = True
        self._scrub_clock = QtCore.QElapsedTimer()
        self._build_ui()
        self._wire_signals()
        self.reset()

    def _build_ui(self) -> None:
        layout = QtWidgets.QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        self.slider = _ClickSeekSlider(QtCore.Qt.Orientation.Horizontal, self)
        self.slider.setObjectName("TimelineSlider")
        self.slider.setRange(0, 0)
        self.slider.setSingleStep(50)
        self.slider.setPageStep(1000)
        self.slider.setTracking(False)

        self.label = QtWidgets.QLabel("00:00.000 / 00:00.000", self)
        self.label.setMinimumWidth(160)
        self.label.setAlignment(QtCore.Qt.AlignmentFlag.AlignRight | QtCore.Qt.AlignmentFlag.AlignVCenter)
        set_role(self.label, "muted")

        layout.addWidget(self.slider, 1)
        layout.addWidget(self.label)

    def _wire_signals(self) -> None:
        self.slider.sliderPressed.connect(self._on_scrub_started)
        self.slider.sliderMoved.connect(self._on_scrub_moved)
        self.slider.sliderReleased.connect(self._on_scrub_released)
        self.slider.valueChanged.connect(self._on_value_changed)

    def reset(self) -> None:
        self._duration_ms = 0
        self._is_scrubbing = False
        self.slider.setRange(0, 0)
        self.slider.setValue(0)
        self.slider.setEnabled(False)
        self._update_label(0, 0)

    def set_enabled_for_media(self, on: bool) -> None:
        self.slider.setEnabled(on and self._duration_ms > 0)

    def set_duration_ms(self, duration_ms: int) -> None:
        self._duration_ms = max(0, duration_ms)
        self.slider.setRange(0, self._duration_ms)
        self.slider.setEnabled(self._duration_ms > 0)
        self._update_label(self.slider.value(), self._duration_ms)

    def set_position_ms(self, pos_ms: int) -> None:
        pos_ms = max(0, min(pos_ms, self._duration_ms))
        if not self._is_scrubbing:
            self.slider.setValue(pos_ms)
        self._update_label(pos_ms, self._duration_ms)

    def _on_scrub_started(self) -> None:
        self._is_scrubbing = True
        self.scrub_started.emit()

    def _on_scrub_moved(self, value: int) -> None:
        self._update_label(value, self._duration_ms)
        if not self._enable_live_scrub_seek:
            return
        if not self._scrub_clock.isValid():
            self._scrub_clock.start()
        if self._scrub_clock.elapsed() >= 33:
            self.scrub_seek_to.emit(value)
            self._scrub_clock.restart()

    def _on_scrub_released(self) -> None:
        self._is_scrubbing = False
        self.scrub_finished.emit(self.slider.value())

    def _on_value_changed(self, value: int) -> None:
        if self._is_scrubbing:
            return
        self._update_label(value, self._duration_ms)

    def _update_label(self, pos_ms: int, duration_ms: int) -> None:
        self.label.setText(f"{_format_ms(pos_ms)} / {_format_ms(duration_ms)}")
