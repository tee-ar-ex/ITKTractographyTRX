option(TractographyTRX_FETCH_TRX_CPP "Fetch trx-cpp if not found" ON)
set(TRX_CPP_GIT_TAG "a6a523642267cc1208851eb1b1a79a4896dde1da" CACHE STRING "trx-cpp git tag")

find_package(trx-cpp QUIET)
if(trx-cpp_FOUND)
  message(STATUS "Trx-cpp found via find_package. trx-cpp_DIR=${trx-cpp_DIR}")
endif()
if(NOT trx-cpp_FOUND)
  if(TractographyTRX_FETCH_TRX_CPP)
    set(TRX_BUILD_TESTS OFF)
    set(TRX_BUILD_EXAMPLES OFF)
    set(TRX_BUILD_BENCHMARKS OFF)
    include(FetchContent)

    # Pass ITK-provided Eigen and ZLIB targets so trx-cpp skips its own
    # find_package calls and uses them directly.
    set(TRX_EIGEN3_TARGET "ITK::ITKEigen3Module")

    if(TARGET ITKZLIBModule)
      set(TRX_ZLIB_TARGET "ITKZLIBModule")
    else()
      set(TRX_ZLIB_TARGET "ITK::ITKZLIBModule")
    endif()

    # Bridge ITK's ZLIB to the standard ZLIB::ZLIB target so that libzip's
    # find_package(ZLIB REQUIRED) finds it when trx-cpp fetches libzip.
    if(NOT TARGET ZLIB::ZLIB)
      add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
      set_target_properties(ZLIB::ZLIB PROPERTIES
        INTERFACE_LINK_LIBRARIES "${TRX_ZLIB_TARGET}"
      )
    endif()

    # Pre-populate ZLIB cache variables so FindZLIB succeeds without zlib.h
    # on disk. In a fresh superbuild, zlib.h is generated into the ITK build
    # tree only after the ZLIB build step runs — but cmake configure needs
    # find_package(ZLIB 1.1.2 REQUIRED) to pass first. Setting these cache
    # vars satisfies FindZLIB's version check; the ZLIB::ZLIB target above
    # already exists so FindZLIB skips target creation.
    itk_module_load(ITKZLIB)
    list(GET ITKZLIB_INCLUDE_DIRS 0 _trx_itkzlib_include_dir)
    set(ZLIB_INCLUDE_DIR "${_trx_itkzlib_include_dir}" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY "ZLIB::ZLIB" CACHE STRING "" FORCE)
    set(ZLIB_VERSION_STRING "1.3.0" CACHE STRING "" FORCE)
    unset(_trx_itkzlib_include_dir)

    message(STATUS "TractographyTRX ZLIB bridge: ZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}")
    message(STATUS "TractographyTRX ZLIB bridge: ZLIB::ZLIB -> ${TRX_ZLIB_TARGET}")

    # Fetch trx-cpp. It will fetch its own libzip, whose find_package(ZLIB)
    # will find our pre-populated cache vars and ZLIB::ZLIB target above.
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
    if(TARGET zip)
      set_target_properties(zip PROPERTIES POSITION_INDEPENDENT_CODE ON)
      # Replace zip's ZLIB::ZLIB link dependency with the real ITK zlib target.
      # ZLIB::ZLIB is our bridge target for configure-time find_package(ZLIB),
      # but it won't exist when a downstream project loads the installed
      # ITKTargets.cmake. The ITK zlib target (ITKZLIBModule) IS in the export
      # set and resolves correctly after installation.
      foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_libs zip ${_prop})
        if(_libs)
          string(REPLACE "ZLIB::ZLIB" "${TRX_ZLIB_TARGET}" _libs "${_libs}")
          set_target_properties(zip PROPERTIES ${_prop} "${_libs}")
        endif()
      endforeach()
      unset(_libs)
      unset(_prop)
    endif()

    # Install fetched targets into ITKTargets so the export set is complete.
    # Guard with a GLOBAL PROPERTY: resets each cmake run (preventing stale
    # "not in export set" errors) but survives within a run (preventing
    # duplicate-install errors if this file is processed twice).
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

# Export code: restore trx-cpp target for downstream consumers (e.g. ANTs)
# that find_package(ITK COMPONENTS TractographyTRX). The 'trx' and 'zip'
# targets are in ITKTargets already; we just need the trx-cpp::trx alias.
set(TractographyTRX_EXPORT_CODE_COMMON [=[

if(NOT TARGET trx-cpp::trx)
  find_package(trx-cpp QUIET CONFIG)
endif()

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
]=])

set(TractographyTRX_EXPORT_CODE_BUILD "${TractographyTRX_EXPORT_CODE_COMMON}")
set(TractographyTRX_EXPORT_CODE_INSTALL "${TractographyTRX_EXPORT_CODE_COMMON}")
