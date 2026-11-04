import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("flash_install", TOOLS / "flash_install.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def test_external_backup_and_protected_write_offsets(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    calls = []
    monkeypatch.setattr(installer.hl_preflight, "main", lambda args: calls.append(args) or 0)

    def run(cmd, **kwargs):
        calls.append((cmd, kwargs))
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(installer.subprocess, "run", run)
    assert installer.main(["/dev/test", "--backup-dir", "backups"]) == 0
    assert calls[0] == ["check", "--port", "/dev/test", "--ptable",
                        str(TOOLS / "partition-table.bin"), "--backup-dir", str(tmp_path / "backups")]
    cmd, kwargs = calls[1]
    assert kwargs["cwd"] == str(TOOLS)
    assert cmd[cmd.index("write_flash") + 1::2] == ["0x2000", "0x8000", "0x180000", "0x190000"]
    assert Path.cwd() == tmp_path


@pytest.mark.parametrize("result", [1, 3])
def test_preflight_failure_prevents_write(result, monkeypatch):
    monkeypatch.setattr(installer.hl_preflight, "main", lambda args: result)

    def unexpected_write(*args, **kwargs):
        pytest.fail("flash must not run after a failed preflight")

    monkeypatch.setattr(installer.subprocess, "run", unexpected_write)
    assert installer.main(["/dev/test"]) == result


@pytest.fixture
def packaged_installers(tmp_path):
    bash = shutil.which("bash")
    if bash is None:
        pytest.skip("bash is required to generate the packaged launchers")
    install_dir = tmp_path / "install package"
    install_dir.mkdir()
    shutil.copy2(TOOLS / "flash_install.py", install_dir)
    # Use a fake preflight that records arguments and stops before any flash access.
    (install_dir / "hl_preflight.py").write_text(
        "import json, os\n"
        "def main(args):\n"
        "    with open(os.environ['HUELEAF_TEST_CAPTURE'], 'w') as output:\n"
        "        json.dump({'cwd': os.getcwd(), 'args': args}, output)\n"
        "    return 3\n",
        encoding="utf-8",
    )
    package = (TOOLS / "package.sh").read_text(encoding="utf-8")
    start = package.index('cat > "$I/flash_install.sh"')
    end = package.index("\npython -m esptool", start)
    # Run the actual launcher generation without building or packaging firmware.
    subprocess.run([bash, "-c", 'I="$1"\n' + package[start:end],
                    "generate-launchers", str(install_dir)], check=True)
    return install_dir


@pytest.mark.parametrize("launcher", [
    "sh",
    pytest.param("bat", marks=pytest.mark.skipif(
        os.name != "nt", reason="Windows cmd is required to execute the batch launcher")),
])
@pytest.mark.parametrize("backup", ["relative", "absolute", "default"])
def test_packaged_launcher_backup_paths(packaged_installers, tmp_path, launcher, backup):
    caller = tmp_path / "caller directory"
    caller.mkdir()
    capture = tmp_path / "preflight.json"
    args = ["COM5"]
    if backup == "relative":
        args += ["--backup-dir", "relative backups"]
        expected = caller / "relative backups"
    elif backup == "absolute":
        expected = tmp_path / "absolute backups"
        args += ["--backup-dir", str(expected)]
    else:
        expected = packaged_installers
    script = packaged_installers / ("flash_install." + launcher)
    command = (["cmd", "/c", str(script)] if launcher == "bat"
               else [shutil.which("bash"), str(script)])
    result = subprocess.run(command + args, cwd=caller,
                            env={**os.environ, "HUELEAF_TEST_CAPTURE": str(capture)},
                            capture_output=True, text=True)
    assert result.returncode == 3, result.stdout + result.stderr
    recorded = json.loads(capture.read_text(encoding="utf-8"))
    preflight_args = recorded["args"]
    assert Path(preflight_args[preflight_args.index("--backup-dir") + 1]) == expected
    assert preflight_args[preflight_args.index("--port") + 1] == "COM5"
    assert Path(recorded["cwd"]) == caller
