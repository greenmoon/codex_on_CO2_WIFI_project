"""Load the private OTA password from ignored include/secrets.h for espota."""

import re
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.

project_dir = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
secrets_path = project_dir / "include" / "secrets.h"
text = secrets_path.read_text(encoding="utf-8")


def find_string(name: str) -> str | None:
    match = re.search(
        rf"constexpr\s+char\s+{re.escape(name)}\s*\[\s*\]\s*=\s*\"([^\"]+)\"\s*;",
        text,
    )
    return match.group(1) if match else None


password = find_string("kOtaPassword")
if not password:
    raise RuntimeError(
        "Define dedicated kOtaPassword[] in ignored include/secrets.h"
    )

# Run as a post script because the ESP32 platform creates UPLOADERFLAGS in its
# main builder. Remove --debug so espota does not echo the private password in
# its parsed-options diagnostic output.
uploader_flags = [
    flag for flag in env.get("UPLOADERFLAGS", []) if flag != "--debug"  # type: ignore[name-defined]
]
uploader_flags.extend(["--auth", password])
env.Replace(UPLOADERFLAGS=uploader_flags)  # type: ignore[name-defined]
