cmake_minimum_required(VERSION 3.21)
project(chuniio LANGUAGES C)

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
# This makes #include "chuniio/foo.h" and #include "util/bar.h" resolve correctly.
SET(SEGATOOLS_ROOT "${CMAKE_SOURCE_DIR}/..")

# chunihook.dll – the segatools I/O plugin
ADD_LIBRARY(chuniio SHARED
    # Core IO implementation
    chuniio.c
    # Config loader
    config.c
    # LED output backends
    ledoutput.c
    pipeimpl.c
    serialimpl.c
    # CHUNITHM 2→3 protocol adapter
    chu2to3.c
    # Utility helpers (vendored from segatools/common/util)
    ../util/dprintf.c
    ../util/env.c
)

SET_TARGET_PROPERTIES(chuniio PROPERTIES OUTPUT_NAME "chunihook")

TARGET_INCLUDE_DIRECTORIES(chuniio PRIVATE
    ${SEGATOOLS_ROOT}
)

TARGET_LINK_LIBRARIES(chuniio ws2_32)

