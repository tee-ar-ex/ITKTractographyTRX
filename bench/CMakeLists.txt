set(TractographyTRX_BENCH_REFERENCE_URL "https://figshare.com/ndownloader/files/62317780" CACHE STRING "URL for the reference TRX benchmark dataset.")
option(TractographyTRX_DOWNLOAD_BENCH_DATA "Download benchmark reference data at configure time." ON)
set(TractographyTRX_BENCH_DATA_DIR "${CMAKE_BINARY_DIR}/_trx_bench_data" CACHE PATH "Local benchmark data directory.")
set(TractographyTRX_BENCH_REFERENCE_TRX "${TractographyTRX_BENCH_DATA_DIR}/10milHCP_dps-sift2.trx" CACHE FILEPATH "Path to the downloaded benchmark reference TRX file.")
set(TractographyTRX_BENCH_REFERENCE_MD5 "094f19039c7e43d1d1ddd63be55815d0" CACHE STRING "Expected MD5 for the benchmark reference TRX file.")

file(MAKE_DIRECTORY "${TractographyTRX_BENCH_DATA_DIR}")
if(TractographyTRX_DOWNLOAD_BENCH_DATA)
  set(_trx_bench_download_url "${TractographyTRX_BENCH_REFERENCE_URL}")
  if(_trx_bench_download_url MATCHES "^https://figshare\\.com/ndownloader/files/([0-9]+)$")
    # figshare.com/ndownloader can return a WAF challenge to non-browser clients.
    # Use the canonical ndownloader host for automated CMake downloads.
    string(REGEX REPLACE "^https://figshare\\.com/ndownloader/files/([0-9]+)$"
                         "https://ndownloader.figshare.com/files/\\1"
                         _trx_bench_download_url "${_trx_bench_download_url}")
  endif()
  message(STATUS "Configuring benchmark reference data download from ${_trx_bench_download_url}")

  if(EXISTS "${TractographyTRX_BENCH_REFERENCE_TRX}" AND TractographyTRX_BENCH_REFERENCE_MD5)
    file(MD5 "${TractographyTRX_BENCH_REFERENCE_TRX}" _trx_bench_actual_hash)
    if(NOT _trx_bench_actual_hash STREQUAL TractographyTRX_BENCH_REFERENCE_MD5)
      message(STATUS "Hash mismatch for benchmark reference TRX; re-downloading.")
      file(REMOVE "${TractographyTRX_BENCH_REFERENCE_TRX}")
    endif()
  endif()

  if(NOT EXISTS "${TractographyTRX_BENCH_REFERENCE_TRX}")
    message(STATUS "Downloading benchmark reference TRX...")
    file(DOWNLOAD "${_trx_bench_download_url}" "${TractographyTRX_BENCH_REFERENCE_TRX}"
      SHOW_PROGRESS
      TLS_VERIFY ON
      EXPECTED_HASH "MD5=${TractographyTRX_BENCH_REFERENCE_MD5}"
      STATUS _trx_bench_download_status
    )
    list(GET _trx_bench_download_status 0 _trx_bench_status_code)
    if(NOT _trx_bench_status_code EQUAL 0)
      list(GET _trx_bench_download_status 1 _trx_bench_status_message)
      message(FATAL_ERROR "Failed to download benchmark reference TRX: ${_trx_bench_status_message}")
    endif()

    file(SIZE "${TractographyTRX_BENCH_REFERENCE_TRX}" _trx_bench_size_bytes)
    if(_trx_bench_size_bytes EQUAL 0)
      file(REMOVE "${TractographyTRX_BENCH_REFERENCE_TRX}")
      message(FATAL_ERROR "Downloaded benchmark reference TRX is empty. Check URL/access and retry.")
    endif()
  endif()

  message(STATUS "Benchmark reference TRX available at ${TractographyTRX_BENCH_REFERENCE_TRX}")
else()
  message(STATUS "TractographyTRX_DOWNLOAD_BENCH_DATA=OFF; skipping benchmark reference data download.")
endif()

add_executable(bench_trx_itk_realdata
  bench_trx_itk_realdata.cpp
)

target_link_libraries(bench_trx_itk_realdata
  PRIVATE
    TractographyTRX
    benchmark::benchmark
)

target_compile_features(bench_trx_itk_realdata PRIVATE cxx_std_17)
