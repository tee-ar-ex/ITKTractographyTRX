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
#ifndef itkTrxStreamlineIO_h
#define itkTrxStreamlineIO_h

#include "TractographyTRXExport.h"

#include "itkTrxStreamlineIOBase.h"

namespace itk
{
/**
 * \class TrxStreamlineIO
 * \brief TRX streamline IO implementation backed by trx-cpp.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxStreamlineIO : public TrxStreamlineIOBase
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxStreamlineIO);

  using Self = TrxStreamlineIO;
  using Superclass = TrxStreamlineIOBase;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxStreamlineIO);

  bool
  CanReadFile(const char * filename) override;

  bool
  CanWriteFile(const char * filename) override;

  void
  Read() override;

  void
  Write() override;

protected:
  TrxStreamlineIO() = default;
  ~TrxStreamlineIO() override = default;
};
} // end namespace itk

#endif
