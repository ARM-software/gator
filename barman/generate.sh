#!/bin/bash
# Copyright (C) 2016-2023 by Arm Limited.
# SPDX-License-Identifier: BSD-3-Clause

pushd $(dirname "${BASH_SOURCE[0]}") >/dev/null
SCRIPT_DIR=`pwd`
popd > /dev/null

export PYTHONPATH=$PYTHONPATH:"$SCRIPT_DIR"

# try to work out what the python 3 executable is called
if [[ -z $PYTHON_CMD ]]; then
    PYTHON_CMD=python
    $PYTHON_CMD "$SCRIPT_DIR/check_version.py"
    if (($? != 0)); then
        PYTHON_CMD=python3
        $PYTHON_CMD "$SCRIPT_DIR/check_version.py"
        if (($? != 0)); then
            echo "Could not detect a Python 3 executable."
            echo "If Python 3 is installed please set the PYTHON_CMD variable to the path of the python executable"
            exit -1
        fi
    fi
fi

if [[ $# -eq 0 ]]; then
    ARGS="-h"
else
    HAS_DIR_OVERRIDE=0
    for arg in "$@"; do
        if [[ $arg == "-d" ]]; then
            HAS_DIR_OVERRIDE=1
            break
        elif [[ $arg == "--barman-dir" ]]; then
            HAS_DIR_OVERRIDE=1
            break
        fi
    done

    if [[ $HAS_DIR_OVERRIDE -eq 0 ]]; then
        ARGS="-d $SCRIPT_DIR"
    fi
fi

$PYTHON_CMD -m generate_barman $ARGS $*
