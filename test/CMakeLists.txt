set(TRX_TEST_DATA_REPO "https://github.com/tee-ar-ex/trx-test-data"
  CACHE STRING "Git repository URL for TRX test data.")
set(TRX_TEST_DATA_TAG "v0.1.0"
  CACHE STRING "Release tag for TRX test data archives.")
option(TRX_DOWNLOAD_TEST_DATA "Download TRX test data archives at configure time." ON)
set(TRX_TEST_DATA_ARCHIVES "gold_standard.zip"
  CACHE STRING "Semicolon-separated release archive names.")
set(TRX_TEST_DATA_DIR "${CMAKE_BINARY_DIR}/_trx_test_data"
  CACHE PATH "Local test data directory.")
set(TRX_TEST_DATA_ARCHIVE_DIR "${CMAKE_BINARY_DIR}/_trx_test_data_archives"
  CACHE PATH "Local test data archive directory.")

file(MAKE_DIRECTORY "${TRX_TEST_DATA_DIR}")
file(MAKE_DIRECTORY "${TRX_TEST_DATA_ARCHIVE_DIR}")
if(TRX_DOWNLOAD_TEST_DATA)
  message(STATUS "Configuring test data from ${TRX_TEST_DATA_REPO} release ${TRX_TEST_DATA_TAG}")
  foreach(_trx_archive IN LISTS TRX_TEST_DATA_ARCHIVES)
    set(_trx_expected_hash "")
    if(_trx_archive STREQUAL "gold_standard.zip")
      set(_trx_expected_hash "35a0b633560cc2b0d8ecda885aa72d06385499e0cd1ca11a956b0904c3358f01")
    endif()
    if(NOT _trx_expected_hash)
      message(FATAL_ERROR "No SHA256 hash configured for ${_trx_archive}.")
    endif()

    set(_trx_archive_url "${TRX_TEST_DATA_REPO}/releases/download/${TRX_TEST_DATA_TAG}/${_trx_archive}")
    set(_trx_archive_path "${TRX_TEST_DATA_ARCHIVE_DIR}/${_trx_archive}")
    set(_trx_marker "${TRX_TEST_DATA_DIR}/${_trx_archive}.extracted")

    if(EXISTS "${_trx_archive_path}")
      file(SHA256 "${_trx_archive_path}" _trx_actual_hash)
      if(NOT _trx_actual_hash STREQUAL _trx_expected_hash)
        message(STATUS "Hash mismatch for ${_trx_archive}; re-downloading.")
        file(REMOVE "${_trx_archive_path}")
      endif()
    endif()

    if(NOT EXISTS "${_trx_archive_path}")
      message(STATUS "Downloading ${_trx_archive}...")
      file(DOWNLOAD "${_trx_archive_url}" "${_trx_archive_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        EXPECTED_HASH "SHA256=${_trx_expected_hash}"
        STATUS _download_status
      )
      list(GET _download_status 0 _status_code)
      if(NOT _status_code EQUAL 0)
        list(GET _download_status 1 _status_message)
        message(FATAL_ERROR "Failed to download ${_trx_archive}: ${_status_message}")
      endif()
    endif()

    if(NOT EXISTS "${_trx_marker}")
      message(STATUS "Extracting ${_trx_archive}...")
      file(ARCHIVE_EXTRACT INPUT "${_trx_archive_path}" DESTINATION "${TRX_TEST_DATA_DIR}")
      file(TOUCH "${_trx_marker}")
    endif()
  endforeach()
  message(STATUS "Test data available at ${TRX_TEST_DATA_DIR}")
else()
  message(STATUS "TRX_DOWNLOAD_TEST_DATA=OFF; skipping test data download.")
endif()

itk_module_test()

set(TractographyTRXTests
  itkTrxReadWriteTest.cxx
  itkTrxWrapperTest.cxx
  itkTrxGoldStandardTest.cxx
  itkTrxGroupTest.cxx
  itkTrxParcellationLabelingTest.cxx
)

createtestdriver(TractographyTRX "${TractographyTRX-Test_LIBRARIES}" "${TractographyTRXTests}")

if(TARGET trx-cpp::trx)
  target_link_libraries(TractographyTRXTestDriver PRIVATE trx-cpp::trx)
endif()

itk_add_test(
  NAME itkTrxReadWriteTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxReadWriteTest
    ${ITK_TEST_OUTPUT_DIR}/trx_test
)

itk_add_test(
  NAME itkTrxWrapperTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxWrapperTest
    ${ITK_TEST_OUTPUT_DIR}/trx_wrapper
)

itk_add_test(
  NAME itkTrxGoldStandardTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxGoldStandardTest
)

set_tests_properties(
  itkTrxReadWriteTest
  PROPERTIES
    ENVIRONMENT
      "TRX_CPP_DEBUG_SAVE=1"
)

set_tests_properties(
  itkTrxGoldStandardTest
  PROPERTIES
    ENVIRONMENT
      "TRX_TEST_DATA_DIR=${TRX_TEST_DATA_DIR}"
)

itk_add_test(
  NAME itkTrxGroupTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxGroupTest
    ${ITK_TEST_OUTPUT_DIR}/trx_group
)

itk_add_test(
  NAME itkTrxParcellationLabelingTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxParcellationLabelingTest
    ${ITK_TEST_OUTPUT_DIR}/trx_parcellation
)
