#===============================================================================
# TMP117 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   tmp117, consumed as tmp117::tmp117 both through add_subdirectory() and
#   through find_package(tmp117) after install.
#
#   cmake -S . -B build -DTMP117_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/tmp117_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${TMP117_SOURCE_FILES}
        INCLUDE_DIRS ${TMP117_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${TMP117_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(TMP117_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(tmp117_driver
    VERSION ${TMP117_VERSION}
    DESCRIPTION "C++20 driver for the Texas Instruments TMP117 high-accuracy I2C digital temperature sensor with alert output and on-chip EEPROM."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because tmp117.hpp includes
# "../src/tmp117.ipp" relative to itself:
#   <prefix>/include/tmp117/inc/*.hpp, tmp117_version.h
#   <prefix>/include/tmp117/src/tmp117.ipp
set(TMP117_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/tmp117/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${TMP117_TARGET_NAME} INTERFACE)
target_include_directories(${TMP117_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${TMP117_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${TMP117_INSTALL_INCLUDEDIR}>
)
target_compile_features(${TMP117_TARGET_NAME} INTERFACE cxx_std_20)

# tmp117::tmp117 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(tmp117::tmp117 ALIAS ${TMP117_TARGET_NAME})
set_target_properties(${TMP117_TARGET_NAME} PROPERTIES EXPORT_NAME tmp117)

option(TMP117_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links tmp117::tmp117" OFF)
if(TMP117_ENABLE_WARNINGS)
    target_compile_options(${TMP117_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(TMP117_BUILD_TESTS "Build the host tests in tests/" OFF)
option(TMP117_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(TMP117_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${TMP117_TARGET_NAME} EXPORT tmp117Targets
    INCLUDES DESTINATION ${TMP117_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${TMP117_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${TMP117_VERSION_HEADER}" DESTINATION ${TMP117_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tmp117/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT tmp117Targets
    FILE tmp117Targets.cmake
    NAMESPACE tmp117::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tmp117)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/tmp117Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tmp117Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tmp117)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tmp117ConfigVersion.cmake"
    VERSION ${TMP117_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tmp117Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tmp117ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tmp117)
