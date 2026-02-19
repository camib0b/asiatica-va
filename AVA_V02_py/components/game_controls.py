"""Main/follow-up game event controls used for tagging."""

from qt_compat import QtCore, QtGui, QtWidgets, Signal
from styles import set_size, set_state, set_variant


class GameControls(QtWidgets.QWidget):
    main_event_pressed = Signal(str)
    game_event_marked = Signal(str, str)

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumWidth(300)
        self.current_main_event = ""
        self.current_first_follow_up = ""
        self.follow_up_stage = "none"
        self.active_main_button: QtWidgets.QPushButton | None = None
        self.follow_up_buttons: list[QtWidgets.QPushButton] = []
        self._flash_timers: dict[QtWidgets.QPushButton, QtCore.QTimer] = {}
        self._build_ui()
        self._wire_signals()
        self._build_shortcuts()
        self.hide_follow_up_buttons()

    def _build_ui(self) -> None:
        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(12)

        main_grid_widget = QtWidgets.QWidget(self)
        self.main_grid_layout = QtWidgets.QGridLayout(main_grid_widget)
        self.main_grid_layout.setContentsMargins(0, 0, 0, 0)
        self.main_grid_layout.setSpacing(4)

        self.hit16yd_button = QtWidgets.QPushButton("16-yd play", main_grid_widget)
        self.hit50yd_button = QtWidgets.QPushButton("50-yd play", main_grid_widget)
        self.hit75yd_button = QtWidgets.QPushButton("75-yd play", main_grid_widget)
        self.enter_d_button = QtWidgets.QPushButton("Circle Entry", main_grid_widget)
        self.shot_button = QtWidgets.QPushButton("Shot", main_grid_widget)
        self.goal_button = QtWidgets.QPushButton("Goal", main_grid_widget)
        self.pc_button = QtWidgets.QPushButton("PC", main_grid_widget)
        self.ps_button = QtWidgets.QPushButton("PS", main_grid_widget)
        self.card_button = QtWidgets.QPushButton("Card", main_grid_widget)

        self.main_buttons = [
            self.hit16yd_button,
            self.hit50yd_button,
            self.hit75yd_button,
            self.enter_d_button,
            self.shot_button,
            self.goal_button,
            self.pc_button,
            self.ps_button,
            self.card_button,
        ]

        for button in self.main_buttons:
            set_size(button, "md")
            set_variant(button, "gameControl")
            button.setFocusPolicy(QtCore.Qt.FocusPolicy.StrongFocus)
            button.setMinimumHeight(40)

        self.main_grid_layout.addWidget(self.hit16yd_button, 0, 0)
        self.main_grid_layout.addWidget(self.hit50yd_button, 0, 1)
        self.main_grid_layout.addWidget(self.hit75yd_button, 0, 2)
        self.main_grid_layout.addWidget(self.enter_d_button, 1, 0)
        self.main_grid_layout.addWidget(self.shot_button, 1, 1)
        self.main_grid_layout.addWidget(self.goal_button, 1, 2)
        self.main_grid_layout.addWidget(self.pc_button, 2, 0)
        self.main_grid_layout.addWidget(self.ps_button, 2, 1)
        self.main_grid_layout.addWidget(self.card_button, 2, 2)

        self.follow_up_container = QtWidgets.QWidget(self)
        self.follow_up_layout = QtWidgets.QHBoxLayout(self.follow_up_container)
        self.follow_up_layout.setContentsMargins(0, 0, 0, 0)
        self.follow_up_layout.setSpacing(8)

        root.addWidget(main_grid_widget)
        root.addWidget(self.follow_up_container)
        root.addStretch(1)

    def _wire_signals(self) -> None:
        for button in self.main_buttons:
            button.clicked.connect(lambda _=False, b=button: self._on_main_button_clicked(b))

    def _build_shortcuts(self) -> None:
        def make_action(key_sequence: QtGui.QKeySequence, handler):
            action = QtGui.QAction(self)
            action.setShortcut(key_sequence)
            action.setShortcutContext(QtCore.Qt.ShortcutContext.ApplicationShortcut)
            action.triggered.connect(handler)
            self.addAction(action)
            return action

        mapping = {
            QtCore.Qt.Key.Key_Q: self.hit16yd_button,
            QtCore.Qt.Key.Key_W: self.hit50yd_button,
            QtCore.Qt.Key.Key_E: self.hit75yd_button,
            QtCore.Qt.Key.Key_A: self.enter_d_button,
            QtCore.Qt.Key.Key_S: self.shot_button,
            QtCore.Qt.Key.Key_D: self.goal_button,
            QtCore.Qt.Key.Key_Z: self.pc_button,
            QtCore.Qt.Key.Key_X: self.ps_button,
            QtCore.Qt.Key.Key_C: self.card_button,
        }

        for key, button in mapping.items():
            make_action(QtGui.QKeySequence(key), lambda b=button: self._click_if_available(b))

        for idx in range(1, 10):
            key = getattr(QtCore.Qt.Key, f"Key_{idx}")
            make_action(QtGui.QKeySequence(key), lambda i=idx - 1: self._trigger_follow_up_index(i))

        make_action(QtGui.QKeySequence(QtCore.Qt.Key.Key_Escape), self._cancel_follow_up)

    def _click_if_available(self, button: QtWidgets.QPushButton) -> None:
        if button.isVisible() and button.isEnabled():
            button.click()

    def _trigger_follow_up_index(self, index: int) -> None:
        if not self.follow_up_container.isVisible():
            return
        if index < 0 or index >= len(self.follow_up_buttons):
            return
        button = self.follow_up_buttons[index]
        if button.isVisible() and button.isEnabled():
            button.click()

    def _cancel_follow_up(self) -> None:
        if self.follow_up_stage == "none" or not self.current_main_event:
            return
        self.game_event_marked.emit(self.current_main_event, "")
        self.clear_active_main_button()
        self.hide_follow_up_buttons()
        self.current_main_event = ""
        self.current_first_follow_up = ""
        self.follow_up_stage = "none"

    def _set_active_main_button(self, button: QtWidgets.QPushButton | None) -> None:
        if self.active_main_button is button:
            return
        if self.active_main_button is not None:
            set_state(self.active_main_button, "activeMain", False)
        self.active_main_button = button
        if self.active_main_button is not None:
            set_state(self.active_main_button, "activeMain", True)

    def clear_active_main_button(self) -> None:
        if self.active_main_button is not None:
            set_state(self.active_main_button, "activeMain", False)
        self.active_main_button = None

    def _on_main_button_clicked(self, button: QtWidgets.QPushButton) -> None:
        self._flash_button_border(button)
        event_name = button.text()
        self.current_main_event = event_name
        self.current_first_follow_up = ""
        self.follow_up_stage = "none"
        self._set_active_main_button(button)
        self.main_event_pressed.emit(event_name)
        self.show_first_level_follow_ups(event_name)

    def _on_follow_up_button_clicked(self, button: QtWidgets.QPushButton) -> None:
        self._flash_button_border(button)
        follow_up_name = button.text()

        if self.follow_up_stage == "first":
            self.current_first_follow_up = follow_up_name
            second = self.get_second_level_follow_ups(self.current_main_event, self.current_first_follow_up)
            if not second:
                self.game_event_marked.emit(self.current_main_event, self.current_first_follow_up)
                self.clear_active_main_button()
                self.hide_follow_up_buttons()
                self.current_main_event = ""
                self.current_first_follow_up = ""
                self.follow_up_stage = "none"
                return

            self.show_second_level_follow_ups(self.current_main_event, self.current_first_follow_up)
            return

        if self.follow_up_stage == "second":
            combined = (
                follow_up_name
                if not self.current_first_follow_up
                else f"{self.current_first_follow_up} → {follow_up_name}"
            )
            self.game_event_marked.emit(self.current_main_event, combined)
            self.clear_active_main_button()
            self.hide_follow_up_buttons()
            self.current_main_event = ""
            self.current_first_follow_up = ""
            self.follow_up_stage = "none"
            return

        self.game_event_marked.emit(self.current_main_event, follow_up_name)

    def get_first_level_follow_ups(self, main_event: str) -> list[str]:
        if main_event == "Shot":
            return ["On target", "Off target"]
        if main_event == "Circle Entry":
            return ["Dribling", "Pass", "Deflection"]
        if main_event == "PC":
            return ["Direct shot", "Variant", "Ruined"]
        if main_event == "75-yd play":
            return ["Forward", "Sideways", "Back"]
        if main_event == "Card":
            return ["Green", "Yellow", "Red"]
        return []

    def get_second_level_follow_ups(self, main_event: str, first_follow_up: str) -> list[str]:
        if main_event == "Shot":
            if first_follow_up == "On target":
                return ["Goal", "Saved", "Post"]
            if first_follow_up == "Off target":
                return ["Closeby", "Not close"]
            return []

        if main_event == "PC" and first_follow_up == "Direct shot":
            return ["Hit", "Swept", "Dragflick"]

        if main_event == "Circle Entry":
            return ["Left", "Middle", "Right"]

        return []

    def show_first_level_follow_ups(self, main_event: str) -> None:
        self.hide_follow_up_buttons()
        actions = self.get_first_level_follow_ups(main_event)
        if not actions:
            self.game_event_marked.emit(main_event, "")
            self.clear_active_main_button()
            self.current_main_event = ""
            self.current_first_follow_up = ""
            self.follow_up_stage = "none"
            return

        for action in actions:
            button = QtWidgets.QPushButton(action, self.follow_up_container)
            set_size(button, "md")
            set_variant(button, "gameControlFollowUp")
            button.setFocusPolicy(QtCore.Qt.FocusPolicy.StrongFocus)
            button.setMinimumHeight(40)
            button.clicked.connect(lambda _=False, b=button: self._on_follow_up_button_clicked(b))
            self.follow_up_layout.addWidget(button)
            self.follow_up_buttons.append(button)

        self.follow_up_stage = "first"
        self.follow_up_container.setVisible(True)

    def show_second_level_follow_ups(self, main_event: str, first_follow_up: str) -> None:
        _ = main_event
        self.hide_follow_up_buttons()
        actions = self.get_second_level_follow_ups(self.current_main_event, first_follow_up)
        if not actions:
            self.game_event_marked.emit(self.current_main_event, first_follow_up)
            self.clear_active_main_button()
            self.current_main_event = ""
            self.current_first_follow_up = ""
            self.follow_up_stage = "none"
            return

        for action in actions:
            button = QtWidgets.QPushButton(action, self.follow_up_container)
            set_size(button, "md")
            set_variant(button, "gameControlFollowUp")
            button.setFocusPolicy(QtCore.Qt.FocusPolicy.StrongFocus)
            button.setMinimumHeight(40)
            button.clicked.connect(lambda _=False, b=button: self._on_follow_up_button_clicked(b))
            self.follow_up_layout.addWidget(button)
            self.follow_up_buttons.append(button)

        self.follow_up_stage = "second"
        self.follow_up_container.setVisible(True)

    def hide_follow_up_buttons(self) -> None:
        while self.follow_up_layout.count():
            item = self.follow_up_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()
        self.follow_up_buttons.clear()
        self.follow_up_container.setVisible(False)

    def _flash_button_border(self, button: QtWidgets.QPushButton) -> None:
        if button not in self._flash_timers:
            timer = QtCore.QTimer(button)
            timer.setSingleShot(True)
            timer.timeout.connect(lambda b=button: set_state(b, "flash", False))
            self._flash_timers[button] = timer

        set_state(button, "flash", True)
        self._flash_timers[button].start(150)
