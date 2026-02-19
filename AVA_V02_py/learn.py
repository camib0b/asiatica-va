"""Compatibility launcher for legacy run commands.

This now starts the modular V02 app. The old monolithic implementation is in
`legacy/learn.py`.
"""

from main import main


if __name__ == "__main__":
    raise SystemExit(main())
