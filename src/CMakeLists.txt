set(
  TractographyTRX_SRCS
  itkTrxFileReader.cxx
  itkTrxFileWriter.cxx
  itkTrxStreamlineIOBase.cxx
  itkTrxStreamlineIO.cxx
  itkTrxStreamlineIOFactory.cxx
  itkTrxStreamlineData.cxx
  itkTrxStreamWriter.cxx
)

itk_module_add_library(TractographyTRX ${TractographyTRX_SRCS})
target_link_libraries(TractographyTRX PUBLIC trx-cpp::trx)
