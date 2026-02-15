"""Centralized styles for AVA application widgets."""

# Colors (HEX)
COLOR_SKY_950 = "#082f49"
COLOR_SKY_900 = "#0c4a6e"
COLOR_SKY_100 = "#e0f2fe"
COLOR_SKY_50 = "#f0f9ff"
COLOR_WHITE = "#ffffff"
COLOR_BLACK = "#000000"

COLOR_SLATE_700 = "#334155"
COLOR_SLATE_950 = "#020617"
COLOR_SLATE_50 = "#f8fafc"

COLOR_GRAY_100 = "#f3f4f6"

COLOR_GRAY_LIGHT = "#f0f0f0"
COLOR_HOVER_LIGHT = "#f0f9ff"
COLOR_PRESSED = "#e0e0e0"
COLOR_BORDER_INPUT = "#ccc"
COLOR_BORDER_FOCUS = "#333"

# Main window
STYLE_MAIN_WINDOW = f"""
    QWidget {{
        background-color: {COLOR_SLATE_50};
    }}
"""

# Labels
STYLE_LABEL_TITLE = f"""
    font-size: 32px;
    font-weight: bold;
    color: {COLOR_BLACK};
"""

STYLE_LABEL_DESCRIPTION = f"""
    font-size: 16px;
    color: {COLOR_SLATE_950};
"""

# Import video button (with hover)
STYLE_BUTTON_IMPORT_VIDEO = f"""
    QPushButton {{
        background-color: {COLOR_WHITE};
        color: {COLOR_BLACK};
        padding: 10px 20px;
        border: 2px solid {COLOR_BLACK};
        border-radius: 5px;
    }}
    QPushButton:hover {{
        background-color: {COLOR_HOVER_LIGHT};
    }}
    QPushButton:pressed {{
        background-color: {COLOR_PRESSED};
    }}
"""

# Same look, no hover/pressed (used after video is selected)
STYLE_BUTTON_IMPORT_VIDEO_SELECTED = f"""
    QPushButton {{
        background-color: {COLOR_GRAY_LIGHT};
        color: {COLOR_SLATE_950};
        padding: 10px 20px;
        border: 2px solid {COLOR_SLATE_950};
        border-radius: 5px;
    }}
"""

# Line edits
STYLE_LINE_EDIT = f"""
    QLineEdit {{
        min-height: 36px;
        padding: 8px 12px;
        font-size: 14px;
        border: 1px solid {COLOR_BORDER_INPUT};
        border-radius: 4px;
    }}
    QLineEdit:focus {{
        border-color: {COLOR_BORDER_FOCUS};
    }}
"""

# Date edit (match line edit sizing)
STYLE_DATE_EDIT = f"""
    QDateEdit {{
        min-height: 36px;
        padding: 8px 12px;
        font-size: 14px;
        border: 1px solid {COLOR_BORDER_INPUT};
        border-radius: 4px;
    }}
    QDateEdit:focus {{
        border-color: {COLOR_BORDER_FOCUS};
    }}
"""
