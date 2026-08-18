cmake_minimum_required(VERSION 3.21)
project(chuniboard_chunithm_server LANGUAGES CXX)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}/include/")

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release)
ENDIF()

SET(CMAKE_CXX_STANDARD 17)

IF(NOT WIN32)
    MESSAGE(FATAL_ERROR "This is a Windows-only project.")
ENDIF()

IF(NOT MSVC)
    ADD_COMPILE_OPTIONS(-Wall -Wextra -Wno-unused-parameter -Wno-unused-result)
ELSE()
    ADD_COMPILE_OPTIONS(/W4 /D_CRT_SECURE_NO_WARNINGS)
    # Statically link CRT – no ucrtbased.dll / VCRUNTIME140D.dll required
    SET(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
ENDIF()

# ── Server executable ─────────────────────────────────────────────────────────
ADD_EXECUTABLE(chunithm_server
    src/main.cpp
    src/resources.rc)
INCLUDE_DIRECTORIES(src)
LINK_DIRECTORIES(${CMAKE_SOURCE_DIR})
TARGET_LINK_LIBRARIES(chunithm_server ws2_32)

# ── chuniio hook DLL (segatools path32=) ─────────────────────────────────────
ADD_SUBDIRECTORY(segatools/chuniio)

# ── aimeio hook DLL (segatools aimeio=) ──────────────────────────────────────
ADD_SUBDIRECTORY(segatools/aimeio)
