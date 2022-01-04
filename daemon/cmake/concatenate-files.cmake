# Copyright (C) 2010-2020 by Arm Limited. All rights reserved.

# Split the argument string
SEPARATE_ARGUMENTS(FILES_TO_CONCATENATE)

# Clear the file to begin with
FILE(WRITE                  ${OUTPUT_FILE}
                            "")

# Iterate over files to append
FOREACH(FILE_TO_CONCAT      ${FILES_TO_CONCATENATE})
    FILE(READ               ${FILE_TO_CONCAT}
                            FILE_CONTENTS)
    FILE(APPEND             ${OUTPUT_FILE}
                            "${FILE_CONTENTS}")
ENDFOREACH()

