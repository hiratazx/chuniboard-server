cmake_minimum_required(VERSION 3.21)
project(aimeio LANGUAGES C)

IF (NOT WIN32)
    MESSAGE(FATAL_ERROR "This must be built for Windows!")
ENDIF()

SET(CMAKE_C_STANDARD 11)

IF (NOT MSVC)
    ADD_COMPILE_OPTIONS(-Wall -Wextra -Wno-unused-parameter)
ELSE()
    ADD_COMPILE_OPTIONS(/W4)
    SET(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
ENDIF()

# Root of the vendored segatools tree (one level up from this CMakeLists).
SET(SEGATOOLS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# aimeio.dll – the segatools Aime/FeliCa reader plugin
ADD_LIBRARY(aimeio SHARED
    # Core IO implementation
    aimeio.c
    # Utility helpers (vendored from segatools/common/util)
    ../util/dprintf.c
    # Module definition file – exports all aime_io_* symbols by name (no mangling)
    aimeio.def
)

SET_TARGET_PROPERTIES(aimeio PROPERTIES OUTPUT_NAME "aimeio")

TARGET_INCLUDE_DIRECTORIES(aimeio PRIVATE
    ${SEGATOOLS_ROOT}
)
