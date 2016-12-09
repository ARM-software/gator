# Copyright (C) ARM Limited 2010-2016. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(gatord-xml-escape)

# The source files to build
SET(GATORD_ESCAPE_SOURCES          ${CMAKE_CURRENT_SOURCE_DIR}/escape.c)

# Generate the gatord-xml-escape executable
ADD_EXECUTABLE(gatord-xml-escape   ${GATORD_ESCAPE_SOURCES})

