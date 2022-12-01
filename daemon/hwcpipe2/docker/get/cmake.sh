#!/bin/env bash

set -e

VERSION=$1
RESULT=$2

CMAKE_NAME=cmake-${VERSION}-linux-x86_64
CMAKE_ARCHIVE=${CMAKE_NAME}.tar.gz
CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v${VERSION}/${CMAKE_ARCHIVE}

TEMP_DIR=$(mktemp -d)
cd ${TEMP_DIR}

curl -L -O ${CMAKE_URL}
tar -xf ${CMAKE_ARCHIVE}

# Trim documentation and other useless files
rm -rf ${CMAKE_NAME}/doc
rm -rf ${CMAKE_NAME}/man
rm -rf ${CMAKE_NAME}/share/aclocal
rm -rf ${CMAKE_NAME}/share/applications
rm -rf ${CMAKE_NAME}/share/bash-completion
rm -rf ${CMAKE_NAME}/share/emacs
rm -rf ${CMAKE_NAME}/share/icons
rm -rf ${CMAKE_NAME}/share/mime
rm -rf ${CMAKE_NAME}/share/vim

mkdir -p ${RESULT}/usr
cp -R ${CMAKE_NAME}/* ${RESULT}/usr


cd - > /dev/null
rm -rf ${TEMP_DIR}
