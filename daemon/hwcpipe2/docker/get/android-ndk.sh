#!/bin/env bash

set -e

VERSION=$1
RESULT=$2

SDK_ROOT=android_sdk
CMD_TOOLS=cmdline-tools
CMD_TOOLS_VERSION=7583922_latest
CMD_TOOLS_ARCHIVE=commandlinetools-linux-${CMD_TOOLS_VERSION}.zip
CMD_TOOLS_URL=https://dl.google.com/android/repository/${CMD_TOOLS_ARCHIVE}

TEMP_DIR=$(mktemp -d)
cd ${TEMP_DIR}

curl -L -O ${CMD_TOOLS_URL}
unzip ${CMD_TOOLS_ARCHIVE}

yes | ${CMD_TOOLS}/bin/sdkmanager --sdk_root=${SDK_ROOT} --install "ndk;${VERSION}"

mkdir -p ${RESULT}/opt
mv "${SDK_ROOT}/ndk/${VERSION}" "${RESULT}/opt/ndk"

cd - > /dev/null
rm -rf ${TEMP_DIR}
