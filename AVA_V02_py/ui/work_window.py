"""Primary AVA work window for tagging and analysis."""

from __future__ import annotations

from qt_compat import QtCore, QtGui, QtWidgets, Signal
from components.game_controls import GameControls
from components.video_player import VideoPlayer
from state.tag_session import GameTag, TagSession
from styles import set_role, set_size, set_variant
from .game_setup_window import GameSetupWindow


def _format_timestamp_ms(ms: int) -> str:
    ms = max(0, ms)
    total_seconds, millis = divmod(ms, 1000)
    hours, rem = divmod(total_seconds, 3600)
    minutes, seconds = divmod(rem, 60)
    if hours > 0:
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"
    return f"{minutes:02d}:{seconds:02d}.{millis:03d}"


class _StatsPlaceholder(QtWidgets.QWidget):
    filter_by_path_requested = Signal(str, str)

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)
        self.label = QtWidgets.QLabel("Stats window is in migration...", self)
        set_role(self.label, "muted")
        layout.addWidget(self.label)

    def set_tag_session(self, session: TagSession | None) -> None:  # noqa: ARG002
        return


class WorkWindow(QtWidgets.QWidget):
    video_closed = Signal()

    class Mode:
        TAGGING = "tagging"
        ANALYZING = "analyzing"

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._mode = self.Mode.TAGGING
        self._tag_session: TagSession | None = None
        self._filter_actions: dict[str, QtGui.QAction] = {}
        self._active_filter_path_main_event = ""
        self._active_filter_path_follow_up = ""
        self._pending_note_index = -1
        self._pending_note_text = ""

        self._pending_main_event = ""
        self._pending_timestamp_ms = 0
        self._has_pending_tag = False

        self.context_period = ""
        self.context_team = ""
        self.context_situation = ""

        self._new_tag_flash_row = -1
        self._new_tag_flash_timer: QtCore.QTimer | None = None

        self._build_ui()
        self._wire_signals()
        self.apply_tagging_layout()

    def mode(self) -> str:
        return self._mode

    def set_tag_session(self, session: TagSession | None) -> None:
        if self._tag_session is session:
            return
        self._tag_session = session
        self.stats_window.set_tag_session(self._tag_session)
        self._rebuild_filter_menu()
        self._rebuild_tags_list()

        if self._tag_session is None:
            return

        self._tag_session.cleared.connect(self._on_session_cleared)
        self._tag_session.stats_changed.connect(self._on_session_stats_changed)
        self._tag_session.tag_added.connect(self._on_session_tag_added)
        self._tag_session.tag_note_changed.connect(lambda _idx: self._load_note_for_selected_tag())

    def set_mode(self, mode: str) -> None:
        if self._mode == mode:
            return
        self._mode = mode
        if mode == self.Mode.TAGGING:
            self.apply_tagging_layout()
        else:
            self.apply_analyzing_layout()

        self.mode_tagging_btn.setChecked(mode == self.Mode.TAGGING)
        self.mode_analyzing_btn.setChecked(mode == self.Mode.ANALYZING)

    def _build_ui(self) -> None:
        self.setObjectName("AppRoot")
        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        self.content_stack = QtWidgets.QStackedWidget(self)

        self.main_content_container = QtWidgets.QWidget(self)
        main_layout = QtWidgets.QVBoxLayout(self.main_content_container)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(8)

        top_row = QtWidgets.QWidget(self.main_content_container)
        top_layout = QtWidgets.QHBoxLayout(top_row)
        top_layout.setContentsMargins(0, 0, 0, 0)
        top_layout.setSpacing(12)

        self.mode_tagging_btn = QtWidgets.QToolButton(top_row)
        self.mode_tagging_btn.setText("Tagging")
        self.mode_tagging_btn.setCheckable(True)
        self.mode_tagging_btn.setChecked(True)
        set_variant(self.mode_tagging_btn, "ghost")
        set_size(self.mode_tagging_btn, "sm")

        self.mode_analyzing_btn = QtWidgets.QToolButton(top_row)
        self.mode_analyzing_btn.setText("Analyzing")
        self.mode_analyzing_btn.setCheckable(True)
        set_variant(self.mode_analyzing_btn, "ghost")
        set_size(self.mode_analyzing_btn, "sm")

        top_layout.addWidget(self.mode_tagging_btn)
        top_layout.addWidget(self.mode_analyzing_btn)
        top_layout.addSpacing(16)

        self.video_player = VideoPlayer(self)
        self.video_controls_row = QtWidgets.QWidget(self)
        video_controls_layout = QtWidgets.QHBoxLayout(self.video_controls_row)
        video_controls_layout.setContentsMargins(0, 0, 0, 0)
        video_controls_layout.setSpacing(8)
        video_controls_layout.addWidget(self.video_player.controls_bar(), 1)

        self.video_menu_button = QtWidgets.QToolButton(self)
        self.video_menu_button.setText("⚙")
        self.video_menu_button.setToolTip("Video Manager")
        self.video_menu_button.setMinimumWidth(36)
        set_variant(self.video_menu_button, "ghost")
        set_size(self.video_menu_button, "sm")
        self.video_menu_button.setPopupMode(QtWidgets.QToolButton.ToolButtonPopupMode.InstantPopup)

        self.video_menu = QtWidgets.QMenu(self.video_menu_button)
        self.replace_video_action = self.video_menu.addAction("Replace video with another one")
        self.discard_video_action = self.video_menu.addAction("Close current video")
        self.video_menu_button.setMenu(self.video_menu)
        video_controls_layout.addWidget(self.video_menu_button, 0, QtCore.Qt.AlignmentFlag.AlignRight)
        top_layout.addWidget(self.video_controls_row, 1)

        main_layout.addWidget(top_row)

        self.video_timeline_row = QtWidgets.QWidget(self.main_content_container)
        timeline_layout = QtWidgets.QHBoxLayout(self.video_timeline_row)
        timeline_layout.setContentsMargins(0, 0, 0, 0)
        timeline_layout.addWidget(self.video_player.timeline_bar())
        main_layout.addWidget(self.video_timeline_row)

        self.content_area = QtWidgets.QWidget(self.main_content_container)
        self.content_layout = QtWidgets.QVBoxLayout(self.content_area)
        self.content_layout.setContentsMargins(0, 0, 0, 0)
        self.content_layout.setSpacing(8)
        main_layout.addWidget(self.content_area, 1)

        self.game_setup_widget = GameSetupWindow(self)
        self.content_stack.addWidget(self.game_setup_widget)
        self.content_stack.addWidget(self.main_content_container)
        self.content_stack.setCurrentIndex(0)
        root.addWidget(self.content_stack)

        self.tagging_main_row = QtWidgets.QWidget(self)
        tagging_main_layout = QtWidgets.QHBoxLayout(self.tagging_main_row)
        tagging_main_layout.setContentsMargins(0, 0, 0, 0)
        tagging_main_layout.setSpacing(12)

        self.tagging_video_col = QtWidgets.QWidget(self)
        self.tagging_video_col.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Expanding,
            QtWidgets.QSizePolicy.Policy.Expanding,
        )
        self.tagging_video_layout = QtWidgets.QVBoxLayout(self.tagging_video_col)
        self.tagging_video_layout.setContentsMargins(0, 0, 0, 0)
        tagging_main_layout.addWidget(self.tagging_video_col, 3)

        self.tagging_right_col = QtWidgets.QWidget(self)
        self.tagging_right_col.setObjectName("TaggingRightCol")
        self.tagging_right_layout = QtWidgets.QVBoxLayout(self.tagging_right_col)
        self.tagging_right_layout.setContentsMargins(0, 0, 0, 0)
        tagging_main_layout.addWidget(self.tagging_right_col, 0)

        self.tags_section = QtWidgets.QWidget(self)
        tags_section_layout = QtWidgets.QVBoxLayout(self.tags_section)
        tags_section_layout.setContentsMargins(0, 0, 0, 0)
        tags_section_layout.setSpacing(6)

        tags_header_row = QtWidgets.QWidget(self.tags_section)
        tags_header_layout = QtWidgets.QHBoxLayout(tags_header_row)
        tags_header_layout.setContentsMargins(0, 0, 0, 0)
        tags_header_layout.setSpacing(8)

        self.tags_header_label = QtWidgets.QLabel("Tags", tags_header_row)
        set_role(self.tags_header_label, "h3")

        self.tags_filter_button = QtWidgets.QToolButton(tags_header_row)
        self.tags_filter_button.setText("Filter")
        set_variant(self.tags_filter_button, "ghost")
        set_size(self.tags_filter_button, "sm")
        self.tags_filter_button.setPopupMode(QtWidgets.QToolButton.ToolButtonPopupMode.InstantPopup)

        self.tags_remove_filters_button = QtWidgets.QToolButton(tags_header_row)
        self.tags_remove_filters_button.setText("Remove filters")
        set_variant(self.tags_remove_filters_button, "ghost")
        set_size(self.tags_remove_filters_button, "sm")
        self.tags_remove_filters_button.hide()

        self.tags_filter_menu = QtWidgets.QMenu(self.tags_filter_button)
        self.tags_filter_button.setMenu(self.tags_filter_menu)

        self.tags_filter_indicator = QtWidgets.QLabel(tags_header_row)
        set_role(self.tags_filter_indicator, "muted")
        self.tags_filter_indicator.hide()

        self.undo_last_tag_button = QtWidgets.QToolButton(tags_header_row)
        self.undo_last_tag_button.setText("Undo")
        self.undo_last_tag_button.setToolTip("Ctrl+Z  Remove most recent tag")
        set_variant(self.undo_last_tag_button, "ghost")
        set_size(self.undo_last_tag_button, "sm")

        tags_header_layout.addWidget(self.tags_header_label)
        tags_header_layout.addStretch(1)
        tags_header_layout.addWidget(self.tags_filter_indicator)
        tags_header_layout.addWidget(self.undo_last_tag_button)
        tags_header_layout.addWidget(self.tags_remove_filters_button)
        tags_header_layout.addWidget(self.tags_filter_button)

        self.tags_list = QtWidgets.QListWidget(self.tags_section)
        self.tags_list.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Expanding,
            QtWidgets.QSizePolicy.Policy.Fixed,
        )

        tags_section_layout.addWidget(tags_header_row)
        tags_section_layout.addWidget(self.tags_list)

        self.game_controls = GameControls(self)
        self.stats_window = _StatsPlaceholder(self)
        self.stats_window.setMinimumHeight(180)

        self.notes_edit = QtWidgets.QPlainTextEdit(self)
        self.notes_edit.setPlaceholderText("Note for selected tag…")
        self.notes_edit.setMaximumHeight(120)
        set_role(self.notes_edit, "muted")

        self.analyzing_main_row = QtWidgets.QWidget(self)
        analyzing_main_layout = QtWidgets.QHBoxLayout(self.analyzing_main_row)
        analyzing_main_layout.setContentsMargins(0, 0, 0, 0)
        analyzing_main_layout.setSpacing(12)

        self.analyzing_left_col = QtWidgets.QWidget(self)
        self.analyzing_left_layout = QtWidgets.QVBoxLayout(self.analyzing_left_col)
        self.analyzing_left_layout.setContentsMargins(0, 0, 0, 0)
        self.analyzing_left_layout.setSpacing(8)

        self.analyzing_right_col = QtWidgets.QWidget(self)
        self.analyzing_right_layout = QtWidgets.QVBoxLayout(self.analyzing_right_col)
        self.analyzing_right_layout.setContentsMargins(0, 0, 0, 0)
        self.analyzing_right_layout.setSpacing(8)

        analyzing_main_layout.addWidget(self.analyzing_left_col, 1)
        analyzing_main_layout.addWidget(self.analyzing_right_col, 1)

        self.video_player.set_controls_visible(False)
        self.game_controls.hide()
        self.stats_window.hide()
        self.tags_header_label.hide()
        self.tags_filter_button.hide()
        self.tags_remove_filters_button.hide()
        self.undo_last_tag_button.hide()
        self.tags_list.hide()
        self.mode_tagging_btn.hide()
        self.mode_analyzing_btn.hide()

        self.note_debounce_timer = QtCore.QTimer(self)
        self.note_debounce_timer.setSingleShot(True)

    def apply_tagging_layout(self) -> None:
        self._mode = self.Mode.TAGGING
        self.video_player.controls_bar().setObjectName("VideoControlsBarSlim")

        self._move_widget(self.video_player.video_widget(), self.tagging_video_layout, stretch=1)
        self._move_widget(self.game_controls, self.tagging_right_layout)

        self._clear_layout(self.content_layout)
        self.content_layout.addWidget(self.tagging_main_row, 1)
        self._move_widget(self.tags_section, self.content_layout)

        rh = self.tags_list.sizeHintForRow(0)
        rh = rh if rh > 0 else 24
        self.tags_list.setMaximumHeight(rh * 3 + 8)
        self.stats_window.hide()
        self.notes_edit.hide()
        self._rebuild_tags_list()

    def apply_analyzing_layout(self) -> None:
        self._mode = self.Mode.ANALYZING
        self.video_player.controls_bar().setObjectName("")

        self._clear_layout(self.content_layout)

        self._move_widget(self.video_player.video_widget(), self.analyzing_left_layout, stretch=1)
        self._move_widget(self.tags_section, self.analyzing_left_layout, stretch=1)
        self._move_widget(self.game_controls, self.analyzing_left_layout)
        self._move_widget(self.stats_window, self.analyzing_right_layout, stretch=1)
        self._move_widget(self.notes_edit, self.analyzing_right_layout)

        self.content_layout.addWidget(self.analyzing_main_row, 1)

        self.stats_window.show()
        self.notes_edit.show()
        self.tags_list.setMaximumHeight(16777215)
        self._rebuild_tags_list()

    def _wire_signals(self) -> None:
        self.game_setup_widget.team_setup_confirmed.connect(self._on_team_setup_confirmed)
        self.game_setup_widget.cancelled.connect(self._on_team_setup_cancelled)

        self.replace_video_action.triggered.connect(self.on_replace_video)
        self.discard_video_action.triggered.connect(self.on_discard_video)

        self.video_player.video_closed.connect(self.video_closed)

        self.game_controls.main_event_pressed.connect(self._on_main_event_pressed)
        self.game_controls.game_event_marked.connect(self._on_game_event_marked)

        self.tags_list.itemActivated.connect(self.on_tag_item_activated)
        self.tags_list.currentItemChanged.connect(lambda *_: self.on_tag_selection_changed())

        self.mode_tagging_btn.clicked.connect(self.on_mode_toggled)
        self.mode_analyzing_btn.clicked.connect(self.on_mode_toggled)

        self.notes_edit.textChanged.connect(self.on_note_text_changed)

        self.tags_remove_filters_button.clicked.connect(self.on_remove_filters)
        self.undo_last_tag_button.clicked.connect(self.on_undo_last_tag)

        mode_action = QtGui.QAction(self)
        mode_action.setShortcut(QtGui.QKeySequence(QtCore.Qt.Key.Key_M))
        mode_action.setShortcutContext(QtCore.Qt.ShortcutContext.WidgetWithChildrenShortcut)
        mode_action.triggered.connect(
            lambda: self.set_mode(self.Mode.ANALYZING if self._mode == self.Mode.TAGGING else self.Mode.TAGGING)
        )
        self.addAction(mode_action)

        delete_tag_action = QtGui.QAction(self)
        delete_tag_action.setShortcut(QtGui.QKeySequence(QtCore.Qt.Key.Key_Backspace))
        delete_tag_action.setShortcutContext(QtCore.Qt.ShortcutContext.WidgetWithChildrenShortcut)
        delete_tag_action.triggered.connect(self.on_delete_selected_tag)
        self.addAction(delete_tag_action)

        undo_action = QtGui.QAction(self)
        undo_action.setShortcut(QtGui.QKeySequence.StandardKey.Undo)
        undo_action.setShortcutContext(QtCore.Qt.ShortcutContext.WidgetWithChildrenShortcut)
        undo_action.triggered.connect(self.on_undo_last_tag)
        self.addAction(undo_action)

        self.note_debounce_timer.timeout.connect(self.save_note_debounce_fired)
        self.video_player.position_changed_ms.connect(self._update_tag_playhead_highlight)

    def show_team_setup_for_video(self, file_path: str) -> None:
        self.game_setup_widget.set_video_path(file_path)
        self.game_setup_widget.set_team_defaults("", "", "", "")
        self.content_stack.setCurrentIndex(0)
        self.game_setup_widget.set_initial_focus()

    def _on_team_setup_confirmed(
        self,
        file_path: str,
        home_name: str,
        away_name: str,
        home_color: str,
        away_color: str,
    ) -> None:
        if self._tag_session is not None:
            self._tag_session.set_game_teams(home_name, away_name, home_color, away_color)
        self.content_stack.setCurrentIndex(1)
        self.load_video_from_file(file_path)

    def _on_team_setup_cancelled(self) -> None:
        self.video_closed.emit()

    def load_video_from_file(self, file_path: str) -> None:
        if not file_path:
            return

        if self._tag_session is not None:
            self._tag_session.clear()

        self._has_pending_tag = False
        self._pending_main_event = ""
        self._pending_timestamp_ms = 0
        self.tags_list.clear()

        self.video_player.load_video_from_file(file_path)
        self.video_player.set_controls_visible(True)

        self.game_controls.show()
        self.context_team = "Home"

        self.mode_tagging_btn.show()
        self.mode_analyzing_btn.show()
        self.tags_header_label.show()
        self.tags_filter_button.show()
        self.undo_last_tag_button.show()
        self.tags_list.show()

        self._update_filter_buttons_visibility()
        self._update_filter_indicator()

        self.stats_window.set_tag_session(self._tag_session)
        if self._mode == self.Mode.ANALYZING:
            self.stats_window.show()

        self._rebuild_filter_menu()
        self._rebuild_tags_list()

    def on_replace_video(self) -> None:
        file_path = self._prompt_for_video_file()
        if file_path:
            self.load_video_from_file(file_path)

    def on_discard_video(self) -> None:
        self.video_player.set_controls_visible(False)
        self.game_controls.hide()
        self.mode_tagging_btn.hide()
        self.mode_analyzing_btn.hide()

        if self._tag_session is not None:
            self._tag_session.clear()

        self._has_pending_tag = False
        self._pending_main_event = ""
        self._pending_timestamp_ms = 0
        self.tags_list.clear()

        self.tags_header_label.hide()
        self.tags_filter_button.hide()
        self.tags_remove_filters_button.hide()
        self.undo_last_tag_button.hide()
        self.tags_list.hide()
        self.stats_window.hide()

        self.video_closed.emit()

    def on_mode_toggled(self) -> None:
        sender = self.sender()
        if sender == self.mode_tagging_btn:
            self.mode_analyzing_btn.setChecked(False)
            self.set_mode(self.Mode.TAGGING)
        elif sender == self.mode_analyzing_btn:
            self.mode_tagging_btn.setChecked(False)
            self.set_mode(self.Mode.ANALYZING)

    def on_tag_item_activated(self, item: QtWidgets.QListWidgetItem) -> None:
        pos_ms = item.data(QtCore.Qt.ItemDataRole.UserRole)
        if pos_ms is not None:
            self.video_player.seek_to_ms(int(pos_ms))

    def on_tag_selection_changed(self) -> None:
        self.note_debounce_timer.stop()
        if self._pending_note_index >= 0 and self._tag_session is not None:
            self._tag_session.set_tag_note(self._pending_note_index, self._pending_note_text)
            self._pending_note_index = -1
        self._load_note_for_selected_tag()

    def on_note_text_changed(self) -> None:
        item = self.tags_list.currentItem()
        if item is None:
            return
        idx = item.data(QtCore.Qt.ItemDataRole.UserRole + 3)
        if idx is None:
            return
        idx = int(idx)
        if idx < 0:
            return
        if self._tag_session is not None and idx >= len(self._tag_session.tags):
            return
        self._pending_note_index = idx
        self._pending_note_text = self.notes_edit.toPlainText()
        self.note_debounce_timer.start(400)

    def save_note_debounce_fired(self) -> None:
        if self._pending_note_index >= 0 and self._tag_session is not None:
            self._tag_session.set_tag_note(self._pending_note_index, self._pending_note_text)
            self._pending_note_index = -1

    def on_delete_selected_tag(self) -> None:
        if self._tag_session is None:
            return
        item = self.tags_list.currentItem()
        if item is None:
            return
        idx = item.data(QtCore.Qt.ItemDataRole.UserRole + 3)
        if idx is None:
            return
        self._tag_session.remove_tag(int(idx))
        self._rebuild_tags_list()

    def on_undo_last_tag(self) -> None:
        if self._tag_session is None:
            return
        n = len(self._tag_session.tags)
        if n == 0:
            return
        self._tag_session.remove_tag(n - 1)
        self._rebuild_tags_list()

    def on_remove_filters(self) -> None:
        self._active_filter_path_main_event = ""
        self._active_filter_path_follow_up = ""
        self._on_select_all_filters()
        self._update_filter_buttons_visibility()

    def _on_main_event_pressed(self, main_event: str) -> None:
        self._pending_main_event = main_event
        self._pending_timestamp_ms = self.video_player.current_position_ms()
        self._has_pending_tag = True

    def _on_game_event_marked(self, main_event: str, follow_up_event: str) -> None:
        timestamp_ms = self.video_player.current_position_ms()
        if self._has_pending_tag and self._pending_main_event == main_event:
            timestamp_ms = self._pending_timestamp_ms

        self._has_pending_tag = False
        self._pending_main_event = ""
        self._pending_timestamp_ms = 0

        if self._tag_session is None:
            return

        tag = GameTag(
            main_event=main_event,
            follow_up_event=follow_up_event,
            position_ms=timestamp_ms,
            period=self.context_period,
            team=self.context_team,
            situation=self.context_situation,
        )
        self._tag_session.add_tag(tag)

    def _on_session_cleared(self) -> None:
        self._rebuild_filter_menu()
        self._rebuild_tags_list()

    def _on_session_stats_changed(self) -> None:
        self._rebuild_filter_menu()
        self._rebuild_tags_list()

    def _on_session_tag_added(self, _tag: GameTag) -> None:
        self._rebuild_filter_menu()
        self._rebuild_tags_list()
        self._flash_new_tag_row()

    def _load_note_for_selected_tag(self) -> None:
        if self._tag_session is None:
            return
        item = self.tags_list.currentItem()
        self.notes_edit.blockSignals(True)
        if item is None:
            self.notes_edit.clear()
            self.notes_edit.setEnabled(False)
            self.notes_edit.setPlaceholderText("Select a tag to add a note…")
            self.notes_edit.blockSignals(False)
            return

        idx = item.data(QtCore.Qt.ItemDataRole.UserRole + 3)
        if idx is not None and 0 <= int(idx) < len(self._tag_session.tags):
            self.notes_edit.setPlainText(self._tag_session.tag_note(int(idx)))
            self.notes_edit.setEnabled(True)
            self.notes_edit.setPlaceholderText("Note for this tag…")
        else:
            self.notes_edit.clear()
            self.notes_edit.setEnabled(True)
            self.notes_edit.setPlaceholderText("Select a tag to add a note…")
        self.notes_edit.blockSignals(False)

    def _is_main_event_allowed(self, main_event: str) -> bool:
        action = self._filter_actions.get(main_event)
        return True if action is None else action.isChecked()

    def _is_tag_allowed(self, main_event: str, follow_up_event: str) -> bool:
        if self._active_filter_path_main_event:
            if main_event != self._active_filter_path_main_event:
                return False
            if not self._active_filter_path_follow_up:
                return True
            return follow_up_event == self._active_filter_path_follow_up or follow_up_event.startswith(
                self._active_filter_path_follow_up + " → "
            )
        return self._is_main_event_allowed(main_event)

    def _has_any_filter_active(self) -> bool:
        if self._active_filter_path_main_event:
            return True
        return any(not action.isChecked() for action in self._filter_actions.values())

    def _update_filter_buttons_visibility(self) -> None:
        self.tags_remove_filters_button.setVisible(self._has_any_filter_active())

    def _rebuild_filter_menu(self) -> None:
        prev_checked = {main: act.isChecked() for main, act in self._filter_actions.items()}
        self.tags_filter_menu.clear()
        self._filter_actions.clear()

        select_all = self.tags_filter_menu.addAction("Select all")
        select_none = self.tags_filter_menu.addAction("Select none")
        select_all.triggered.connect(self._on_select_all_filters)
        select_none.triggered.connect(self._on_select_no_filters)
        self.tags_filter_menu.addSeparator()

        if self._tag_session is None:
            return

        for main_event in sorted(self._tag_session.main_event_counts.keys(), key=str.lower):
            act = self.tags_filter_menu.addAction(main_event)
            act.setCheckable(True)
            act.setChecked(prev_checked.get(main_event, True))
            act.toggled.connect(self._on_filter_action_toggled)
            self._filter_actions[main_event] = act

    def _update_filter_indicator(self) -> None:
        if self._active_filter_path_main_event:
            path_text = self._active_filter_path_main_event
            if self._active_filter_path_follow_up:
                path_text += " → " + self._active_filter_path_follow_up
            self.tags_filter_indicator.setText("Filtered by: " + path_text)
            self.tags_filter_indicator.show()
            return

        active_filters = [main for main, action in self._filter_actions.items() if action.isChecked()]
        if not active_filters or len(active_filters) == len(self._filter_actions):
            self.tags_filter_indicator.hide()
            return

        active_filters.sort(key=str.lower)
        self.tags_filter_indicator.setText("Filtered by: " + ", ".join(active_filters))
        self.tags_filter_indicator.show()

    def _rebuild_tags_list(self) -> None:
        self.tags_list.clear()
        if self._tag_session is None:
            return

        entries: list[tuple[GameTag, int]] = []
        for idx, tag in enumerate(self._tag_session.tags):
            if self._is_tag_allowed(tag.main_event, tag.follow_up_event):
                entries.append((tag, idx))

        entries.sort(key=lambda pair: pair[0].position_ms)

        for tag, session_index in entries:
            event_text = tag.main_event
            if tag.follow_up_event:
                event_text += " → " + tag.follow_up_event
            row_text = f"{_format_timestamp_ms(tag.position_ms)}  {event_text}"
            item = QtWidgets.QListWidgetItem(row_text)
            item.setData(QtCore.Qt.ItemDataRole.UserRole, tag.position_ms)
            item.setData(QtCore.Qt.ItemDataRole.UserRole + 1, tag.main_event)
            item.setData(QtCore.Qt.ItemDataRole.UserRole + 2, tag.follow_up_event)
            item.setData(QtCore.Qt.ItemDataRole.UserRole + 3, session_index)
            self.tags_list.addItem(item)

        self.tags_list.scrollToBottom()
        self._update_filter_indicator()
        self._update_filter_buttons_visibility()
        self._update_tag_playhead_highlight(self.video_player.current_position_ms())

    def _flash_new_tag_row(self) -> None:
        if self.tags_list.count() == 0:
            return
        if self._new_tag_flash_timer is None:
            self._new_tag_flash_timer = QtCore.QTimer(self)
            self._new_tag_flash_timer.setSingleShot(True)
            self._new_tag_flash_timer.timeout.connect(self._clear_new_tag_flash)

        self._new_tag_flash_timer.stop()
        self._new_tag_flash_row = self.tags_list.count() - 1
        item = self.tags_list.item(self._new_tag_flash_row)
        if item is not None:
            item.setBackground(QtGui.QBrush(QtGui.QColor(147, 197, 253)))
        self._new_tag_flash_timer.start(500)

    def _clear_new_tag_flash(self) -> None:
        if self._new_tag_flash_row >= 0:
            item = self.tags_list.item(self._new_tag_flash_row)
            if item is not None:
                item.setBackground(QtGui.QBrush())
        self._new_tag_flash_row = -1
        self._update_tag_playhead_highlight(self.video_player.current_position_ms())

    def _update_tag_playhead_highlight(self, position_ms: int) -> None:
        for row in range(self.tags_list.count()):
            item = self.tags_list.item(row)
            if item is None:
                continue
            tag_ms = int(item.data(QtCore.Qt.ItemDataRole.UserRole) or 0)
            if abs(tag_ms - position_ms) <= 2000:
                item.setBackground(QtGui.QBrush(QtGui.QColor(147, 197, 253)))
            else:
                item.setBackground(QtGui.QBrush())

    def _on_select_all_filters(self) -> None:
        for action in self._filter_actions.values():
            action.setChecked(True)
        self._rebuild_tags_list()
        self._update_filter_indicator()
        self._update_filter_buttons_visibility()

    def _on_select_no_filters(self) -> None:
        for action in self._filter_actions.values():
            action.setChecked(False)
        self._rebuild_tags_list()
        self._update_filter_indicator()
        self._update_filter_buttons_visibility()

    def _on_filter_action_toggled(self, _checked: bool) -> None:
        self._rebuild_tags_list()
        self._update_filter_indicator()
        self._update_filter_buttons_visibility()

    def _prompt_for_video_file(self) -> str:
        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Select a video file",
            "",
            "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)",
        )
        return file_path

    @staticmethod
    def _clear_layout(layout: QtWidgets.QLayout) -> None:
        while layout.count():
            item = layout.takeAt(0)
            del item

    @staticmethod
    def _move_widget(widget: QtWidgets.QWidget, layout: QtWidgets.QBoxLayout, stretch: int = 0) -> None:
        parent = widget.parentWidget()
        if parent is not None and parent.layout() is not None:
            parent.layout().removeWidget(widget)
        layout.addWidget(widget, stretch)
