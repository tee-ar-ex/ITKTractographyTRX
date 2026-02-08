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
#include "itkTrxFileWriter.h"

#include "itkMacro.h"

#include <algorithm>
#include <cstdlib>

#include "itkTrxStreamlineIOFactory.h"

namespace itk
{
void
TrxFileWriter::SetInput(const InputType * input)
{
  this->SetNthInput(0, const_cast<InputType *>(input));
}

const TrxFileWriter::InputType *
TrxFileWriter::GetInput() const
{
  return itkDynamicCastInDebugMode<const InputType *>(this->Superclass::GetInput(0));
}

void
TrxFileWriter::GenerateData()
{
  if (m_FileName.empty())
  {
    itkExceptionMacro("FileName is empty.");
  }

  const auto * input = this->GetInput();
  if (input == nullptr)
  {
    itkExceptionMacro("Input is null.");
  }

  auto io = TrxStreamlineIOFactory::CreateTrxStreamlineIO(m_FileName.c_str(), IOFileModeEnum::WriteMode);
  if (!io)
  {
    itkExceptionMacro("No TRX IO available for: " << m_FileName);
  }
  io->SetFileName(m_FileName);
  io->SetUseCompression(m_UseCompression);
  io->SetInput(input);
  io->Write();
}

void
TrxFileWriter::Update()
{
  this->GenerateData();
}
} // end namespace itk
