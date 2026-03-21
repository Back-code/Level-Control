Import("env")

import os
import subprocess
from pathlib import Path


project_dir = Path(env["PROJECT_DIR"])
ui_dir = project_dir / "ui"

if not ui_dir.exists():
    print("[build_ui] ui directory not found, skipping")
else:
    npm_cmd = "npm.cmd" if os.name == "nt" else "npm"
    print("[build_ui] running npm run build in ui/")
    subprocess.run([npm_cmd, "run", "build"], cwd=str(ui_dir), check=True)
