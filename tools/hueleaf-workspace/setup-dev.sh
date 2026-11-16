#!/usr/bin/env bash
# Full ESP-IDF development environment; installation only needs bootstrap.py.
set -euo pipefail
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
VERSION=${HUELEAF_IDF_VERSION:-v5.5.4}
PYTHON=${HUELEAF_PYTHON:-}
if [ -z "$PYTHON" ]; then PYTHON=$(command -v python3.12 || true); fi
if [ -z "$PYTHON" ] || [ ! -x "$PYTHON" ]; then
    echo "请设置 HUELEAF_PYTHON=/path/to/python3.12" >&2; exit 1
fi
"$PYTHON" -c 'import sys; assert sys.version_info[:2] == (3, 12), "需要 Python 3.12"'
LOCAL=$("$PYTHON" "$HERE/paths.py")
SOURCE=${HUELEAF_SOURCE:-$(CDPATH= cd -- "$HERE/../.." && pwd -P)}
export IDF_PATH="$LOCAL/esp-idf/$VERSION"
export IDF_TOOLS_PATH="$LOCAL/espressif"
export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf${VERSION#v}_py3.12_env"
export PIP_CACHE_DIR="$LOCAL/cache/pip"
export IDF_COMPONENT_CACHE_PATH="$LOCAL/cache/components"
mkdir -p "$LOCAL/esp-idf" "$LOCAL/logs" "$LOCAL/backups" "$LOCAL/venvs" "$LOCAL/manifests" "$PIP_CACHE_DIR"
if [ ! -d "$IDF_PATH/.git" ]; then
    git clone --branch "$VERSION" --depth 1 https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi
if [ "$(git -C "$IDF_PATH" describe --tags --exact-match HEAD)" != "$VERSION" ]; then
    echo "SDK 版本不匹配，未覆盖 $IDF_PATH。" >&2; exit 1
fi
git -C "$IDF_PATH" -c submodule.fetchJobs=8 submodule update --init --recursive --depth 1
"$PYTHON" "$IDF_PATH/tools/idf_tools.py" install --targets esp32c5
"$PYTHON" "$IDF_PATH/tools/idf_tools.py" install cmake ninja
# Reuse our cached wheelhouse; IDF still applies its official constraints.
"$PYTHON" "$IDF_PATH/tools/idf_tools.py" install-python-env --features core --extra-wheels-dir "$LOCAL/cache/wheels"
HOST="$LOCAL/venvs/hueleaf-host"
if [ ! -x "$HOST/bin/python" ]; then "$PYTHON" -m venv "$HOST"; fi
if [ -f "$SOURCE/tools/hueleaf-push/requirements.txt" ]; then
    "$HOST/bin/python" -m pip install --find-links "$LOCAL/cache/wheels" -r "$SOURCE/tools/hueleaf-push/requirements.txt" pytest
else
    "$HOST/bin/python" -m pip install --find-links "$LOCAL/cache/wheels" 'pyserial>=3.5' 'Pillow>=10' pytest
fi
"$HOST/bin/python" -m pip check
"$HOST/bin/python" -m pip freeze > "$LOCAL/manifests/host-requirements-installed.txt"
"$IDF_PYTHON_ENV_PATH/bin/python" -m pip freeze > "$LOCAL/manifests/idf-requirements-installed.txt"
"$HOST/bin/python" -m pip install -r "$HERE/requirements-install.txt"
printf '\n安装完成：%s/hueleaf.sh status\n' "$HERE"
