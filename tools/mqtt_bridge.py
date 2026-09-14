#!/usr/bin/env python3
"""Compatibility entry point. The shared implementation is tools/common/."""
from pathlib import Path
import runpy

runpy.run_path(Path(__file__).parent / "common" / "mqtt_bridge.py", run_name="__main__")
