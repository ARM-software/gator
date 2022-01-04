# Copyright (C) 2010-2020 by Arm Limited. All rights reserved.

# Split the argument string
SEPARATE_ARGUMENTS(FILES_TO_HASH)

# Generate the hash string which is the concatenation of the MD5 of the hashed files
SET(HASH_STRING             "")
FOREACH(FILE_TO_HASH        ${FILES_TO_HASH})
    FILE(READ               ${FILE_TO_HASH}
                            FILE_HASH)
    SET(HASH_STRING         "${HASH_STRING}${FILE_HASH}")
ENDFOREACH()

# Hash the concatenated string
STRING(MD5                  HASH_STRING
                            "${HASH_STRING}")

# Output to file
MESSAGE(STATUS              "Generated hash value in ${HASH_FILE} = ${HASH_STRING}")
FILE(WRITE                  ${HASH_FILE}
                            "${HASH_STRING}")
MESSAGE(STATUS              "Generated hash value in ${OUTPUT_FILE} with constant name ${CONSTANT_NAME}, hash = ${HASH_STRING}")
FILE(WRITE                  ${OUTPUT_FILE}
                            "extern const char * const ${CONSTANT_NAME} = \"${HASH_STRING}\";")

