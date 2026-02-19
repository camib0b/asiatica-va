"""Minimal smoke checks for AVA V02 module wiring.

Run: python3 AVA_V02_py/smoke_test.py
"""

import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

ROOT = os.path.dirname(os.path.abspath(__file__))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from qt_compat import QtWidgets
from state.tag_session import GameTag, TagSession
from ui.main_window import MainWindow
from ui.work_window import WorkWindow


def main() -> int:
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    session = TagSession()
    session.add_tag(GameTag(main_event="Shot", follow_up_event="On target", position_ms=1000))
    assert session.main_event_counts.get("Shot") == 1

    work = WorkWindow()
    work.set_tag_session(session)
    work.set_mode(WorkWindow.Mode.ANALYZING)
    work.set_mode(WorkWindow.Mode.TAGGING)

    main_window = MainWindow()
    assert main_window.stack.count() == 2

    work.deleteLater()
    main_window.deleteLater()
    app.processEvents()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
