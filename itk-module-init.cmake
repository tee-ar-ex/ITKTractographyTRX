find_package(trx-cpp REQUIRED)

# When this module is loaded by an app, load trx-cpp too.
set(TractographyTRX_EXPORT_CODE_INSTALL "
find_package(trx-cpp REQUIRED)
")
set(TractographyTRX_EXPORT_CODE_BUILD "
if(NOT ITK_BINARY_DIR)
  find_package(trx-cpp REQUIRED)
endif()
")
