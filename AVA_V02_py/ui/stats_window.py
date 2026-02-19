"""Tag statistics tree for AVA analysis mode."""

from __future__ import annotations

from qt_compat import QtCore, QtWidgets, Signal
from state.tag_session import TagSession
from styles import set_role


def _format_count_and_percent(count: int, main_count: int) -> str:
    pct = (100.0 * count / main_count) if main_count > 0 else 0.0
    pct_str = str(int(round(pct))) if abs(pct - round(pct)) < 1e-9 else f"{pct:.1f}"
    return f"{count} ({pct_str}%)"


class StatsWindow(QtWidgets.QWidget):
    filter_by_path_requested = Signal(str, str)

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._tag_session: TagSession | None = None
        self._build_ui()
        self._wire_signals()

    def set_tag_session(self, session: TagSession | None) -> None:
        if self._tag_session is session:
            return
        self._tag_session = session
        self._clear_tree()

        if self._tag_session is None:
            return

        self._rebuild_tree()
        self._tag_session.cleared.connect(self._clear_tree)
        self._tag_session.stats_changed.connect(self._rebuild_tree)

    def _build_ui(self) -> None:
        self.setObjectName("StatsPanel")
        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        self.header_label = QtWidgets.QLabel("Stats", self)
        set_role(self.header_label, "h3")

        self.tree = QtWidgets.QTreeWidget(self)
        self.tree.setColumnCount(2)
        self.tree.setHeaderLabels(["Event", "Count"])
        self.tree.setRootIsDecorated(True)
        self.tree.setAlternatingRowColors(True)
        self.tree.setUniformRowHeights(True)
        self.tree.header().setStretchLastSection(False)
        self.tree.header().setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeMode.Stretch)
        self.tree.header().setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeMode.Fixed)
        self.tree.header().resizeSection(1, 90)

        layout.addWidget(self.header_label)
        layout.addWidget(self.tree, 1)

    def _wire_signals(self) -> None:
        self.tree.itemDoubleClicked.connect(self._on_tree_item_double_clicked)

    def _on_tree_item_double_clicked(self, item: QtWidgets.QTreeWidgetItem, _column: int) -> None:
        main_event = item.data(0, QtCore.Qt.ItemDataRole.UserRole)
        follow_up = item.data(0, QtCore.Qt.ItemDataRole.UserRole + 1)
        if main_event:
            self.filter_by_path_requested.emit(str(main_event), str(follow_up or ""))

    def _clear_tree(self) -> None:
        self.tree.clear()

    def _rebuild_tree(self) -> None:
        self._clear_tree()
        if self._tag_session is None:
            return

        mains = sorted(self._tag_session.main_event_counts.keys(), key=str.lower)
        for main_event in mains:
            main_count = self._tag_session.main_event_counts.get(main_event, 0)

            main_item = QtWidgets.QTreeWidgetItem(self.tree)
            main_item.setText(0, main_event)
            main_item.setText(1, str(main_count))
            main_item.setData(0, QtCore.Qt.ItemDataRole.UserRole, main_event)
            main_item.setData(0, QtCore.Qt.ItemDataRole.UserRole + 1, "")

            follow_ups_map = self._tag_session.follow_up_counts_by_main_event.get(main_event)
            if not follow_ups_map:
                continue

            follow_ups = sorted(follow_ups_map.keys(), key=str.lower)
            for follow_up in follow_ups:
                follow_count = follow_ups_map.get(follow_up, 0)
                child = QtWidgets.QTreeWidgetItem(main_item)
                child.setText(0, f"  {follow_up}")
                child.setText(1, _format_count_and_percent(follow_count, main_count))
                child.setData(0, QtCore.Qt.ItemDataRole.UserRole, main_event)
                child.setData(0, QtCore.Qt.ItemDataRole.UserRole + 1, follow_up)

            main_item.setExpanded(True)
