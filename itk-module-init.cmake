option(TractographyTRX_FETCH_TRX_CPP "Fetch trx-cpp if not found" ON)
set(TRX_CPP_GIT_TAG "main" CACHE STRING "trx-cpp git tag")

find_package(trx-cpp QUIET)
if(NOT trx-cpp_FOUND)
  if(TractographyTRX_FETCH_TRX_CPP)
    if(NOT DEFINED TRX_BUILD_TESTS)
      set(TRX_BUILD_TESTS OFF CACHE BOOL "Build trx-cpp tests")
    endif()
    if(NOT DEFINED TRX_BUILD_EXAMPLES)
      set(TRX_BUILD_EXAMPLES OFF CACHE BOOL "Build trx-cpp examples")
    endif()
    if(NOT DEFINED TRX_BUILD_BENCHMARKS)
      set(TRX_BUILD_BENCHMARKS OFF CACHE BOOL "Build trx-cpp benchmarks")
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.11)
      message(FATAL_ERROR "trx-cpp not found and CMake < 3.11 cannot fetch it. Set trx-cpp_DIR or update CMake.")
    endif()
    include(FetchContent)
    find_package(libzip QUIET)
    if(NOT libzip_FOUND)
      message(STATUS "libzip not found; fetching v1.11.4")
      if(NOT DEFINED LIBZIP_DO_INSTALL)
        set(LIBZIP_DO_INSTALL ON CACHE BOOL "Enable libzip package config")
      endif()
      if(NOT DEFINED BUILD_TOOLS)
        set(BUILD_TOOLS OFF CACHE BOOL "Disable libzip tools")
      endif()
      if(NOT DEFINED BUILD_REGRESS)
        set(BUILD_REGRESS OFF CACHE BOOL "Disable libzip regression tests")
      endif()
      if(NOT DEFINED BUILD_EXAMPLES)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Disable libzip examples")
      endif()
      if(NOT DEFINED BUILD_DOC)
        set(BUILD_DOC OFF CACHE BOOL "Disable libzip docs")
      endif()
      FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
      )
      FetchContent_MakeAvailable(libzip)
      set(libzip_DIR "${libzip_BINARY_DIR}" CACHE PATH "libzip config path" FORCE)
      list(PREPEND CMAKE_PREFIX_PATH "${libzip_BINARY_DIR}")
    endif()
    message(STATUS "trx-cpp not found; fetching ${TRX_CPP_GIT_TAG}")
    FetchContent_Declare(
      trx_cpp
      GIT_REPOSITORY https://github.com/tee-ar-ex/trx-cpp.git
      GIT_TAG ${TRX_CPP_GIT_TAG}
    )
    FetchContent_MakeAvailable(trx_cpp)
  else()
    find_package(trx-cpp REQUIRED)
  endif()
endif()

# When this module is loaded by an app, load trx-cpp too.
set(TractographyTRX_EXPORT_CODE_INSTALL "
find_package(trx-cpp REQUIRED)
")
set(TractographyTRX_EXPORT_CODE_BUILD "
if(NOT ITK_BINARY_DIR)
  find_package(trx-cpp REQUIRED)
endif()
")
