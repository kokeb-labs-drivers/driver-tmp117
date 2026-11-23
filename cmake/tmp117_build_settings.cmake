#===============================================================================
# TMP117 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(TMP117_TARGET_NAME "tmp117")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(TMP117_VERSION_MAJOR 0)
set(TMP117_VERSION_MINOR 1)
set(TMP117_VERSION_PATCH 0)
set(TMP117_VERSION "${TMP117_VERSION_MAJOR}.${TMP117_VERSION_MINOR}.${TMP117_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(TMP117_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/tmp117_version.h.in")
set(TMP117_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/tmp117_generated")
set(TMP117_VERSION_HEADER "${TMP117_VERSION_HEADER_DIR}/tmp117_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${TMP117_VERSION_HEADER_DIR}")
configure_file("${TMP117_VERSION_TEMPLATE}" "${TMP117_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(TMP117_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${TMP117_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/tmp117.ipp, included by inc/tmp117.hpp.
set(TMP117_SOURCE_FILES)

set(TMP117_IDF_REQUIRES)
