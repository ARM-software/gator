#!/bin/env bash

set -e

VERSION=$1
RESULT=$2

NINJA_NAME=ninja
NINJA_ARCHIVE=ninja-linux.zip
NINJA_URL=https://github.com/ninja-build/ninja/releases/download/v${VERSION}/${NINJA_ARCHIVE}

TEMP_DIR=$(mktemp -d)
cd ${TEMP_DIR}

curl -L -O ${NINJA_URL}
unzip ${NINJA_ARCHIVE}

mkdir -p ${RESULT}/usr/bin
cp ninja ${RESULT}/usr/bin/

cd - > /dev/null
rm -rf ${TEMP_DIR}
