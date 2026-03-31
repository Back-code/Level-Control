Import("env")

import json
import sys
from pathlib import Path

root = Path(env["PROJECT_DIR"])
version_path = root / "version.json"
generated_dir = root / ".pio" / "generated"
generated_header = generated_dir / "GeneratedVersion.h"

with version_path.open("r", encoding="utf-8") as handle:
    version = json.load(handle)

version_str = f"{int(version['major'])}.{int(version['minor'])}.{int(version['commit'])}"

# Optionales Suffix als Argument
suffix = ""
if len(sys.argv) > 1:
    suffix = sys.argv[1]
    if suffix:
        version_str = f"{version_str}|{suffix}"

generated_dir.mkdir(parents=True, exist_ok=True)
generated_header.write_text(
    "#ifndef GENERATED_VERSION_H\n"
    "#define GENERATED_VERSION_H\n"
    f"#define SALZSTAND_VERSION \"{version_str}\"\n"
    "#endif\n",
    encoding="utf-8",
)

env.Append(CPPPATH=[str(generated_dir)])