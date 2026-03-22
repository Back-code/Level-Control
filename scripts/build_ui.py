Import("env")

import os
import subprocess
import sys
from pathlib import Path


project_dir = Path(env["PROJECT_DIR"])
ui_dir = project_dir / "ui"

if not ui_dir.exists():
    print("[build_ui] ui directory not found, skipping")
else:
    npm_cmd = "npm.cmd" if os.name == "nt" else "npm"
    print("[build_ui] running npm run build in ui/")
    result = subprocess.run(
        [npm_cmd, "run", "build"],
        cwd=str(ui_dir),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    # Print output through ascii-safe encoding to avoid CP1252 issues on Windows terminals
    for line in (result.stdout + result.stderr).splitlines():
        safe = line.encode("ascii", "replace").decode("ascii")
        print(safe)
    if result.returncode != 0:
        sys.exit(result.returncode)
