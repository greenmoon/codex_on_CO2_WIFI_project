#!/bin/zsh
# Compatibility entry point; the macOS launcher lives in tools/macos/.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
exec "$PROJECT_DIR/tools/macos/start_mqtt_bridge.command"
