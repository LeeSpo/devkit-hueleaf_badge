from pathlib import Path
import os
import shutil
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hueleaf_gui as G


def test_browser_conversion_upload_and_display_state():
    node = shutil.which("node")
    if not node:
        pytest.skip("Node.js is required for the asynchronous browser-state regression")
    script = G.PAGE.split("<script>", 1)[1].split("</script>", 1)[0]
    subprocess.run([node, str(Path(__file__).with_name("gui_conversion_state.js"))],
                   input=script, text=True, capture_output=True, check=True, timeout=30)
