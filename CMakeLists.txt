# libgit2: the cross-platform, linkable library implementation of git.
# See `README.md` for build instructions.
#
# This top-level CMakeLists.txt sets up configuration options and
# determines which subprojects to build.

cmake_minimum_required(VERSION 3.5.1)

project(libgit2 VERSION "1.9.0" LANGUAGES C)

# Add find modules to the path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

#
# Build options
#

# Experimental features
option(EXPERIMENTAL_SHA256     "Enable experimental SHA256 support (for R&D/testing)"  OFF)

# Optional subsystems
option(BUILD_SHARED_LIBS       "Build Shared Library (OFF for Static)"                  ON)
option(BUILD_TESTS             "Build Tests using the Clar suite"                       ON)
option(BUILD_CLI               "Build the command-line interface"                       ON)
option(BUILD_EXAMPLES          "Build library usage example apps"                      OFF)
option(BUILD_FUZZERS           "Build the fuzz targets"                                OFF)

# WASM-specific configuration
if(EMSCRIPTEN)
   option(GIT_IO_WASM             "Enable WASM I/O patterns"                               ON)
   option(GIT_FS_VIRTUAL          "Enable virtual filesystem support"                     ON)
   option(ENABLE_SIMD             "Enable SIMD optimizations for WASM"                   OFF)
   
   # WASM-specific defaults
   set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Shared Library (OFF for WASM)" FORCE)
   set(BUILD_TESTS OFF CACHE BOOL "Build Tests (disabled for WASM)" FORCE)
   set(BUILD_CLI OFF CACHE BOOL "Build CLI (disabled for WASM)" FORCE)
   set(BUILD_EXAMPLES OFF CACHE BOOL "Build Examples (disabled for WASM)" FORCE)
   set(BUILD_FUZZERS OFF CACHE BOOL "Build Fuzzers (disabled for WASM)" FORCE)
   
   # WASM-compatible backend selection
   set(USE_SSH OFF CACHE STRING "SSH disabled for WASM" FORCE)
   set(USE_HTTPS "OpenSSL-Dynamic" CACHE STRING "HTTPS via OpenSSL.wasm")
   set(USE_SHA1 "HTTPS" CACHE STRING "SHA1 via OpenSSL for SIMD potential")
   set(USE_SHA256 "HTTPS" CACHE STRING "SHA256 via OpenSSL for SIMD potential")
   set(USE_HTTP_PARSER "builtin" CACHE STRING "Builtin HTTP parser")
   set(USE_REGEX "builtin" CACHE STRING "Builtin regex")
   set(USE_COMPRESSION "builtin" CACHE STRING "Builtin compression")
   
   # Configure Emscripten flags
   set(EMSCRIPTEN_BASE_FLAGS 
     "-sALLOW_MEMORY_GROWTH=1"
     "-sINITIAL_MEMORY=67108864"
     "-sFORCE_FILESYSTEM=1"
     "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','PATH','cwrap','ccall']"
     "-sWASM_ASYNC_COMPILATION=0"
     "-sNO_EXIT_RUNTIME=1"
     "-sENVIRONMENT=web,worker"
     "-sMODULARIZE=1"
     "-sEXPORT_NAME=LibGit2Module"
   )
   
   if(GIT_FS_VIRTUAL)
     list(APPEND EMSCRIPTEN_BASE_FLAGS "-sFS_DEBUG=0")
     add_definitions(-DGIT_FS_VIRTUAL=1)
   endif()
   
   if(GIT_IO_WASM)
     add_definitions(-DGIT_IO_WASM=1)
   endif()
   
   if(ENABLE_SIMD)
     list(APPEND EMSCRIPTEN_BASE_FLAGS "-msimd128")
     add_definitions(-DGIT_WASM_SIMD=1)
     
     # Add SIMD source files
     file(GLOB WASM_SIMD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/wasm/*simd*.c")
     if(WASM_SIMD_SOURCES)
       target_sources(git2 PRIVATE ${WASM_SIMD_SOURCES})
     endif()
   endif()
   
   if(USE_THREADS STREQUAL "pthreads")
     list(APPEND EMSCRIPTEN_BASE_FLAGS "-pthread" "-sPTHREAD_POOL_SIZE=4" "-sPROXY_TO_PTHREAD")
     add_definitions(-DUSE_THREADS=1)
     
     # Add threading source files
     file(GLOB WASM_THREADING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/wasm/*threading*.c")
     if(WASM_THREADING_SOURCES)
       target_sources(git2 PRIVATE ${WASM_THREADING_SOURCES})
     endif()
   endif()
   
   string(REPLACE ";" " " EMSCRIPTEN_FLAGS_STR "${EMSCRIPTEN_BASE_FLAGS}")
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EMSCRIPTEN_FLAGS_STR}")
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EMSCRIPTEN_FLAGS_STR}")
   
endif()

# Feature enablement and backend selection
   set(USE_THREADS             "" CACHE STRING "Use threads for parallel processing when possible. One of ON, OFF, or a specific provider: pthreads or win32. (Defaults to ON.)")
   set(USE_SSH                 "" CACHE STRING "Enables SSH support and optionally selects provider. One of ON, OFF, or a specific provider: libssh2 or exec. (Defaults to OFF.)")
   set(USE_HTTPS               "" CACHE STRING "Enable HTTPS support and optionally selects the provider. One of ON, OFF, or a specific provider: OpenSSL, OpenSSL-FIPS, OpenSSL-Dynamic, mbedTLS, SecureTransport, Schannel, or WinHTTP. (Defaults to ON.)")
   set(USE_SHA1                "" CACHE STRING "Selects SHA1 provider. One of builtin, HTTPS, or a specific provider. (Defaults to builtin.)")
   set(USE_SHA256              "" CACHE STRING "Selects SHA256 provider. One of Builtin, HTTPS, or a specific provider. (Defaults to HTTPS.)")
   set(USE_HTTP_PARSER         "" CACHE STRING "Selects HTTP Parser support: http-parser, llhttp, or builtin. (Defaults to builtin.)")
   set(USE_AUTH_NTLM           "" CACHE STRING "Enables NTLM authentication support. One of Builtin or win32.")
   set(USE_AUTH_NEGOTIATE      "" CACHE STRING "Enable Negotiate (SPNEGO) authentication support. One of GSSAPI or win32.")
#  set(USE_XDIFF               "" CACHE STRING "Specifies the xdiff implementation; either system or builtin.")
   set(USE_REGEX               "" CACHE STRING "Selects regex provider. One of regcomp_l, pcre2, pcre, regcomp, or builtin.")
   set(USE_COMPRESSION         "" CACHE STRING "Selects compression backend. Either builtin or zlib.")
   set(USE_NSEC                "" CACHE STRING "Enable nanosecond precision timestamps. One of ON, OFF, or a specific provider: mtimespec, mtim, mtime, or win32. (Defaults to ON).")

if(APPLE)
	# Currently only available on macOS for `precomposeUnicode` support
	set(USE_I18N            "" CACHE STRING "Enables internationalization support.")
endif()

# Debugging options
   set(DEBUG_LEAK_CHECKER      "" CACHE STRING "Configure for leak checking test runs. One of valgrind, leaks, or win32. Either valgrind or leaks.")
option(USE_STANDALONE_FUZZERS  "Enable standalone fuzzers (compatible with gcc)"       OFF)
option(DEBUG_POOL              "Enable debug pool allocator"                           OFF)
option(DEBUG_STRICT_ALLOC      "Enable strict allocator behavior"                      OFF)
option(DEBUG_STRICT_OPEN       "Enable path validation in open"                        OFF)

# Output options
option(SONAME                  "Set the (SO)VERSION of the target"                      ON)
   set(LIBGIT2_FILENAME        "git2" CACHE STRING "Name of the produced binary")
option(DEPRECATE_HARD          "Do not include deprecated functions in the library"    OFF)

# Compilation options
# Default to c99 on Android Studio for compatibility; c90 everywhere else
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Android")
   set(CMAKE_C_STANDARD        "99" CACHE STRING "The C standard to compile against")
else()
   set(CMAKE_C_STANDARD        "90" CACHE STRING "The C standard to compile against")
endif()
option(CMAKE_C_EXTENSIONS      "Whether compiler extensions are supported"             OFF)
option(ENABLE_WERROR           "Enable compilation with -Werror"                       OFF)

if(UNIX)
	option(ENABLE_REPRODUCIBLE_BUILDS "Enable reproducible builds"                 OFF)
endif()

if(MSVC)
	# This option must match the settings used in your program, in particular if you
	# are linking statically
	option(STATIC_CRT          "Link the static CRT libraries"                      ON)
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()


# Modules

include(FeatureSummary)
include(CheckLibraryExists)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckPrototypeDefinitionSafe)
include(AddCFlagIfSupported)
include(FindPkgLibraries)
include(FindThreads)
include(FindStatNsec)
include(Findfutimens)
include(GNUInstallDirs)
include(IdeSplitSources)
include(EnableWarnings)
include(DefaultCFlags)
include(ExperimentalFeatures)


#
# Subdirectories
#

add_subdirectory(src)

if(BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

if(BUILD_FUZZERS)
	if((BUILD_TESTS OR BUILD_EXAMPLES) AND NOT USE_STANDALONE_FUZZERS)
		message(FATAL_ERROR "Cannot build the fuzzer and the tests or examples together")
	endif()
	add_subdirectory(fuzzers)
endif()


# Export for people who use us as a dependency

if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
	set(LIBGIT2_DEPENDENCY_OBJECTS ${LIBGIT2_DEPENDENCY_OBJECTS} PARENT_SCOPE)
	set(LIBGIT2_SYSTEM_LIBS ${LIBGIT2_SYSTEM_LIBS} PARENT_SCOPE)
endif()


# Summary

feature_summary(WHAT ENABLED_FEATURES DESCRIPTION "Enabled features:")
feature_summary(WHAT DISABLED_FEATURES DESCRIPTION "Disabled features:")

# warn for not using sha1dc

foreach(WARNING ${WARNINGS})
	message(WARNING ${WARNING})
endforeach()
