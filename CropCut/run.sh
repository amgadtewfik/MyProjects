#!/bin/bash
cd "$(dirname "$0")"
# Use the venv's Python directly. On this machine `source activate`
# leaves `python3` pointing at the Homebrew shim, which has no PyQt6 —
# so we bypass the shim by calling the venv's python explicitly.
VENV_PY="$(pwd)/.venv/bin/python3"
exec "$VENV_PY" main.py "$@"
