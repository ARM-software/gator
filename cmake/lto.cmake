# Copyright (C) 2021 by Arm Limited. All rights reserved.

# ###
# Try to determine the linker type
# - also, where appropriate, ensure compiler uses it
# ###
if(CMAKE_LINKER MATCHES ".*ld\\.lld(\\.exe)?")
    set(CMAKE_LINKER_TYPE_IS "lld")
    set(CMAKE_LINKER_LTO_THREAD_COUNT "all")
    set(CMAKE_USE_LD_FLAG "-fuse-ld=lld")
elseif(CMAKE_LINKER MATCHES ".*ld\\.gold(\\.exe)?")
    set(CMAKE_LINKER_TYPE_IS "gold")
    set(CMAKE_LINKER_LTO_THREAD_COUNT "all")
    set(CMAKE_USE_LD_FLAG "-fuse-ld=gold")
elseif(CMAKE_LINKER MATCHES ".*ld?\\.bfd(\\.exe)?")
    set(CMAKE_LINKER_TYPE_IS "bfd")
    set(CMAKE_LINKER_LTO_THREAD_COUNT "1")
elseif(CMAKE_LINKER MATCHES ".*ld(\\.exe)?")
    set(CMAKE_LINKER_TYPE_IS "bfd")
    set(CMAKE_LINKER_LTO_THREAD_COUNT "1")
else()
    set(CMAKE_LINKER_TYPE_IS "unknown")
    set(CMAKE_LINKER_LTO_THREAD_COUNT "1")
endif()

message(STATUS "Detected linker type as: ${CMAKE_LINKER} --> ${CMAKE_LINKER_TYPE_IS} // ${CMAKE_USE_LD_FLAG} // ${CMAKE_LINKER_LTO_THREAD_COUNT}")

# ###
# Ensure the correct linker is used
# ###
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_USE_LD_FLAG}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${CMAKE_USE_LD_FLAG}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${CMAKE_USE_LD_FLAG}")

# ###
# Configure based on the linker type
# ###
if("${CMAKE_LINKER_TYPE_IS}" STREQUAL "lld")
    option(CMAKE_ENABLE_LTO "Enable LTO" ON)
    option(CMAKE_USE_THINLTO "Enable ThinLTO" OFF)
    option(CMAKE_USE_THINLTO_THREADS "Enable ThinLTO threads" ON)
    option(CMAKE_ENABLE_SEC_GC "Enable section GC" ON)
    option(CMAKE_ENABLE_LOG_SEC_GC "Log output of section GC process to identify potential dead code" OFF)
elseif(("${CMAKE_LINKER_TYPE_IS}" STREQUAL "bfd") OR ("${CMAKE_LINKER_TYPE_IS}" STREQUAL "gold"))
    option(CMAKE_ENABLE_LTO "Enable LTO" ON)
    set(CMAKE_USE_THINLTO OFF)
    set(CMAKE_USE_THINLTO_THREADS OFF)
    option(CMAKE_ENABLE_SEC_GC "Enable section GC" ON)
    option(CMAKE_ENABLE_LOG_SEC_GC "Log output of section GC process to identify potential dead code" OFF)
else()
    set(CMAKE_ENABLE_LTO OFF)
    set(CMAKE_USE_THINLTO OFF)
    set(CMAKE_USE_THINLTO_THREADS OFF)
    set(CMAKE_ENABLE_SEC_GC OFF)
    set(CMAKE_ENABLE_LOG_SEC_GC OFF)
endif()

# Where to store the cache
if(CMAKE_USE_THINLTO)
    set(CMAKE_USE_THINLTO_CACHE_DIR "" CACHE STRING "Enable clang ThinLTO global cache (sets cache directory)")
endif()

# ###
# Log status
# ###
message(STATUS "Building with Enable LTO: ${CMAKE_ENABLE_LTO}")
message(STATUS "Building with Enable GC sections: ${CMAKE_ENABLE_SEC_GC}")

if(CMAKE_ENABLE_SEC_GC)
    message(STATUS "Building with GC section logging: ${CMAKE_ENABLE_LOG_SEC_GC}")
endif()

message(STATUS "Linking with LLD ThinLTO: ${CMAKE_USE_THINLTO}")

if(CMAKE_USE_THINLTO)
    message(STATUS "Linking with LLD ThinLTO global cache: ${CMAKE_USE_THINLTO_CACHE_DIR}")
endif()

# ###
# Configure LTO options
# ###
if(CMAKE_ENABLE_LTO)
    if(CMAKE_USE_THINLTO)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=thin")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=thin")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -flto=thin")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=thin")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=auto")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=auto")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -flto=auto")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=auto")
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_ENABLE_LTO_ADDITIONAL_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_ENABLE_LTO_ADDITIONAL_C_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_ENABLE_LTO_ADDITIONAL_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${CMAKE_ENABLE_LTO_ADDITIONAL_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${CMAKE_ENABLE_LTO_ADDITIONAL_LINKER_FLAGS}")

    if("${CMAKE_LINKER_TYPE_IS}" STREQUAL "lld")
        if(CMAKE_USE_THINLTO AND CMAKE_USE_THINLTO_CACHE_DIR)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--thinlto-cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--thinlto-cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--thinlto-cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
        endif()

        if(CMAKE_USE_THINLTO AND CMAKE_USE_THINLTO_THREADS)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--thinlto-jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--thinlto-jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--thinlto-jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
        endif()
    elseif("${CMAKE_LINKER_TYPE_IS}" STREQUAL "gold")
        if(CMAKE_USE_THINLTO AND CMAKE_USE_THINLTO_CACHE_DIR)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-plugin-opt,cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-plugin-opt,cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-plugin-opt,cache-dir=${CMAKE_USE_THINLTO_CACHE_DIR}")
        endif()

        if(CMAKE_USE_THINLTO AND CMAKE_USE_THINLTO_THREADS)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-plugin-opt,jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-plugin-opt,jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-plugin-opt,jobs=${CMAKE_LINKER_LTO_THREAD_COUNT}")
        endif()
    endif()
endif()

# ###
# Configure section GC options
# ###
if(CMAKE_ENABLE_SEC_GC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

    if(CMAKE_ENABLE_LOG_SEC_GC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections,--print-gc-sections")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--gc-sections,--print-gc-sections")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,--print-gc-sections")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--gc-sections")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")
    endif()
endif()

# ###
# Make sure LTO cache dir exists
# ###
if(CMAKE_USE_THINLTO AND CMAKE_USE_THINLTO_CACHE_DIR AND(NOT EXISTS "${CMAKE_USE_THINLTO_CACHE_DIR}"))
    file(MAKE_DIRECTORY "${CMAKE_USE_THINLTO_CACHE_DIR}")
endif()
