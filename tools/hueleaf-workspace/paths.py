"""Source checkout and writable data are deliberately independent."""
import os
from pathlib import Path
import sys


def source_root():
    configured = os.environ.get("HUELEAF_SOURCE")
    if configured:
        return Path(configured).expanduser().resolve()
    parents = Path(__file__).resolve().parents
    if len(parents) > 2:
        candidate = parents[2]
        if (candidate / "firmware/hueleaf-gallery/CMakeLists.txt").is_file():
            return candidate
    return None


def data_root():
    configured = os.environ.get("HUELEAF_DATA_DIR")
    if configured:
        return Path(configured).expanduser().resolve()
    source = source_root()
    if source:
        return source.parent / "local"
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/HueLeaf"
    if os.name == "nt":
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "HueLeaf"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "hueleaf"


def venv_python(directory):
    return Path(directory) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


if __name__ == "__main__":
    print(data_root())
