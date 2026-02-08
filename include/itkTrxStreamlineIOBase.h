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
#ifndef itkTrxStreamlineIOBase_h
#define itkTrxStreamlineIOBase_h

#include "TractographyTRXExport.h"

#include "itkLightProcessObject.h"
#include "itkTrxStreamlineData.h"

#include <string>

namespace itk
{
/**
 * \class TrxStreamlineIOBase
 * \brief Abstract superclass defining TRX streamline IO interface.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxStreamlineIOBase : public LightProcessObject
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxStreamlineIOBase);

  using Self = TrxStreamlineIOBase;
  using Superclass = LightProcessObject;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkOverrideGetNameOfClassMacro(TrxStreamlineIOBase);

  using StreamlineDataType = TrxStreamlineData;
  using StreamlineDataPointer = StreamlineDataType::Pointer;
  using ConstStreamlineDataPointer = StreamlineDataType::ConstPointer;

  itkSetStringMacro(FileName);
  itkGetStringMacro(FileName);

  itkSetMacro(UseCompression, bool);
  itkGetConstMacro(UseCompression, bool);
  itkBooleanMacro(UseCompression);

  void
  SetInput(const StreamlineDataType * input);

  const StreamlineDataType *
  GetInput() const;

  StreamlineDataType *
  GetOutput();

  const StreamlineDataType *
  GetOutput() const;

  virtual bool
  CanReadFile(const char * filename) = 0;

  virtual bool
  CanWriteFile(const char * filename) = 0;

  virtual void
  Read() = 0;

  virtual void
  Write() = 0;

protected:
  TrxStreamlineIOBase() = default;
  ~TrxStreamlineIOBase() override = default;

  void
  SetOutput(const StreamlineDataType * output);

  std::string m_FileName{};
  bool        m_UseCompression{ false };

private:
  ConstStreamlineDataPointer m_Input;
  StreamlineDataPointer      m_Output;
};
} // end namespace itk

#endif
