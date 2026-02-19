"""Top-level main window with welcome/work stack."""

from qt_compat import QtCore, QtWidgets
from state.tag_session import TagSession
from .welcome_window import WelcomeWindow
from .work_window import WorkWindow


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, parent: QtWidgets.QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("AVA | Camila Escudero")
        self.resize(1300, 800)

        self.stack = QtWidgets.QStackedWidget(self)
        self.stack.setObjectName("AppRoot")
        self.stack.setAttribute(QtCore.Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setCentralWidget(self.stack)

        self.welcome_window = WelcomeWindow(self)
        self.work_window = WorkWindow(self)
        self.tag_session = TagSession(self)
        self.work_window.set_tag_session(self.tag_session)

        self.stack.addWidget(self.welcome_window)
        self.stack.addWidget(self.work_window)

        self.welcome_window.video_import_requested.connect(self.on_video_import_requested)
        self.work_window.video_closed.connect(self.on_video_closed)

        self.stack.setCurrentWidget(self.welcome_window)

        app = QtWidgets.QApplication.instance()
        if app is not None:
            app.aboutToQuit.connect(self._reset_session)

    def show_welcome_window(self) -> None:
        self.stack.setCurrentWidget(self.welcome_window)

    def show_work_window_with_setup(self, file_path: str) -> None:
        self.work_window.show_team_setup_for_video(file_path)
        self.stack.setCurrentWidget(self.work_window)

    def on_video_import_requested(self) -> None:
        file_path = self.prompt_for_video_file()
        if file_path:
            self.show_work_window_with_setup(file_path)

    def on_video_closed(self) -> None:
        self._reset_session()
        self.show_welcome_window()

    def prompt_for_video_file(self) -> str:
        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Select a video file",
            "",
            "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)",
        )
        return file_path

    def _reset_session(self) -> None:
        self.tag_session.clear()
        self.tag_session.clear_team_info()
