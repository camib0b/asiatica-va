"""Work window stub; fully implemented in a later milestone."""

from qt_compat import QtCore, QtWidgets


class WorkWindow(QtWidgets.QWidget):
    video_closed = QtCore.Signal()

    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self._label = QtWidgets.QLabel("Work window is being migrated...", self)
        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(self._label)

    def set_tag_session(self, session) -> None:  # noqa: ANN001
        self._session = session

    def show_team_setup_for_video(self, file_path: str) -> None:
        self._file_path = file_path

    def load_video_from_file(self, file_path: str) -> None:
        self._file_path = file_path
