rem Copyright (C) 2016-2023 by Arm Limited.
rem SPDX-License-Identifier: BSD-3-Clause

@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set PYTHONPATH=%PYTHONPATH%;%SCRIPT_DIR%

set PYTHON_EXE=python

call :detect_python

set ARGS=
if "%~1"=="" (
	set ARGS="-h"
) else (
	set DIR_OVERRIDE=0
	for %%a in (%*) do (
		if "%%~a"=="-d" (
			set DIR_OVERRIDE=1
		)
		if "%%~a"=="--barman-dir" (
			set DIR_OVERRIDE=1
		)
	)

	if "!DIR_OVERRIDE!"=="0" (
		set ARGS=-d %SCRIPT_DIR%
	)
)

%PYTHON_EXE% -m generate_barman %ARGS% %*
exit /B 0

:detect_python
%PYTHON_EXE% %SCRIPT_DIR%\check_version.py
if %ERRORLEVEL% NEQ 0 (
    set PYTHON_EXE=python3
    %PYTHON_EXE% %SCRIPT_DIR%\check_version.py
    if %ERRORLEVEL% NEQ 0 (
        echo "Could not detect a Python 3 executable"
        exit /B -1
    )
)
