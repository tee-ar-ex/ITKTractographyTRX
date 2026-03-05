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
  itkTrxParcellationLabeler.cxx
)

itk_module_add_library(TractographyTRX ${TractographyTRX_SRCS})
target_link_libraries(TractographyTRX PRIVATE trx-cpp::trx)

if(TARGET libzip::zip)
  target_link_libraries(TractographyTRX PRIVATE libzip::zip)
elseif(TARGET zip::zip)
  target_link_libraries(TractographyTRX PRIVATE zip::zip)
elseif(TARGET zip)
  target_link_libraries(TractographyTRX PRIVATE zip)
endif()
