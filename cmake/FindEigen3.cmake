if(TARGET Eigen3::Eigen)
  set(Eigen3_FOUND TRUE)
  return()
endif()

if(NOT EIGEN3_INCLUDE_DIR)
  find_path(EIGEN3_INCLUDE_DIR Eigen/Dense
    HINTS
      "${Eigen3_ROOT}"
      "${Eigen3_ROOT}/include"
      "${EIGEN3_ROOT}"
      "${EIGEN3_ROOT}/include"
      "${Eigen3_DIR}"
      "${ITK_SOURCE_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
      "${ITK_BINARY_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
      "${ITK_DIR}/Modules/ThirdParty/Eigen3/src/itkeigen"
    PATH_SUFFIXES eigen3
  )
endif()

if(EIGEN3_INCLUDE_DIR)
  # GLOBAL so the target is visible inside FetchContent-added subdirectories
  # (CMake 3.24+ scopes IMPORTED targets to the creating directory by default).
  add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
  set_target_properties(Eigen3::Eigen PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}"
  )
  set(Eigen3_FOUND TRUE)
  set(EIGEN3_INCLUDE_DIRS "${EIGEN3_INCLUDE_DIR}")
else()
  set(Eigen3_FOUND FALSE)
endif()
