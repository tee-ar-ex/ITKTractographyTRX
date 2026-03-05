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

  # Additional files keyed by Figshare file id for readability/maintainability:
  # <figshare-id>|<output-filename>|<expected-md5>
  set(_trx_bench_additional_files
    "62394673|native_space_seg-Glasser_dseg.nii.gz|e598af5c96ba234f336837930b25bfe5"
    "62394676|native_space_seg-4S456Parcels_dseg.txt|22e51421c4432cd88d359a28ff254d1f"
    "62394994|native_space_seg-4S456Parcels_dseg.nii.gz|a3c229ffb704f13035df05afc2278f88"
    "62394670|native_space_seg-Glasser_dseg.txt|d8f29247531dd761714fb374e359eeb8"
  )

  foreach(_trx_extra_spec IN LISTS _trx_bench_additional_files)
    string(REPLACE "|" ";" _trx_extra_parts "${_trx_extra_spec}")
    list(GET _trx_extra_parts 0 _trx_extra_id)
    list(GET _trx_extra_parts 1 _trx_extra_filename)
    list(GET _trx_extra_parts 2 _trx_extra_md5)

    set(_trx_extra_download_url "https://figshare.com/ndownloader/files/${_trx_extra_id}")
    if(_trx_extra_download_url MATCHES "^https://figshare\\.com/ndownloader/files/([0-9]+)$")
      string(REGEX REPLACE "^https://figshare\\.com/ndownloader/files/([0-9]+)$"
                           "https://ndownloader.figshare.com/files/\\1"
                           _trx_extra_download_url "${_trx_extra_download_url}")
    endif()
    set(_trx_extra_path "${TractographyTRX_BENCH_DATA_DIR}/${_trx_extra_filename}")

    if(EXISTS "${_trx_extra_path}")
      file(MD5 "${_trx_extra_path}" _trx_extra_actual_hash)
      if(NOT _trx_extra_actual_hash STREQUAL _trx_extra_md5)
        message(STATUS "Hash mismatch for ${_trx_extra_filename}; re-downloading.")
        file(REMOVE "${_trx_extra_path}")
      endif()
    endif()

    if(NOT EXISTS "${_trx_extra_path}")
      message(STATUS "Downloading additional benchmark data: ${_trx_extra_filename}")
      file(DOWNLOAD "${_trx_extra_download_url}" "${_trx_extra_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        EXPECTED_HASH "MD5=${_trx_extra_md5}"
        STATUS _trx_extra_download_status
      )
      list(GET _trx_extra_download_status 0 _trx_extra_status_code)
      if(NOT _trx_extra_status_code EQUAL 0)
        list(GET _trx_extra_download_status 1 _trx_extra_status_message)
        message(FATAL_ERROR "Failed to download additional benchmark data (${_trx_extra_filename}): ${_trx_extra_status_message}")
      endif()

      file(SIZE "${_trx_extra_path}" _trx_extra_size_bytes)
      if(_trx_extra_size_bytes EQUAL 0)
        file(REMOVE "${_trx_extra_path}")
        message(FATAL_ERROR "Downloaded additional benchmark data is empty (${_trx_extra_filename}).")
      endif()
    endif()

    message(STATUS "Additional benchmark data available at ${_trx_extra_path}")
  endforeach()
else()
  message(STATUS "TractographyTRX_DOWNLOAD_BENCH_DATA=OFF; skipping benchmark reference data download.")
endif()

add_executable(bench_trx_itk_realdata
  bench_trx_itk_realdata.cpp
)

target_link_libraries(bench_trx_itk_realdata
  PRIVATE
    TractographyTRX
    trx-cpp::trx
    benchmark::benchmark
)

target_compile_features(bench_trx_itk_realdata PRIVATE cxx_std_17)

# Convenience target: run all benchmarks against the downloaded reference data.
add_custom_target(run_bench
  COMMAND bench_trx_itk_realdata
    --reference-trx "${TractographyTRX_BENCH_REFERENCE_TRX}"
    --bench-data-dir "${TractographyTRX_BENCH_DATA_DIR}"
  DEPENDS bench_trx_itk_realdata
  COMMENT "Running ITKTractographyTRX benchmarks"
  USES_TERMINAL
)

# Parcellation-only subset for quicker iteration.
add_custom_target(run_bench_parcellate
  COMMAND bench_trx_itk_realdata
    --reference-trx "${TractographyTRX_BENCH_REFERENCE_TRX}"
    --bench-data-dir "${TractographyTRX_BENCH_DATA_DIR}"
    --benchmark_filter=BM_Parcellate
  DEPENDS bench_trx_itk_realdata
  COMMENT "Running parcellation benchmarks only"
  USES_TERMINAL
)
