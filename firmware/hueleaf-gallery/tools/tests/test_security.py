"""Read-only eFuse gate must fail closed before any flash write."""
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest

TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("security_preflight", TOOLS / "hl_preflight.py")
hp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hp)


def summary():
    return {name: {"value": False, "readable": True} for name in hp.SECURITY_FIELDS}


def run_check(monkeypatch, tmp_path, data, rc=0):
    def run(cmd):
        assert "summary" in cmd and "burn_efuse" not in cmd and "write_flash" not in cmd
        Path(cmd[cmd.index("--file") + 1]).write_text(json.dumps(data))
        return subprocess.CompletedProcess(cmd, rc)
    monkeypatch.setattr(hp.subprocess, "run", run)
    return hp.check_security("TEST", str(tmp_path))


def test_unprotected_summary_passes(monkeypatch, tmp_path):
    run_check(monkeypatch, tmp_path, summary())


def test_espefuse_human_values_use_raw_bits(monkeypatch, tmp_path):
    data = summary()
    data["SPI_BOOT_CRYPT_CNT"] = {"value": "Disable", "raw_value": "0x0", "readable": True}
    run_check(monkeypatch, tmp_path, data)
    data["SPI_BOOT_CRYPT_CNT"]["raw_value"] = "0x1"
    with pytest.raises(SystemExit):
        run_check(monkeypatch, tmp_path, data)


@pytest.mark.parametrize("name", hp.SECURITY_FIELDS)
def test_security_bits_refuse_install(name, monkeypatch, tmp_path):
    data = summary()
    data[name]["value"] = 1
    with pytest.raises(SystemExit) as e:
        run_check(monkeypatch, tmp_path, data)
    assert e.value.code == 3


@pytest.mark.parametrize("kind", ["missing", "unreadable", "invalid", "command"])
def test_unknown_security_refuses_install(kind, monkeypatch, tmp_path):
    data = summary()
    name = hp.SECURITY_FIELDS[0]
    if kind == "missing": del data[name]
    if kind == "unreadable": data[name]["readable"] = False
    if kind == "invalid": data[name]["value"] = "unknown"
    with pytest.raises(SystemExit):
        run_check(monkeypatch, tmp_path, data, rc=1 if kind == "command" else 0)
