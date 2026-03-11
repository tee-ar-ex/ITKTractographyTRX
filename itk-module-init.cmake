set(_TractographyTRX_CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
list(PREPEND CMAKE_MODULE_PATH "${_TractographyTRX_CMAKE_MODULE_PATH}")

option(TractographyTRX_FETCH_TRX_CPP "Fetch trx-cpp if not found" ON)
set(TRX_CPP_GIT_TAG "main" CACHE STRING "trx-cpp git tag")

find_package(trx-cpp QUIET)
if(trx-cpp_FOUND)
  message(STATUS "Trx-cpp found via find_package. trx-cpp_DIR=${trx-cpp_DIR}")
  if(TARGET trx-cpp::trx)
    get_target_property(_trx_cpp_location trx-cpp::trx IMPORTED_LOCATION)
    if(_trx_cpp_location)
      message(STATUS "Trx-cpp imported location: ${_trx_cpp_location}")
    endif()
  endif()
endif()
if(NOT trx-cpp_FOUND)
  if(TractographyTRX_FETCH_TRX_CPP)
    set(TRX_BUILD_TESTS OFF)
    set(TRX_BUILD_EXAMPLES OFF)
    set(TRX_BUILD_BENCHMARKS OFF)
    if(CMAKE_VERSION VERSION_LESS 3.11)
      message(FATAL_ERROR "trx-cpp not found and CMake < 3.11 cannot fetch it. Set trx-cpp_DIR or update CMake.")
    endif()
    include(FetchContent)
    # When building as an ITK module, ITKZLIB_LIBRARIES and ITKZLIB_INCLUDE_DIRS
    # are set by ITK's ZLIB module (an implicit dependency via ITKCommon).
    # ITKZLIB_INCLUDE_DIRS already contains both the source and binary dirs, so
    # zconf.h (generated into the binary dir) is covered automatically.
    if(ITKZLIB_LIBRARIES AND NOT ZLIB_FOUND)
      set(ZLIB_LIBRARY "${ITKZLIB_LIBRARIES}" CACHE STRING "ZLIB library" FORCE)
      set(ZLIB_INCLUDE_DIR "${ITKZLIB_INCLUDE_DIRS}" CACHE STRING "ZLIB include dirs" FORCE)
      set(ZLIB_FOUND TRUE)
    endif()
    if(NOT ZLIB_FOUND)
      find_package(ZLIB QUIET)
    endif()
    if(NOT ZLIB_FOUND)
      message(STATUS "ZLIB not found via ITK; fetching v1.3.1")
      set(SKIP_INSTALL_ALL ON)
      set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
      set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
      )
      FetchContent_MakeAvailable(zlib)
      set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS} CACHE BOOL "" FORCE)
      unset(_saved_BUILD_SHARED_LIBS)
      if(TARGET zlibstatic)
        set(ZLIB_LIBRARY zlibstatic CACHE STRING "ZLIB library target" FORCE)
      elseif(TARGET zlib)
        set(ZLIB_LIBRARY zlib CACHE STRING "ZLIB library target" FORCE)
      endif()
      if(zlib_SOURCE_DIR)
        # zlib.h lives in the source dir; zconf.h is generated into the binary
        # dir. Both dirs must be on the include path for consumers (e.g. libzip).
        if(zlib_BINARY_DIR)
          set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}" CACHE STRING "ZLIB include dirs" FORCE)
        else()
          set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "ZLIB include dir" FORCE)
        endif()
      endif()
    endif()
    find_package(libzip QUIET)
    if(NOT libzip_FOUND)
      message(STATUS "libzip not found; fetching v1.11.4")
      set(LIBZIP_DO_INSTALL OFF)
      set(BUILD_TOOLS OFF)
      set(BUILD_REGRESS OFF)
      set(BUILD_EXAMPLES OFF)
      set(BUILD_DOC OFF)
      # TRX files only use deflate; disable optional codecs to avoid pulling
      # in system bzip2/lzma/zstd as undeclared link dependencies.
      set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
      set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
      set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
      )
      set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
      set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
      FetchContent_MakeAvailable(libzip)
      set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS} CACHE BOOL "" FORCE)
      unset(_saved_BUILD_SHARED_LIBS)
      if(TARGET zip)
        set_target_properties(zip PROPERTIES POSITION_INDEPENDENT_CODE ON)
      endif()
      # Expose the FetchContent build dir as libzip_DIR so that subsequent
      # find_package(libzip) calls (e.g. from trx-cpp) resolve to this version
      # rather than a system installation.
      FetchContent_GetProperties(libzip BINARY_DIR _libzip_binary_dir)
      set(libzip_DIR "${_libzip_binary_dir}" CACHE PATH "FetchContent libzip build dir" FORCE)
      unset(_libzip_binary_dir)
    endif()
    # Create Eigen3::Eigen before trx-cpp needs it.
    # Follow the pattern documented in ITKEigen3/CMakeLists.txt: when ITK uses
    # its bundled Eigen3, ITKInternalEigen3_DIR points to an Eigen3Config.cmake
    # that exposes <Eigen/Core> includes (unlike the ITKEigen3 target itself
    # which uses <itkeigen/Eigen/Core>).
    if(NOT TARGET Eigen3::Eigen AND DEFINED ITKInternalEigen3_DIR)
      set(Eigen3_DIR "${ITKInternalEigen3_DIR}")
      find_package(Eigen3 QUIET CONFIG)
    endif()
    # Fallback for standalone builds or ITK_USE_SYSTEM_EIGEN=ON (where
    # Eigen3::Eigen is already defined by ITK's own find_package call).
    find_package(Eigen3 QUIET)
    message(STATUS "trx-cpp not found; fetching ${TRX_CPP_GIT_TAG}")
    FetchContent_Declare(
      trx_cpp
      GIT_REPOSITORY https://github.com/tee-ar-ex/trx-cpp.git
      GIT_TAG ${TRX_CPP_GIT_TAG}
    )
    set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(trx_cpp)
    set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})
    unset(_saved_BUILD_SHARED_LIBS)

    # Static TractographyTRX links against trx; keep fetched concrete targets in
    # ITKTargets when they are built in-tree so export generation remains valid.
    if(NOT _TractographyTRX_fetched_targets_installed)
      set(_TractographyTRX_install_archive_dir "${ITK_INSTALL_ARCHIVE_DIR}")
      set(_TractographyTRX_install_library_dir "${ITK_INSTALL_LIBRARY_DIR}")
      set(_TractographyTRX_install_runtime_dir "${ITK_INSTALL_RUNTIME_DIR}")
      set(_TractographyTRX_did_install_fetched_targets FALSE)
      if(NOT _TractographyTRX_install_archive_dir)
        set(_TractographyTRX_install_archive_dir "lib")
      endif()
      if(NOT _TractographyTRX_install_library_dir)
        set(_TractographyTRX_install_library_dir "lib")
      endif()
      if(NOT _TractographyTRX_install_runtime_dir)
        set(_TractographyTRX_install_runtime_dir "bin")
      endif()
      foreach(_fetched_target trx zip)
        if(TARGET ${_fetched_target})
          get_target_property(_aliased ${_fetched_target} ALIASED_TARGET)
          get_target_property(_imported ${_fetched_target} IMPORTED)
          if(NOT _aliased AND NOT _imported)
            install(TARGETS ${_fetched_target}
              EXPORT ITKTargets
              ARCHIVE DESTINATION ${_TractographyTRX_install_archive_dir}
              LIBRARY DESTINATION ${_TractographyTRX_install_library_dir}
              RUNTIME DESTINATION ${_TractographyTRX_install_runtime_dir}
            )
            set(_TractographyTRX_did_install_fetched_targets TRUE)
          endif()
          unset(_aliased)
          unset(_imported)
        endif()
      endforeach()
      if(_TractographyTRX_did_install_fetched_targets)
        set(_TractographyTRX_fetched_targets_installed TRUE CACHE INTERNAL "")
      endif()
      unset(_fetched_target)
      unset(_TractographyTRX_did_install_fetched_targets)
      unset(_TractographyTRX_install_archive_dir)
      unset(_TractographyTRX_install_library_dir)
      unset(_TractographyTRX_install_runtime_dir)
    endif()
  else()
    find_package(trx-cpp REQUIRED)
  endif()
