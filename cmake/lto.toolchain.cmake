# Copyright (C) 2021 by Arm Limited. All rights reserved.

option(CMAKE_ENABLE_LTO     "Enable LTO"        ON)
option(CMAKE_ENABLE_SEC_GC  "Enable section GC" ON)
option(CMAKE_USE_LLD        "Enable LLD linker" OFF)

message(STATUS "Building with Enable LTO: ${CMAKE_ENABLE_LTO}")
message(STATUS "Building with Enable GC sections: ${CMAKE_ENABLE_SEC_GC}")

if (CMAKE_ENABLE_LTO)
    set(CMAKE_C_FLAGS           "${CMAKE_C_FLAGS} -flto ${CMAKE_ENABLE_LTO_ADDITIONAL_C_FLAGS}")
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -flto ${CMAKE_ENABLE_LTO_ADDITIONAL_C_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -flto ${CMAKE_ENABLE_LTO_ADDITIONAL_LINKER_FLAGS}")

    if (CMAKE_USE_LLD)
        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
    elseif (NOT DEFINED ANDROID_LD)
        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
    endif()
endif()

if (CMAKE_ENABLE_SEC_GC)
    set(CMAKE_C_FLAGS           "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif()

