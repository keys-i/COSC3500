"""Run the profile parser self-check"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    subprocess.run(
        [
            sys.executable,
            str(root / "tools/scripts/profiles.py"),
            "--self-test",
        ],
        check=True,
    )
