set(
  DOCUMENTATION
  "TRX tractography support with lazy, queryable streamline access and a \
streaming writer that enforces DPS/DPV synchronization. See the Insight \
Journal article: <a href=\"https://insight-journal.org/browse/publication/XXXX\">TRX tractography in ITK</a>."
)

itk_module(
  TractographyTRX
  ENABLE_SHARED
  DEPENDS
    ITKCommon
    ITKTransform
    ITKEigen3
    ITKIONIFTI
    ITKMathematicalMorphology
  TEST_DEPENDS
    ITKTestKernel
  DESCRIPTION "${DOCUMENTATION}"
  EXCLUDE_FROM_DEFAULT
)
