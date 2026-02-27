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
