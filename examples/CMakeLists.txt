add_executable(trx_group_tdi
  trx_group_tdi_cli.cxx
)

target_link_libraries(trx_group_tdi
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_group_tdi PRIVATE cxx_std_17)

add_executable(trx_add_groups
  trx_add_groups_cli.cxx
)

target_link_libraries(trx_add_groups
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_add_groups PRIVATE cxx_std_17)

add_executable(trx_info
  trx_info.cxx
)

target_link_libraries(trx_info
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_info PRIVATE cxx_std_17)

add_executable(trx_extract_subset
  trx_extract_subset_cli.cxx
)

target_link_libraries(trx_extract_subset
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_extract_subset PRIVATE cxx_std_17)
