"""Main AVA entrypoint.

During migration this forwards to the legacy implementation.
"""

import runpy


if __name__ == "__main__":
    runpy.run_module("legacy.learn", run_name="__main__")
