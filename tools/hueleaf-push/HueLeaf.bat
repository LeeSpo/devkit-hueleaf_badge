@echo off
rem Windows: double-click to start the HueLeaf GUI. First run creates .venv and installs requirements.
chcp 65001 >nul
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  where py >nul 2>nul && (set "PY=py -3") || (set "PY=python")
  echo 首次运行：正在准备环境（约 1 分钟）...
  call %%PY%% -m venv .venv || goto nopy
  ".venv\Scripts\python.exe" -m pip install -q -r requirements.txt || goto fail
)
".venv\Scripts\python.exe" -c "import PIL, serial, numpy, numba" >nul 2>nul
if errorlevel 1 (
  echo 正在更新图像转换组件...
  ".venv\Scripts\python.exe" -m pip install -q -r requirements.txt || goto depsfail
)
echo HueLeaf 已启动，浏览器会自动打开。用完关闭此窗口即可。
".venv\Scripts\python.exe" hueleaf_gui.py
goto :eof
:nopy
echo 未找到 Python 3。请从 https://www.python.org/downloads/ 安装（勾选 Add to PATH）。
pause & exit /b 1
:fail
rmdir /s /q .venv
echo 安装失败，请检查网络后重试。
pause & exit /b 1

:depsfail
echo 更新失败，请检查网络后重试。
pause & exit /b 1
