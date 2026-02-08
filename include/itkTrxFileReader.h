/*=========================================================================
 *
 *  Copyright NumFOCUS
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef itkTrxFileReader_h
#define itkTrxFileReader_h

#include "TractographyTRXExport.h"

#include "itkProcessObject.h"
#include "itkTrxStreamlineData.h"

namespace itk
{
/**
 * \class TrxFileReader
 * \brief Read TRX streamline files into a TrxStreamlineData object.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxFileReader : public ProcessObject
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxFileReader);

  using Self = TrxFileReader;
  using Superclass = ProcessObject;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxFileReader);

  using OutputType = TrxStreamlineData;
  using OutputPointer = OutputType::Pointer;

  void
  SetFileName(const std::string & filename);

  const std::string &
  GetFileName() const;

  OutputType *
  GetOutput();

  const OutputType *
  GetOutput() const;

protected:
  TrxFileReader();
  ~TrxFileReader() override = default;

  DataObjectPointer
  MakeOutput(DataObjectPointerArraySizeType idx) override;

  void
  GenerateData() override;

private:
  std::string m_FileName{};
};
} // end namespace itk

#endif
