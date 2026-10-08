# SPDX-FileCopyrightText: 2026 Edward Kmett <ekmett@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause OR Apache-2.0
cmake_minimum_required(VERSION 4.4)
project(work VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
if(NOT TARGET hint::hint)
  FetchContent_Declare(hint GIT_REPOSITORY https://github.com/ekmett/hint.git
    GIT_TAG 9685ad404b57075a8e91fa3deef9c3fd759b7237 FIND_PACKAGE_ARGS 0.1 CONFIG)
  FetchContent_MakeAvailable(hint)
endif()
find_package(Threads REQUIRED)
add_library(work STATIC)
add_library(work::work ALIAS work)
target_compile_features(work PUBLIC cxx_std_26)
target_sources(work PUBLIC FILE_SET modules TYPE CXX_MODULES
  BASE_DIRS src FILES src/work.ccm)
target_link_libraries(work PUBLIC hint::hint Threads::Threads)
set_target_properties(work PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # CMake's module rule passes -TP beside an explicit Clang module input mode.
  target_compile_options(work PUBLIC -Wno-unused-command-line-argument)
  target_compile_options(work PRIVATE /W4 /WX)
else()
  target_compile_options(work PRIVATE -Wall -Wextra -Werror)
endif()
option(WORK_BUILD_TESTS "Build work checks" ${PROJECT_IS_TOP_LEVEL})
if(WORK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(t)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS work EXPORT workTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  FILE_SET modules DESTINATION "${CMAKE_INSTALL_LIBDIR}/work/modules")
install(EXPORT workTargets NAMESPACE work:: CXX_MODULES_DIRECTORY cxx-modules
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/work")
configure_package_config_file(etc/cmake/workConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/workConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/work")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/workConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/workConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/workConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/work")
install(FILES LICENSE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/work")
