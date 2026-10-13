"""Shared by the tools: where the repo is, and reading JSON.

Each tool imports it after putting its own folder on sys.path (python -I does not add it):

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from common import ROOT, load_json  # noqa: E402
"""

import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, "data")


def load_json(path):
    with open(path) as f:
        return json.load(f)
