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
    #
    # ITKZLIB_LIBRARIES may be a CMake target name (e.g. "zlib" for the in-tree
    # bundled build) rather than a file path.  Setting ZLIB_LIBRARY to a target
    # name causes FindZLIB to create ZLIB::ZLIB as UNKNOWN IMPORTED with
    # IMPORTED_LOCATION="zlib", which ninja treats as a missing file rather than
    # a build target.  Pre-create ZLIB::ZLIB as an INTERFACE IMPORTED GLOBAL
    # target instead; FindZLIB's "NOT TARGET ZLIB::ZLIB" guard then skips its
    # own (broken) target creation.
    if(ITKZLIB_LIBRARIES AND NOT ZLIB_FOUND)
      if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
        set_target_properties(ZLIB::ZLIB PROPERTIES
          INTERFACE_LINK_LIBRARIES  "${ITKZLIB_LIBRARIES}"
          INTERFACE_INCLUDE_DIRECTORIES "${ITKZLIB_INCLUDE_DIRS}"
        )
      endif()
      # Cache vars so libzip's find_package(ZLIB) sees ZLIB as already found
      # and respects the ZLIB::ZLIB target we just created.
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
    # Tell trx-cpp which Eigen3 target to use via its TRX_EIGEN3_TARGET variable.
    # ITKEigen3 is a declared DEPENDS of TractographyTRX (itk-module.cmake), so the
    # ITKEigen3 module is processed before this file runs, which means:
    #
    # TractographyTRX_ITK_EigenExternal — when building inside an ITK source/build
    #   tree, explicitly map this compatibility target to ITK's bundled headers in
    #   Modules/ThirdParty/Eigen3/src/itkeigen. That gives third-party code a stable
    #   external include layout for #include <Eigen/Core>, regardless of how ITK's
    #   own internal wrapper targets are arranged in a given revision.
    # eigen_internal — the real build target during some ITK source/remote-module
    #   builds. Prefer it over wrapper targets when it is known to expose a correct
    #   external Eigen layout.
    # ITK::eigen_internal — installed/build-tree namespaced form of the same bundled
    #   Eigen target.
    # ITK::ITKEigen3Module — the forward-looking public ITK wrapper target. Keep it
    #   as a fallback for standalone builds, but do not rely on it for build-tree
    #   third-party wiring when its include layout may still be ITK-internal.
    # ITKInternalEigen3_DIR — old ITK (pre-PR#5831 / pre-6.0b02); no eigen_internal.
    set(_TractographyTRX_have_valid_trx_eigen_target OFF)
    if(TRX_EIGEN3_TARGET AND TARGET ${TRX_EIGEN3_TARGET})
      set(_TractographyTRX_have_valid_trx_eigen_target ON)
    endif()
    if(NOT _TractographyTRX_have_valid_trx_eigen_target)
      set(_TractographyTRX_itk_eigen_candidate_dirs
        "${ITK_SOURCE_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
        "${ITK_BINARY_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
        "${ITK_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
      )
      foreach(_TractographyTRX_itk_eigen_target
          Eigen3::Eigen
          eigen_internal
          ITK::eigen_internal
          ITK::ITKEigen3Module)
        if(TARGET ${_TractographyTRX_itk_eigen_target})
          get_target_property(
            _TractographyTRX_itk_eigen_target_includes
            ${_TractographyTRX_itk_eigen_target}
            INTERFACE_INCLUDE_DIRECTORIES
          )
          foreach(_TractographyTRX_itk_eigen_include
              ${_TractographyTRX_itk_eigen_target_includes})
            if(_TractographyTRX_itk_eigen_include MATCHES "\\$<BUILD_INTERFACE:([^>]+)>")
              set(_TractographyTRX_itk_eigen_include "${CMAKE_MATCH_1}")
            endif()
            if(_TractographyTRX_itk_eigen_include MATCHES "/itkeigen/\\.\\.$")
              string(REGEX REPLACE
                "/itkeigen/\\.\\.$"
                "/itkeigen"
                _TractographyTRX_itk_eigen_candidate_dir
                "${_TractographyTRX_itk_eigen_include}"
              )
              list(APPEND _TractographyTRX_itk_eigen_candidate_dirs
                "${_TractographyTRX_itk_eigen_candidate_dir}")
            elseif(_TractographyTRX_itk_eigen_include MATCHES "/itkeigen$")
              list(APPEND _TractographyTRX_itk_eigen_candidate_dirs
                "${_TractographyTRX_itk_eigen_include}")
            elseif(_TractographyTRX_itk_eigen_include MATCHES "/src$")
              list(APPEND _TractographyTRX_itk_eigen_candidate_dirs
                "${_TractographyTRX_itk_eigen_include}/itkeigen")
            endif()
            unset(_TractographyTRX_itk_eigen_candidate_dir)
          endforeach()
          unset(_TractographyTRX_itk_eigen_target_includes)
        endif()
      endforeach()
      list(REMOVE_DUPLICATES _TractographyTRX_itk_eigen_candidate_dirs)
      foreach(_TractographyTRX_itk_eigen_candidate_dir
          ${_TractographyTRX_itk_eigen_candidate_dirs})
        if(EXISTS "${_TractographyTRX_itk_eigen_candidate_dir}/Eigen/Core")
          set(_TractographyTRX_itk_eigen_include_dir
            "${_TractographyTRX_itk_eigen_candidate_dir}")
          break()
        endif()
      endforeach()
      if(_TractographyTRX_itk_eigen_include_dir
          AND NOT TARGET TractographyTRX_ITK_EigenExternal)
        add_library(TractographyTRX_ITK_EigenExternal INTERFACE IMPORTED GLOBAL)
        set_target_properties(TractographyTRX_ITK_EigenExternal PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${_TractographyTRX_itk_eigen_include_dir}"
        )
      endif()
      if(TARGET TractographyTRX_ITK_EigenExternal)
        # Prefer the compatibility target when building inside ITK so trx-cpp gets
        # an external Eigen include layout even if ITK already defined Eigen3::Eigen
        # with ITK-internal include directories.
        set(TRX_EIGEN3_TARGET "TractographyTRX_ITK_EigenExternal")
      elseif(TARGET Eigen3::Eigen)
        set(TRX_EIGEN3_TARGET "Eigen3::Eigen")
      elseif(TARGET eigen_internal)
        # Source build (TractographyTRX as an ITK remote module): eigen_internal is
        # the real build target; ITK:: namespaced targets don't exist at this point.
        set(TRX_EIGEN3_TARGET "eigen_internal")
      elseif(TARGET ITK::eigen_internal)
        # Installed/build-tree ITK that exposes the concrete bundled Eigen target.
        set(TRX_EIGEN3_TARGET "ITK::eigen_internal")
      elseif(TARGET ITK::ITKEigen3Module)
        # Forward-looking public wrapper target. Use it when the concrete bundled
        # targets are not available.
        set(TRX_EIGEN3_TARGET "ITK::ITKEigen3Module")
      elseif(DEFINED ITKInternalEigen3_DIR AND NOT DEFINED Eigen3_DIR)
        # Old standalone ITK: fall back to ITK's exported Eigen3Config.cmake instead
        # of permitting a random system Eigen3 to win.
        set(Eigen3_DIR "${ITKInternalEigen3_DIR}")
      endif()
    endif()
    if(TRX_EIGEN3_TARGET)
      message(STATUS "TractographyTRX: TRX_EIGEN3_TARGET=${TRX_EIGEN3_TARGET}")
      if(TARGET ${TRX_EIGEN3_TARGET})
        get_target_property(
          _TractographyTRX_selected_eigen_includes
          ${TRX_EIGEN3_TARGET}
          INTERFACE_INCLUDE_DIRECTORIES
        )
        message(STATUS "TractographyTRX: ${TRX_EIGEN3_TARGET} includes=${_TractographyTRX_selected_eigen_includes}")
      else()
        message(STATUS "TractographyTRX: selected Eigen target does not exist yet")
      endif()
    else()
      message(STATUS "TractographyTRX: TRX_EIGEN3_TARGET is unset")
    endif()
    if(_TractographyTRX_itk_eigen_include_dir)
      message(STATUS "TractographyTRX: discovered ITK external Eigen include dir=${_TractographyTRX_itk_eigen_include_dir}")
    endif()
    if(Eigen3_DIR)
      message(STATUS "TractographyTRX: Eigen3_DIR=${Eigen3_DIR}")
    endif()
    unset(_TractographyTRX_have_valid_trx_eigen_target)
    unset(_TractographyTRX_selected_eigen_includes)
    unset(_TractographyTRX_itk_eigen_include_dir)
    unset(_TractographyTRX_itk_eigen_include)
    unset(_TractographyTRX_itk_eigen_target)
    unset(_TractographyTRX_itk_eigen_candidate_dir)
    unset(_TractographyTRX_itk_eigen_candidate_dirs)
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
# Restore Eigen3::Eigen from ITK's bundled Eigen3.
# trx is a static private dependency of TractographyTRX and is exported to
# ITKTargets so that downstream static-linked consumers can link it.  Its own
# INTERFACE_LINK_LIBRARIES lists Eigen3::Eigen, which CMake validates at
# generate time in all downstream consumers.  Eigen3Config.cmake is installed
# alongside ITK into ${ITK_INSTALL_PREFIX}/share; find it there with
# NO_DEFAULT_PATH so system Eigen3 (e.g. Homebrew) is never used instead.
if(NOT TARGET Eigen3::Eigen)
  find_package(Eigen3 QUIET CONFIG
    PATHS "${ITK_INSTALL_PREFIX}/share"
    NO_DEFAULT_PATH
  )
endif()

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
