cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
cmake_policy(VERSION 3.16.3)

project(TractographyTRX)

set(TractographyTRX_LIBRARIES TractographyTRX)

option(TractographyTRX_BUILD_BENCHMARKS "Build TractographyTRX benchmarks" OFF)
option(TractographyTRX_BUILD_EXAMPLES "Build TractographyTRX example executables" OFF)

if(NOT ITK_SOURCE_DIR)
  include(itk-module-init.cmake)
  find_package(ITK REQUIRED)
  list(APPEND CMAKE_MODULE_PATH ${ITK_CMAKE_DIR})
  include(ITKModuleExternal)
else()
  include(itk-module-init.cmake)
  itk_module_impl()
endif()

if(TractographyTRX_BUILD_BENCHMARKS)
  find_package(benchmark REQUIRED)
  add_subdirectory(bench)
endif()

if(TractographyTRX_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
