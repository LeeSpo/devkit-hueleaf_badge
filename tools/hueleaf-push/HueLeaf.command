#!/bin/bash
# macOS: double-click to start the HueLeaf GUI. First run creates .venv and installs requirements.
cd "$(dirname "$0")" || exit 1
PY=$(command -v python3)
if [ -z "$PY" ]; then
  echo "未找到 python3。请先安装 Python 3（https://www.python.org/downloads/ 或终端运行 xcode-select --install）"
  read -r -p "按回车关闭…" _; exit 1
fi
if [ ! -x .venv/bin/python ]; then
  echo "首次运行：正在准备环境（约 1 分钟）…"
  "$PY" -m venv .venv && .venv/bin/python -m pip install -q --upgrade pip && \
  .venv/bin/python -m pip install -q -r requirements.txt || { rm -rf .venv; echo "安装失败，请检查网络后重试"; read -r -p "按回车关闭…" _; exit 1; }
fi
if ! .venv/bin/python -c "import PIL, serial, numpy, numba" >/dev/null 2>&1; then
  echo "正在更新图像转换组件…"
  .venv/bin/python -m pip install -q -r requirements.txt || { echo "更新失败，请检查网络后重试"; read -r -p "按回车关闭…" _; exit 1; }
fi
echo "HueLeaf 已启动，浏览器会自动打开。用完关闭此窗口即可。"
exec .venv/bin/python hueleaf_gui.py
