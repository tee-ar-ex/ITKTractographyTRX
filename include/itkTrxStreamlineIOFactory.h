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
#ifndef itkTrxStreamlineIOFactory_h
#define itkTrxStreamlineIOFactory_h

#include "TractographyTRXExport.h"

#include "itkObjectFactoryBase.h"
#include "itkTrxStreamlineIOBase.h"
#include "itkCommonEnums.h"

namespace itk
{
/**
 * \class TrxStreamlineIOFactory
 * \brief Create instances of TRX streamline IO objects using an object factory.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxStreamlineIOFactory : public ObjectFactoryBase
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxStreamlineIOFactory);

  using Self = TrxStreamlineIOFactory;
  using Superclass = ObjectFactoryBase;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkFactorylessNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxStreamlineIOFactory);

  using TrxStreamlineIOBasePointer = TrxStreamlineIOBase::Pointer;

  static TrxStreamlineIOBasePointer
  CreateTrxStreamlineIO(const char * path, IOFileModeEnum mode);

  const char *
  GetITKSourceVersion() const override;

  const char *
  GetDescription() const override;

protected:
  TrxStreamlineIOFactory();
  ~TrxStreamlineIOFactory() override;
};
} // end namespace itk

#endif
