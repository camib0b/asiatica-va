"""Style loading and dynamic style-property helpers for AVA."""

from pathlib import Path

from qt_compat import QtCore, QtWidgets

# Import to register resources when resources_rc.py exists.
try:  # pragma: no cover - resource file is generated in local workflow
    import resources_rc  # noqa: F401
except ImportError:  # pragma: no cover
    resources_rc = None  # noqa: F841

# Legacy role constants retained for compatibility with existing code.
ROLE_MAIN_WINDOW = "main-window"
ROLE_LABEL_TITLE = "label-title"
ROLE_LABEL_DESCRIPTION = "label-description"
ROLE_IMPORT_VIDEO = "import-video"
ROLE_IMPORT_VIDEO_SELECTED = "import-video-selected"
ROLE_METADATA_INPUT = "metadata-input"

# Resource path for stylesheet.
QSS_PATH = ":/main.qss"


def _read_text_file(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8")


def load_stylesheet() -> str:
    """Load QSS from disk first (dev-friendly), then Qt resources."""
    disk_qss = _read_text_file(Path(__file__).with_name("main.qss"))
    if disk_qss:
        return disk_qss

    file = QtCore.QFile(QSS_PATH)
    mode = QtCore.QIODevice.OpenModeFlag.ReadOnly | QtCore.QIODevice.OpenModeFlag.Text
    if not file.open(mode):
        return ""
    content = bytes(file.readAll()).decode("utf-8")
    file.close()
    return content


def polish_widget(widget: QtWidgets.QWidget) -> None:
    """Force stylesheet re-evaluation after property change."""
    style = widget.style()
    if style:
        style.unpolish(widget)
        style.polish(widget)
    widget.update()


def set_prop(widget: QtWidgets.QWidget, key: str, value) -> bool:
    """Set a dynamic property and repolish only when value changes."""
    if widget is None:
        return False
    if widget.property(key) == value:
        return False
    style = widget.style()
    if style:
        style.unpolish(widget)
    widget.setProperty(key, value)
    if style:
        style.polish(widget)
    widget.update()
    return True


def set_role(widget: QtWidgets.QWidget, role: str) -> bool:
    return set_prop(widget, "role", role)


def set_variant(widget: QtWidgets.QWidget, variant: str) -> bool:
    return set_prop(widget, "variant", variant)


def set_size(widget: QtWidgets.QWidget, size: str) -> bool:
    return set_prop(widget, "size", size)


def set_state(widget: QtWidgets.QWidget, key: str, on: bool) -> bool:
    return set_prop(widget, key, on)
