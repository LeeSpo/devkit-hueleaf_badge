#!/usr/bin/env bash
# Source-development commands, including compatibility with the old workspace entry.
set -euo pipefail
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
PY=${HUELEAF_PYTHON:-}
if [ -z "$PY" ]; then PY=$(command -v python3.12 || command -v python3 || true); fi
if [ -z "$PY" ]; then echo '请安装 Python 3.12。' >&2; exit 1; fi
LOCAL=$("$PY" "$HERE/paths.py")
"$PY" "$HERE/bootstrap.py" --setup-only
HOST="$LOCAL/venvs/hueleaf-host/bin/python"
case "${1:-menu}" in
    build|preflight|install) source "$HERE/dev-env.sh" ;;
    test) "$HOST" -c 'import pytest' 2>/dev/null || "$HOST" -m pip install pytest ;;
esac
exec "$HOST" "$HERE/workspace.py" "$@"
