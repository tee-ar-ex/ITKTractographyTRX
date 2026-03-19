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
    # Pre-set trx-cpp dependency targets so it skips its own find_package calls
    # and uses the ITK-provided targets instead.
    # TRX_EIGEN3_TARGET: ITK::ITKEigen3Module is the public wrapper (post-PR#5831).
    set(TRX_EIGEN3_TARGET "ITK::ITKEigen3Module")
    # TRX_ZLIB_TARGET: select the ZLIB target visible in FetchContent child scopes.
    # In an in-tree ITK build (e.g. ANTs superbuild), ITKZLIBModule is a regular
    # globally-visible target; ITK::ITKZLIBModule is a directory-scoped ALIAS
    # created inside ITKZLIB's add_subdirectory and is not accessible outside it.
    # Against an installed ITK, find_package(ITK) creates ITK::ITKZLIBModule as
    # an IMPORTED target that IS visible in subdirectories including FetchContent.
    if(TARGET ITKZLIBModule)
      set(TRX_ZLIB_TARGET "ITKZLIBModule")
    else()
      set(TRX_ZLIB_TARGET "ITK::ITKZLIBModule")
    endif()
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
    if(TARGET trx)
      set_target_properties(trx PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # Keep fetched concrete targets in ITKTargets so export generation remains
    # valid on every cmake run.  Use a GLOBAL PROPERTY as the guard: unlike a
    # CACHE variable it resets each cmake run (preventing the "not in export
    # set" error on re-configure), but survives within a single run (preventing
    # the "included more than once" error when ITK processes this file twice).
    get_property(_trx_already_installed GLOBAL PROPERTY TractographyTRX_fetched_targets_installed SET)
    if(_trx_already_installed)
      unset(_trx_already_installed)
      return()
    endif()
    set_property(GLOBAL PROPERTY TractographyTRX_fetched_targets_installed TRUE)
    unset(_trx_already_installed)
    set(_TractographyTRX_install_archive_dir "${ITK_INSTALL_ARCHIVE_DIR}")
    set(_TractographyTRX_install_library_dir "${ITK_INSTALL_LIBRARY_DIR}")
    set(_TractographyTRX_install_runtime_dir "${ITK_INSTALL_RUNTIME_DIR}")
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
        endif()
        unset(_aliased)
        unset(_imported)
      endif()
    endforeach()
    unset(_fetched_target)
    unset(_TractographyTRX_install_archive_dir)
    unset(_TractographyTRX_install_library_dir)
    unset(_TractographyTRX_install_runtime_dir)
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
