#!/usr/bin/env bash
set -euo pipefail
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
PY=${HUELEAF_PYTHON:-}
if [ -z "$PY" ]; then PY=$(command -v python3.12 || command -v python3 || true); fi
if [ -z "$PY" ]; then echo '请安装 Python 3.12，或设置 HUELEAF_PYTHON。' >&2; exit 1; fi
exec "$PY" "$HERE/bootstrap.py" "$@"
