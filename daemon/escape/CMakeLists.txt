# Copyright (C) 2010-2021 by Arm Limited. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 3.16 FATAL_ERROR)

PROJECT(gatord-xml-escape)

# The source files to build
SET(GATORD_ESCAPE_SOURCES          ${CMAKE_CURRENT_SOURCE_DIR}/escape.c)

# Generate the gatord-xml-escape executable
ADD_EXECUTABLE(gatord-xml-escape   ${GATORD_ESCAPE_SOURCES})

