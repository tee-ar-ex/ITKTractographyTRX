itk_wrap_module(TractographyTRX)

set(WRAPPER_LIBRARY_DEPENDS ITKCommon ITKTransform ITKIONIFTI ITKMathematicalMorphology)

set(WRAPPER_SUBMODULE_ORDER
  itkTrxGroup
  itkTrxStreamlineData
  itkTrxFileReader
  itkTrxFileWriter
  itkTrxStreamWriter
  itkTrxParcellationLabeler
  itkTrxGroupTdiMapper
  itkTrxTransformBridge
)

itk_auto_load_and_end_wrap_submodules()
