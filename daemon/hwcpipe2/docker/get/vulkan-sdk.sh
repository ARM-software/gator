#!/bin/env bash

set -e

VERSION=$1
RESULT=$2

VULKAN_SDK_NAME=vulkansdk-linux-x86_64-${VERSION}
VULKAN_SDK_ARCHIVE=${VULKAN_SDK_NAME}.tar.gz
VULKAN_SDK_URL=https://sdk.lunarg.com/sdk/download/${VERSION}/linux/${VULKAN_SDK_ARCHIVE}

TEMP_DIR=$(mktemp -d)
cd ${TEMP_DIR}

curl -O -L ${VULKAN_SDK_URL}
tar -xf ${VULKAN_SDK_ARCHIVE}

mkdir -p ${RESULT}/usr/local
mv ${VERSION}/x86_64/* ${RESULT}/usr/local/

cd - > /dev/null
rm -rf ${TEMP_DIR}
