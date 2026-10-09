"""A smoke boot: builds the port, runs it for a while with no one at the keys
and fails if it crashed or quit by itself. Needs the extracted data. The window
and the sound come up as usual.

    python native/tools/smoke.py [seconds]      (default 20)
"""
import subprocess
import sys
import tempfile
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    r = subprocess.run([sys.executable, NATIVE / "build.py"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"build failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    with tempfile.TemporaryDirectory() as cwd:  # DW3_DUMP and friends write here, not in the tree
        try:
            p = subprocess.run([NATIVE / "build/dw2003.exe"], cwd=cwd, capture_output=True, text=True, timeout=seconds)
        except subprocess.TimeoutExpired as e:
            err = (e.stderr or b"").decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
            if "CRASH" in err:
                sys.exit(f"crashed:\n{err[-2000:]}")
            print(f"OK: ran {seconds} s" + (f", it said:\n{err[-1000:]}" if err.strip() else ""))
            return
    sys.exit(f"quit after less than {seconds} s (code {p.returncode}):\n{p.stderr[-2000:]}")


if __name__ == "__main__":
    main()
