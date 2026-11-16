"""Use canonical firmware helpers in a checkout; bundle those same files for users."""
import importlib.util
from pathlib import Path


def load_helper(name, filename):
    here = Path(__file__).resolve().parent
    bundled = here / ("_" + filename)
    path = bundled if bundled.is_file() else here.parents[1] / "firmware/hueleaf-gallery/tools" / filename
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


preflight = load_helper("hueleaf_device_preflight", "hl_preflight.py")
remote_layout = load_helper("hueleaf_device_layout", "remote_layout.py")
