"""In-memory tag session state and derived statistics."""

from __future__ import annotations

from dataclasses import dataclass

from qt_compat import QtCore, Signal


@dataclass
class GameTag:
    main_event: str
    follow_up_event: str = ""
    position_ms: int = 0
    note: str = ""
    period: str = ""
    team: str = ""
    situation: str = ""


class TagSession(QtCore.QObject):
    cleared = Signal()
    tag_added = Signal(object)
    tag_note_changed = Signal(int)
    stats_changed = Signal()

    def __init__(self, parent: QtCore.QObject | None = None) -> None:
        super().__init__(parent)
        self._tags: list[GameTag] = []
        self._main_event_counts: dict[str, int] = {}
        self._follow_up_counts_by_main_event: dict[str, dict[str, int]] = {}
        self._home_team_name = ""
        self._away_team_name = ""
        self._home_team_color = ""
        self._away_team_color = ""

    @property
    def tags(self) -> list[GameTag]:
        return self._tags

    @property
    def main_event_counts(self) -> dict[str, int]:
        return self._main_event_counts

    @property
    def follow_up_counts_by_main_event(self) -> dict[str, dict[str, int]]:
        return self._follow_up_counts_by_main_event

    @property
    def home_team_name(self) -> str:
        return self._home_team_name

    @property
    def away_team_name(self) -> str:
        return self._away_team_name

    @property
    def home_team_color(self) -> str:
        return self._home_team_color

    @property
    def away_team_color(self) -> str:
        return self._away_team_color

    def clear(self) -> None:
        self._tags.clear()
        self._main_event_counts.clear()
        self._follow_up_counts_by_main_event.clear()
        self.cleared.emit()
        self.stats_changed.emit()

    def clear_team_info(self) -> None:
        self._home_team_name = ""
        self._away_team_name = ""
        self._home_team_color = ""
        self._away_team_color = ""

    def set_game_teams(
        self,
        home_name: str,
        away_name: str,
        home_color: str,
        away_color: str,
    ) -> None:
        self._home_team_name = home_name.strip()
        self._away_team_name = away_name.strip()
        self._home_team_color = home_color.strip()
        self._away_team_color = away_color.strip()

    def add_tag(self, tag: GameTag) -> None:
        self._tags.append(tag)
        self._main_event_counts[tag.main_event] = self._main_event_counts.get(tag.main_event, 0) + 1

        if tag.follow_up_event:
            follow_ups = self._follow_up_counts_by_main_event.setdefault(tag.main_event, {})
            follow_ups[tag.follow_up_event] = follow_ups.get(tag.follow_up_event, 0) + 1

        self.tag_added.emit(tag)
        self.stats_changed.emit()

    def remove_tag(self, index: int) -> None:
        if index < 0 or index >= len(self._tags):
            return

        tag = self._tags[index]

        main_count = self._main_event_counts.get(tag.main_event, 0)
        if main_count > 1:
            self._main_event_counts[tag.main_event] = main_count - 1
        elif main_count == 1:
            self._main_event_counts.pop(tag.main_event, None)

        if tag.follow_up_event:
            follow_ups = self._follow_up_counts_by_main_event.get(tag.main_event)
            if follow_ups is not None:
                follow_count = follow_ups.get(tag.follow_up_event, 0)
                if follow_count > 1:
                    follow_ups[tag.follow_up_event] = follow_count - 1
                elif follow_count == 1:
                    follow_ups.pop(tag.follow_up_event, None)
                    if not follow_ups:
                        self._follow_up_counts_by_main_event.pop(tag.main_event, None)

        self._tags.pop(index)
        self.stats_changed.emit()

    def set_tag_note(self, index: int, note: str) -> None:
        if index < 0 or index >= len(self._tags):
            return
        if self._tags[index].note == note:
            return
        self._tags[index].note = note
        self.tag_note_changed.emit(index)

    def tag_note(self, index: int) -> str:
        if index < 0 or index >= len(self._tags):
            return ""
        return self._tags[index].note
