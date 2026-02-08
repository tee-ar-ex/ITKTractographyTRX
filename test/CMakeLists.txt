itk_module_test()

set(TractographyTRXTests itkTrxReadWriteTest.cxx)

createtestdriver(TractographyTRX "${TractographyTRX-Test_LIBRARIES}" "${TractographyTRXTests}")

itk_add_test(
  NAME itkTrxReadWriteTest
  COMMAND
    TractographyTRXTestDriver
    itkTrxReadWriteTest
    ${ITK_TEST_OUTPUT_DIR}/trx_test
)

set_tests_properties(
  itkTrxReadWriteTest
  PROPERTIES
    ENVIRONMENT
      "TRX_CPP_DEBUG_SAVE=1"
)
