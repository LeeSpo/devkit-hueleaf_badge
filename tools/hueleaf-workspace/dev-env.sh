#!/usr/bin/env bash
# Source from bash/zsh; all settings are local to this shell.
if [ -n "${BASH_VERSION:-}" ] && [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "请使用：source /path/to/tools/hueleaf-workspace/dev-env.sh" >&2
    exit 1
fi
if [ -n "${ZSH_VERSION:-}" ]; then
    _hl_file="${(%):-%x}"
elif [ -n "${BASH_VERSION:-}" ]; then
    _hl_file="${BASH_SOURCE[0]}"
else
    echo "请在 bash/zsh 中加载环境。" >&2
    return 1
fi
_hl_here=$(CDPATH= cd -- "$(dirname -- "$_hl_file")" && pwd -P)
_hl_python=${HUELEAF_PYTHON:-$(command -v python3.12 || command -v python3)}
_hl_local=$("$_hl_python" "$_hl_here/paths.py")
_hl_version=${HUELEAF_IDF_VERSION:-v5.5.4}
export IDF_PATH="$_hl_local/esp-idf/$_hl_version"
export IDF_TOOLS_PATH="$_hl_local/espressif"
export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf${_hl_version#v}_py3.12_env"
export PIP_CACHE_DIR="$_hl_local/cache/pip"
export CCACHE_DIR="$_hl_local/cache/ccache"
export IDF_COMPONENT_CACHE_PATH="$_hl_local/cache/components"
if [ ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ] || [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "未找到 $_hl_version 环境，请运行 $_hl_here/setup-dev.sh。" >&2
    unset _hl_file _hl_local _hl_version _hl_here _hl_python
    return 1
fi
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
unset _hl_file _hl_local _hl_version _hl_here _hl_python
. "$IDF_PATH/export.sh"
