"""Style loading and role constants for AVA application."""

from PySide6.QtCore import QFile, QIODevice

# Import to register resources
import resources_rc  # noqa: F401

# Dynamic property values - use with setProperty("role", ROLE_*)
ROLE_MAIN_WINDOW = "main-window"
ROLE_LABEL_TITLE = "label-title"
ROLE_LABEL_DESCRIPTION = "label-description"
ROLE_IMPORT_VIDEO = "import-video"
ROLE_IMPORT_VIDEO_SELECTED = "import-video-selected"
ROLE_METADATA_INPUT = "metadata-input"

# Resource path for stylesheet
QSS_PATH = ":/main.qss"


def load_stylesheet() -> str:
    """Load the main QSS stylesheet from Qt resources."""
    file = QFile(QSS_PATH)
    if not file.open(QIODevice.OpenModeFlag.ReadOnly | QIODevice.OpenModeFlag.Text):
        return ""
    content = bytes(file.readAll()).decode("utf-8")
    file.close()
    return content


def polish_widget(widget):
    """Force stylesheet re-evaluation after property change."""
    style = widget.style()
    if style:
        style.unpolish(widget)
        style.polish(widget)
        widget.update()
