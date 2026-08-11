cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED DSP_MAJOR)
	set(DSP_MAJOR 0)
	set(DSP_MINOR 10)
	set(DSP_PATCH 0)
endif()

project(dsp
	DESCRIPTION "Header-only mixed-precision DSP library"
	VERSION "${DSP_MAJOR}.${DSP_MINOR}.${DSP_PATCH}"
	LANGUAGES CXX
	HOMEPAGE_URL "https://github.com/stillwater-sc/dsp"
)

set(PROJECT_VERSION_MAJOR ${DSP_MAJOR})
set(PROJECT_VERSION_MINOR ${DSP_MINOR})
set(PROJECT_VERSION_PATCH ${DSP_PATCH})

# Enable folder organization in IDE generators (Visual Studio, Xcode).
# Each test/application sets a FOLDER target property so the IDE's
# Solution Explorer / project tree shows a hierarchical view that mirrors
# the include/sw/dsp/ module layout. Makefile / Ninja generators ignore
# the FOLDER property, so this is a no-op for command-line builds.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Header-only interface library
add_library(${PROJECT_NAME} INTERFACE)
add_library(sw::dsp ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

target_include_directories(${PROJECT_NAME} INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

####
# Dependencies
#
# Strategy: try find_package first (for system/installed copies),
# then fall back to FetchContent from GitHub (headers only).
include(FetchContent)

# Helper: fetch a header-only dependency from GitHub WITHOUT calling
# add_subdirectory on its CMakeLists.txt (which would build its tests,
# examples, and other targets we don't need).
#
# The classic idiom — FetchContent_GetProperties + manual
# FetchContent_Populate — is deprecated as of CMake 3.30 (CMP0169).
# The forward-compatible replacement is FetchContent_MakeAvailable with
# SOURCE_SUBDIR pointing at a non-existent subdirectory: MakeAvailable
# only calls add_subdirectory when the subdir contains a CMakeLists.txt,
# so a path that doesn't exist gives us populate-without-build.
#
# Implemented as a macro (not function) so the `<name>_SOURCE_DIR`
# variable that FetchContent_MakeAvailable sets ends up in the caller's
# scope where the subsequent target_include_directories() can read it.
# CMake functions introduce their own scope, which would swallow the
# variable assignment. Universal's compile_all() macro uses the same
# trick (and same rationale).
#
# Caller is responsible for setting up include directories from the
# resulting <name>_SOURCE_DIR, since each dep has its own include-path
# layout.
macro(dsp_fetch_headers_only dep_name dep_repo dep_tag)
	FetchContent_Declare(${dep_name}
		GIT_REPOSITORY ${dep_repo}
		GIT_TAG        ${dep_tag}
		GIT_SHALLOW    TRUE
		SOURCE_SUBDIR  _do_not_build_
	)
	FetchContent_MakeAvailable(${dep_name})
endmacro()

# Universal number arithmetic library (header-only)
find_package(universal CONFIG QUIET)
if(universal_FOUND)
	target_link_libraries(${PROJECT_NAME} INTERFACE universal::universal)
	message(STATUS "Found Universal via find_package")
else()
	message(STATUS "Universal not found via find_package, fetching headers from GitHub")
	dsp_fetch_headers_only(universal
		https://github.com/stillwater-sc/universal.git
		v4.6.10)
	# Universal headers use two include conventions:
	#   - External: #include <sw/universal/...>  (needs include/)
	#   - Internal: #include <universal/...>     (needs include/sw/)
	# SYSTEM suppresses upstream warnings (MSVC C4244 narrowing in
	# posit_impl.hpp, etc.) — those are upstream's responsibility, not
	# ours, and they drown out our own diagnostics in MSVC build logs.
	target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE
		$<BUILD_INTERFACE:${universal_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${universal_SOURCE_DIR}/include/sw>)
endif()

# MTL5 linear algebra library (header-only)
find_package(MTL5 CONFIG QUIET)
if(MTL5_FOUND)
	target_link_libraries(${PROJECT_NAME} INTERFACE MTL5::mtl5)
	message(STATUS "Found MTL5 via find_package")
else()
	message(STATUS "MTL5 not found via find_package, fetching headers from GitHub")
	# Pinned to a tagged MTL5 release. v5.3.0+ includes
	# `mtl/operation/ldlt.hpp` (used by `include/sw/dsp/estimation/ukf.hpp`),
	# which landed post-v5.2.0.
	dsp_fetch_headers_only(mtl5
		https://github.com/stillwater-sc/mtl5.git
		v5.5.0)
	# SYSTEM: see Universal block above for rationale.
	target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE
		$<BUILD_INTERFACE:${mtl5_SOURCE_DIR}/include>)
endif()

####
# Compiler-specific settings
target_compile_options(${PROJECT_NAME} INTERFACE
	$<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
	$<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
	$<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
	$<$<CXX_COMPILER_ID:AppleClang>:-Wall -Wextra -Wpedantic>
)

####
# Build options
option(DSP_BUILD_APPLICATIONS      "Build demonstration applications"                ON)
option(DSP_BUILD_TESTS             "Build unit tests"                                ON)
option(DSP_BUILD_REGRESSION_TESTS  "Build large-scale regression tests (slow)"       OFF)

if(DSP_BUILD_APPLICATIONS)
	add_subdirectory(applications)
endif()

if(DSP_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

####
# Install
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
	EXPORT dspTargets
)

install(DIRECTORY include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT dspTargets
	FILE dspTargets.cmake
	NAMESPACE sw::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsp
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/dspConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/dspConfig.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsp
)

write_basic_package_version_file(
	${CMAKE_CURRENT_BINARY_DIR}/dspConfigVersion.cmake
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY SameMajorVersion
)

install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/dspConfig.cmake
	${CMAKE_CURRENT_BINARY_DIR}/dspConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsp
)
