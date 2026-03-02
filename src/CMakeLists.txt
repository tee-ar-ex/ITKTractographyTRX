set(
  TractographyTRX_SRCS
  itkTrxFileReader.cxx
  itkTrxFileWriter.cxx
  itkTrxGroup.cxx
  itkTrxStreamlineIOBase.cxx
  itkTrxStreamlineIO.cxx
  itkTrxStreamlineIOFactory.cxx
  itkTrxStreamlineData.cxx
  itkTrxStreamWriter.cxx
)

itk_module_add_library(TractographyTRX ${TractographyTRX_SRCS})
target_link_libraries(TractographyTRX PRIVATE trx-cpp::trx)
if(TARGET trx)
  add_dependencies(TractographyTRX trx)
endif()