endif()

set(TractographyTRX_EXPORT_CODE_COMMON [=[
# Restore non-ITK third-party targets for downstream consumers.
# Keep the dependency namespaced (trx-cpp::trx) to avoid exporting a global
# bare `trx` target from TractographyTRX.
if(NOT TARGET trx-cpp::trx)
  find_package(trx-cpp QUIET CONFIG)
endif()

# Fallback for build-tree consumption where only libtrx exists.
if(NOT TARGET trx-cpp::trx)
  find_library(_TractographyTRX_trx_library
    NAMES trx
    PATHS "${ITK_DIR}/lib"
    NO_DEFAULT_PATH
  )
  if(_TractographyTRX_trx_library)
    add_library(trx-cpp::trx UNKNOWN IMPORTED)
    set_target_properties(trx-cpp::trx PROPERTIES
      IMPORTED_LOCATION "${_TractographyTRX_trx_library}"
    )
  endif()
  unset(_TractographyTRX_trx_library CACHE)
  unset(_TractographyTRX_trx_library)
endif()

# Normalize libzip target aliases expected by trx-cpp across environments.
if(NOT TARGET libzip::zip)
  if(TARGET zip::zip)
    add_library(libzip::zip INTERFACE IMPORTED)
    set_target_properties(libzip::zip PROPERTIES
      INTERFACE_LINK_LIBRARIES "zip::zip"
    )
  elseif(TARGET zip)
    add_library(libzip::zip INTERFACE IMPORTED)
    set_target_properties(libzip::zip PROPERTIES
      INTERFACE_LINK_LIBRARIES "zip"
    )
  else()
    find_package(libzip QUIET CONFIG)
    if(NOT TARGET libzip::zip AND NOT TARGET zip::zip AND NOT TARGET zip)
      find_library(_TractographyTRX_libzip_library NAMES zip libzip)
      find_path(_TractographyTRX_libzip_include_dir zip.h)
      if(_TractographyTRX_libzip_library)
        add_library(libzip::zip UNKNOWN IMPORTED)
        set_target_properties(libzip::zip PROPERTIES
          IMPORTED_LOCATION "${_TractographyTRX_libzip_library}"
        )
        if(_TractographyTRX_libzip_include_dir)
          set_target_properties(libzip::zip PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_TractographyTRX_libzip_include_dir}"
          )
        endif()
      endif()
      unset(_TractographyTRX_libzip_library CACHE)
      unset(_TractographyTRX_libzip_include_dir CACHE)
      unset(_TractographyTRX_libzip_library)
      unset(_TractographyTRX_libzip_include_dir)
    elseif(NOT TARGET libzip::zip AND TARGET zip::zip)
      add_library(libzip::zip INTERFACE IMPORTED)
      set_target_properties(libzip::zip PROPERTIES
        INTERFACE_LINK_LIBRARIES "zip::zip"
      )
    elseif(NOT TARGET libzip::zip AND TARGET zip)
      add_library(libzip::zip INTERFACE IMPORTED)
      set_target_properties(libzip::zip PROPERTIES
        INTERFACE_LINK_LIBRARIES "zip"
      )
    endif()
  endif()
endif()

if(NOT TARGET zip::zip AND TARGET libzip::zip)
  add_library(zip::zip INTERFACE IMPORTED)
  set_target_properties(zip::zip PROPERTIES
    INTERFACE_LINK_LIBRARIES "libzip::zip"
  )
endif()

if(NOT TARGET zip AND TARGET libzip::zip)
  add_library(zip INTERFACE IMPORTED)
  set_target_properties(zip PROPERTIES
    INTERFACE_LINK_LIBRARIES "libzip::zip"
  )
endif()
]=])

# Keep build-tree and install-tree dependency restoration aligned.
set(TractographyTRX_EXPORT_CODE_BUILD "${TractographyTRX_EXPORT_CODE_COMMON}")
set(TractographyTRX_EXPORT_CODE_INSTALL "${TractographyTRX_EXPORT_CODE_COMMON}")
