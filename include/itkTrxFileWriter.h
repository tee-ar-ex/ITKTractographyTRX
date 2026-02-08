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
#ifndef itkTrxFileWriter_h
#define itkTrxFileWriter_h

#include "TractographyTRXExport.h"

#include "itkProcessObject.h"
#include "itkTrxStreamlineData.h"

namespace itk
{
/**
 * \class TrxFileWriter
 * \brief Write TRX streamline files from a TrxStreamlineData object.
 *
 * For streaming generation of new tractography, prefer TrxStreamWriter, which
 * enforces per-streamline DPS/DPV synchronization.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxFileWriter : public ProcessObject
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxFileWriter);

  using Self = TrxFileWriter;
  using Superclass = ProcessObject;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxFileWriter);

  using InputType = TrxStreamlineData;

  using Superclass::SetInput;
  void
  SetInput(const InputType * input);

  const InputType *
  GetInput() const;

  itkSetStringMacro(FileName);
  itkGetStringMacro(FileName);

  itkSetMacro(UseCompression, bool);
  itkGetConstMacro(UseCompression, bool);
  itkBooleanMacro(UseCompression);

  /** Write the TRX file. */
  void
  Update() override;

protected:
  TrxFileWriter() = default;
  ~TrxFileWriter() override = default;

  void
  GenerateData() override;

private:
  std::string m_FileName{};
  bool        m_UseCompression{ false };
};
} // end namespace itk

#endif
