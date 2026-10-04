#!/bin/zsh
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
exec "$PROJECT_DIR/.venv/bin/python" "$PROJECT_DIR/tools/mqtt_bridge.py"
