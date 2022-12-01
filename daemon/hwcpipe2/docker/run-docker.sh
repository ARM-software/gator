#!/bin/sh

export WORK_DIR=$(git rev-parse --show-toplevel)

docker run -w "${WORK_DIR}" -u $(id -u $USER):$(id -g $USER) -v "${WORK_DIR}:${WORK_DIR}" -it gpuddk--docker.artifactory.geo.arm.com/gpuddk/hwcpipe2:latest
