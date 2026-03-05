add_executable(trx_group_tdi_cli
  trx_group_tdi_cli.cxx
)

target_link_libraries(trx_group_tdi_cli
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_group_tdi_cli PRIVATE cxx_std_17)

add_executable(trx_add_groups_cli
  trx_add_groups_cli.cxx
)

target_link_libraries(trx_add_groups_cli
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_add_groups_cli PRIVATE cxx_std_17)

add_executable(trx_info
  trx_info.cxx
)

target_link_libraries(trx_info
  PRIVATE
    TractographyTRX
)

target_compile_features(trx_info PRIVATE cxx_std_17)
