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
#include "itkTrxStreamlineIOBase.h"

namespace itk
{
void
TrxStreamlineIOBase::SetInput(const StreamlineDataType * input)
{
  m_Input = input;
}

const TrxStreamlineIOBase::StreamlineDataType *
TrxStreamlineIOBase::GetInput() const
{
  return m_Input.GetPointer();
}

TrxStreamlineIOBase::StreamlineDataType *
TrxStreamlineIOBase::GetOutput()
{
  return m_Output.GetPointer();
}

const TrxStreamlineIOBase::StreamlineDataType *
TrxStreamlineIOBase::GetOutput() const
{
  return m_Output.GetPointer();
}

void
TrxStreamlineIOBase::SetOutput(const StreamlineDataType * output)
{
  m_Output = const_cast<StreamlineDataType *>(output);
}
} // end namespace itk
