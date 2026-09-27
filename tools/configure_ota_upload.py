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


password = find_string("kOtaPassword") or find_string("kRouterPassword")
if not password:
    raise RuntimeError(
        "Define kOtaPassword[] or kRouterPassword[] in ignored include/secrets.h"
    )

env.Append(UPLOAD_FLAGS=[f"--auth={password}"])  # type: ignore[name-defined]
