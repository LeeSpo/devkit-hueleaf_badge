@echo off
setlocal
chcp 65001 >nul
if defined HUELEAF_PYTHON (
  "%HUELEAF_PYTHON%" "%~dp0bootstrap.py" %*
) else (
  where py >nul 2>nul
  if not errorlevel 1 (
    py -3.12 "%~dp0bootstrap.py" %*
  ) else (
    python "%~dp0bootstrap.py" %*
  )
)
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" pause
exit /b %RC%
