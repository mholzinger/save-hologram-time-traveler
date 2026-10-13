"""Shared by the tools: where the repo is, and reading JSON.

Each tool imports it after putting its own folder on sys.path (python -I does not add it):

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from common import ROOT, load_json  # noqa: E402
"""

import json
import os
import sys

# In the patcher executable (PyInstaller) the shipped files (data/, tools/fonts/, port/3ds/cia/, app/) are
# unpacked under sys._MEIPASS with the repo's layout.
FROZEN = getattr(sys, "frozen", False)
ROOT = sys._MEIPASS if FROZEN else os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, "data")


def load_json(path):
    with open(path) as f:
        return json.load(f)
