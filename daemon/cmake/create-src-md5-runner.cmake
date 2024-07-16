# Copyright (C) 2010-2022 by Arm Limited. All rights reserved.

# as expected for STRING CONFIGURE
CMAKE_POLICY(SET CMP0053 NEW)

# Split the argument string
SEPARATE_ARGUMENTS(FILES_TO_HASH)

# Generate the hash string which is the concatenation of the MD5 of the hashed files
SET(HASH_STRING "")

FOREACH(FILE_TO_HASH ${FILES_TO_HASH})
    FILE(READ ${FILE_TO_HASH}
        FILE_HASH)
    SET(HASH_STRING "${HASH_STRING}${FILE_HASH}")
ENDFOREACH()

# Hash the concatenated string
STRING(MD5 HASH_STRING
    "${HASH_STRING}")

STRING(CONFIGURE "@ID_VALUE@" ID_VALUE @ONLY ESCAPE_QUOTES)

# Output to file
MESSAGE(STATUS "Generated hash value in ${HASH_FILE} = ${HASH_STRING}")
FILE(WRITE ${HASH_FILE}
    "${HASH_STRING}")
MESSAGE(STATUS "Generated hash value in ${OUTPUT_FILE} with constant name ${CONSTANT_NAME}, hash = ${HASH_STRING}")
MESSAGE(STATUS "Generated id value in ${OUTPUT_FILE} with constant name ${ID_NAME}, text = ${ID_VALUE}")
FILE(WRITE ${OUTPUT_FILE}
    "extern const char * const ${CONSTANT_NAME} = \"${HASH_STRING}\";\nextern const char * const ${ID_NAME} = \"${ID_VALUE}\";\nextern const char * const ${COPYRIGHT_NAME} = \"${COPYRIGHT_VALUE}\";\n")
